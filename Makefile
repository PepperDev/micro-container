CC      ?= musl-gcc
PREFIX  ?= /usr/local
DESTDIR ?=

CAGE_VERSION ?= $(shell git describe --tags --dirty --always 2>/dev/null || echo "0.0.0-dev")

CFLAGS   ?= -std=c99 -pedantic -pedantic-errors -O3 -Wall -Wextra -Werror -flto -MMD -MP -D_GNU_SOURCE -DCAGE_VERSION=\"$(CAGE_VERSION)\" -Isrc
LDFLAGS  ?= -static -flto
TEST_CF ?= -std=c99 -pedantic -pedantic-errors -O0 -g -Wall -Wextra -Werror -coverage -MMD -MP -D_GNU_SOURCE -DCAGE_VERSION=\"$(CAGE_VERSION)\" -Isrc
TEST_LD ?= -static -coverage

SRC_DIR := src
BIN_DIR := bin
OBJ_DIR := obj
TEST_DIR := tests

SRCS     := $(shell find $(SRC_DIR) -name '*.c')
OBJS     := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
DEPS     := $(OBJS:.o=.d)

MAIN_OBJ := $(OBJ_DIR)/main.o
LIB_OBJS := $(filter-out $(MAIN_OBJ),$(OBJS))

TEST_SRCS := $(shell find $(TEST_DIR) -name '*.c')
TEST_OBJS := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/tests/%.o,$(TEST_SRCS))
TEST_DEPS := $(TEST_OBJS:.o=.d)

COV_DIR   := $(OBJ_DIR)/cov
COV_OBJS  := $(patsubst $(SRC_DIR)/%.c,$(COV_DIR)/%.o,$(SRCS))
COV_DEPS  := $(COV_OBJS:.o=.d)

TARGET := cage

.PHONY: all clean test install format lint coverage integration report

all: $(BIN_DIR)/$(TARGET)

$(BIN_DIR)/$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/tests/%.o: $(TEST_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CF) -I$(SRC_DIR) -c -o $@ $<

$(COV_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CF) -c -o $@ $<

$(OBJ_DIR)/test_runner: $(filter-out $(COV_DIR)/main.o,$(COV_OBJS)) $(TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_LD) -o $@ $^

test: $(OBJ_DIR)/test_runner
	./$(OBJ_DIR)/test_runner

INTG_DIR := integration
INTG_BIN := $(BIN_DIR)/integration

integration: $(INTG_BIN)/test_ext4
	$(INTG_BIN)/test_ext4

$(INTG_BIN)/test_ext4: $(INTG_DIR)/test_ext4.c src/ext4/ext4.c src/ext4/sb.c src/ext4/util.c src/ext4/csum.c
	@mkdir -p $(INTG_BIN)
	$(CC) $(CFLAGS) -Isrc $(LDFLAGS) -o $@ $^

install: $(BIN_DIR)/$(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN_DIR)/$(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	strip $(DESTDIR)$(PREFIX)/bin/$(TARGET)

format:
	find $(SRC_DIR) $(TEST_DIR) -name '*.[ch]' -exec indent -linux -l120 -i2 -nut \{\} \+
	find $(SRC_DIR) $(TEST_DIR) -name '*~' -delete

MAX_LOC_PER_FILE ?= 300

lint:
	cppcheck --enable=all --check-level=exhaustive \
	  -I src \
	  --inline-suppr \
	  --suppress=missingIncludeSystem \
	  --error-exitcode=1 src/
	@lizard -C 6 -L 40 -Ttoken_count=150 -a 5 -ENS src/
	@lizard -C 6 -L 40 -Ttoken_count=150 -a 5 -ENS src/ 2>/dev/null | awk -v max=$(MAX_LOC_PER_FILE) 'BEGIN{in_data=0} /^=+/ {if(in_data)exit; in_data=1; next} in_data&&/^  NLOC/{next} in_data&&/^[-]+/{next} in_data&&/^$$/{next} in_data{nloc=$$1+0; loc=$$NF; n=split(loc,a,"@"); f=a[n]; t[f]+=nloc} END{ec=0; for(f in t){if(t[f]>max){printf "FAIL: %s has %d LOC (max %d)\n",f,t[f],max; ec=1}} exit ec}'

GCOV_DIR := $(OBJ_DIR)/gcov

coverage: $(OBJ_DIR)/test_runner
	@-./$(OBJ_DIR)/test_runner >/dev/null 2>&1
	@mkdir -p $(GCOV_DIR)
	@cd $(GCOV_DIR) && \
	for f in $$(find $(CURDIR)/$(COV_DIR) -name '*.gcda' ! -name '*syscall*'); do \
	  base=$${f%.gcda}; \
	  src=$$(echo $$base | sed 's|$(CURDIR)/$(COV_DIR)/|$(CURDIR)/$(SRC_DIR)/|').c; \
	  gcov -o "$$base" -s "$(CURDIR)" "$$src" 2>/dev/null | grep 'Lines executed' | tail -1; \
	done > gcov.txt; \
	true
	@sed 's/Lines executed://; s/% of / /' $(GCOV_DIR)/gcov.txt | \
	  awk '{t+=$$2; c+=$$2*$$1/100} END {p=c/t*100; printf "Coverage: %.2f%% (%d/%d lines)\n", p, c, t; if (p < 80.0) {printf "FAIL: coverage < 80%%\n"; exit 1}}'

report: $(OBJ_DIR)/test_runner
	@out=$$(./$(OBJ_DIR)/test_runner 2>&1); \
	rc=$$?; \
	mkdir -p $(GCOV_DIR); \
	summary=$$(echo "$$out" | grep '^== Results:'); \
	echo "=== Test Results ==="; \
	if [ $$rc -eq 0 ]; then echo "  PASS  $$summary"; else echo "  FAIL  $$summary"; fi; \
	echo ""; \
	echo "=== cppcheck Offenders ==="; \
	out=$$(cppcheck --enable=all --check-level=exhaustive $(shell find src -type d | sed 's/^/-I /') --inline-suppr --suppress=missingIncludeSystem --error-exitcode=1 src/ 2>&1) || rc=1; \
	files=$$(echo "$$out" | grep '^src/' | sed 's/:.*//' | sort -u); \
	if [ -z "$$files" ]; then echo "  PASS"; else echo "$$files" | sed 's/^/    /'; echo "  FAIL  $$(echo "$$files" | wc -l) file(s)"; fi; \
	echo ""; \
	echo "=== Lizard Offenders (CCN>6 || tokens>150 || length>40 || params>5) ==="; \
	out=$$(lizard -C 6 -L 40 -Ttoken_count=150 -a 5 -ENS src/ 2>/dev/null | \
	  awk 'BEGIN{h=0} /^=+/{h++;next} h==1&&NF==7&&$$2+0==$$2{if($$2>6||$$3>150||$$5>40||$$4>5)printf "  CCN=%-2d TOK=%-3d PAR=%-2d LEN=%-2d  %s\n",$$2,$$3,$$4,$$5,$$7}'); \
	if [ -z "$$out" ]; then echo "  none"; else echo "$$out"; rc=1; fi; \
	echo ""; \
	echo "=== File LOC Offenders (> $(MAX_LOC_PER_FILE)) ==="; \
	out=$$(lizard -C 6 -L 40 -Ttoken_count=150 -a 5 -ENS src/ 2>/dev/null | \
	  awk -v max=$(MAX_LOC_PER_FILE) \
	    'BEGIN{h=0} /^=+/{h++;next} h==1&&NF==7&&$$2+0==$$2{n=split($$7,a,"@");t[a[n]]+=$$1} \
	     END{for(f in t)if(t[f]>max)printf "  %s: %d LOC\n",f,t[f]}' | \
	  sort -t: -k2 -rn); \
	if [ -z "$$out" ]; then echo "  none"; else echo "$$out"; rc=1; fi; \
	echo ""; \
	echo "=== Coverage Offenders (< 80%) ==="; \
	cd $(GCOV_DIR) && \
	data=$$(for f in $$(find $(CURDIR)/$(COV_DIR) -name '*.gcda' ! -name '*syscall*'); do \
	  base=$${f%.gcda}; \
	  src=$$(echo $$base | sed 's|$(CURDIR)/$(COV_DIR)/|$(CURDIR)/$(SRC_DIR)/|').c; \
	  short=$${src#$(CURDIR)/}; \
	  gcov -o "$$base" -s "$(CURDIR)" "$$src" 2>/dev/null | \
	    awk '/Lines executed/{split($$2,a,":");gsub(/%/,"",a[2]);split($$0,b," of ");split(b[2],c," ");print a[2],c[1],"'$$short'"}'; \
	done); \
	cov_off=$$(echo "$$data" | sort -k1 -n | awk '!seen[$$3]++{if($$1+0<80.0)print}'); \
	if [ -n "$$cov_off" ]; then echo "$$cov_off"; else echo "  none"; fi; \
	echo "$$data" | awk '{t+=$$2; c+=$$2*$$1/100} END {p=(t?c/t*100:0); s=(p<80.0?"FAIL":"PASS"); printf "\n%s Total coverage: %.2f%% (%d/%d lines)\n", s, p, c, t; if (p < 80.0) exit 1}' || rc=1; \
	exit $$rc

clean:
	rm -rf $(BIN_DIR) $(OBJ_DIR)

-include $(DEPS)
-include $(COV_DEPS)
-include $(TEST_DEPS)
