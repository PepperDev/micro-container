TARGET  ?= bin/cage
DESTDIR ?= /usr/local

SOURCES = $(patsubst %,src/%.c, \
		super/super_parse \
		super/super_do \
		super/super_escalate \
		error/error_log \
		mem \
		user \
		config \
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

.objs/mem.o: src/mem.h
.objs/io.o: src/io.h src/mem.h
.objs/config.o: src/config.h src/mem.h
.objs/user.o: src/user.h src/mem.h src/io.h
.objs/proc.o: src/proc.h src/mem.h src/io.h
.objs/env.o: src/env.h src/mem.h
.objs/overlay.o: src/overlay.h src/config.h src/mem.h src/io.h src/proc.h
.objs/mount.o: src/mount.h src/mem.h src/io.h src/proc.h
.objs/root.o: src/root.h src/proc.h
.objs/launch.o: src/launch.h src/proc.h src/io.h
.objs/cage.o: src/cage.h src/config.h src/mem.h src/proc.h src/env.h src/user.h src/io.h src/overlay.h src/mount.h src/root.h src/launch.h
.objs/main.o: src/user.h src/config.h src/proc.h src/cage.h

$(DIRS):
	-mkdir -p $@

clean:
	-rm -rf $(TARGET) .objs

install:
	install -o 0 -g 0 -m 4755 $(TARGET) $(DESTDIR)/bin/
