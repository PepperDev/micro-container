TARGET  ?= bin/cage
DESTDIR ?= /usr/local

SOURCES = $(patsubst %,src/%.c, \
		config \
		validate \
		proc \
		cage \
		mount \
		root \
		launch \
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

.objs/%.o: src/%.c | .objs
	$(CC) $(CFLAGS) -c $< -o $@

.objs/config.o: src/config.h

.objs/validate.o: src/validate.h
.objs/proc.o: src/proc.h

.objs/mount.o: src/mount.h
.objs/root.o: src/root.h
.objs/launch.o: src/launch.h

.objs/cage.o: src/cage.h src/config.h
.objs/main.o: src/config.h src/validate.h src/proc.h src/cage.h

$(DIRS):
	-mkdir -p $@

clean:
	-rm -f $(TARGET) $(OBJS)
	-rmdir -p $(DIRS)

install:
	install -o 0 -g 0 -m 4755 $(TARGET) $(DESTDIR)/bin/
