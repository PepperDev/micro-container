TARGET  ?= bin/cage
DESTDIR ?= /usr/local

SOURCES = $(patsubst %,src/%.c, \
		super/super_parse \
		super/super_fix \
		super/super_escalate \
		error/error_log \
		config/config_parse \
		mem \
		user \
		io \
		proc \
		env \
		overlay \
		mount \
		root \
		launch \
		cage \
		main \
	)

OBJS    = $(SOURCES:src/%.c=.objs/%.o)
DIRS    = $(patsubst %/,%,$(sort $(dir $(TARGET) $(OBJS))))

CFLAGS  = -std=c99 -pedantic -O3 -Wall
LDFLAGS = -static
LIBS    =

all: $(TARGET)
.PHONY: all clear install

$(TARGET): $(OBJS) | bin
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
	strip $@

.objs/%.o: src/%.c | .objs $(DIRS)
	$(CC) $(CFLAGS) -c $< -o $@

$(DIRS):
	-mkdir -p $@

clean:
	-rm -rf $(TARGET) .objs

install:
	install -o 0 -g 0 -m 4755 $(TARGET) $(DESTDIR)/bin/
