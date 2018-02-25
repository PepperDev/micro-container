TARGET  ?= bin/container
DESTDIR ?= /usr/local

SOURCES = $(patsubst %,src/%.c,main param mount io credential container)

OBJS    = $(SOURCES:src/%.c=.objs/%.o)
DIRS    = $(patsubst %/,%,$(sort $(dir $(TARGET) $(OBJS))))

CFLAGS  = -O2
LDFLAGS =
LIBS    =

all: $(TARGET)
.PHONY: all clear install

$(TARGET): $(OBJS) | bin
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
	strip $@

.objs/%.o: src/%.c | .objs
	$(CC) $(CFLAGS) -c $< -o $@

.objs/credential.o: src/io.h src/uid.h
.objs/main.o: src/param.h src/mount.h src/credential.h
.objs/mount.o: src/var.h src/io.h src/config.h src/container.h
.objs/param.o: src/config.h
.objs/container.o: src/var.h src/uid.h src/config.h

$(DIRS):
	-mkdir -p $@

clean:
	-rm -f $(TARGET) $(OBJS)
	-rmdir -p $(DIRS)

install:
	install -o 0 -g 0 -m 4755 $(TARGET) $(DESTDIR)/bin/
