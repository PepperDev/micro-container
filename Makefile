TARGET  ?= bin/container
DESTDIR ?= /usr/local

SOURCES = $(patsubst %,src/%.c, \
		_main \
		_param \
		_mount \
		_io \
		_credential \
		_container \
		_overlay \
		_user \
		config \
		io \
		validate \
		user \
		buffer \
	)

OBJS    = $(SOURCES:src/%.c=.objs/%.o)
DIRS    = $(patsubst %/,%,$(sort $(dir $(TARGET) $(OBJS))))

CFLAGS  = -O2 -Wall
LDFLAGS =
LIBS    =

all: $(TARGET)
.PHONY: all clear install

$(TARGET): $(OBJS) | bin
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
	strip $@

.objs/%.o: src/%.c | .objs
	$(CC) $(CFLAGS) -c $< -o $@

.objs/_credential.o: src/_io.h src/_uid.h
.objs/_main.o: src/_param.h src/_mount.h src/_credential.h
.objs/_mount.o: src/_var.h src/_io.h src/_config_default.h src/_container.h src/_overlay.h
.objs/_param.o: src/_config_default.h
.objs/_container.o: src/_var.h src/_uid.h src/_config_default.h src/_user.h src/_io.h
.objs/_user.o: src/_var.h src/_uid.h src/_io.h

.objs/config.o: src/config.h
.objs/io.o: src/io.h
.objs/user.o: src/user.h
.objs/validate.o: src/validate.h src/config.h src/io.h src/user.h
.objs/buffer.o: src/buffer.h

$(DIRS):
	-mkdir -p $@

clean:
	-rm -f $(TARGET) $(OBJS)
	-rmdir -p $(DIRS)

install:
	install -o 0 -g 0 -m 4755 $(TARGET) $(DESTDIR)/bin/
