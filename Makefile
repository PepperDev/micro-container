TARGET  ?= bin/container
DESTDIR ?= /usr/local

SOURCES = $(patsubst %,src/%.c, \
		config \
		io \
		proc \
		user \
		buffer \
		validate \
		main \
	)

OBJS    = $(SOURCES:src/%.c=.objs/%.o)
DIRS    = $(patsubst %/,%,$(sort $(dir $(TARGET) $(OBJS))))

CFLAGS  = -O2 -Wall
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
.objs/io.o: src/io.h
.objs/proc.o: src/proc.h
.objs/user.o: src/user.h src/proc.h
.objs/buffer.o: src/buffer.h
.objs/validate.o: src/validate.h src/config.h src/io.h src/user.h src/buffer.h
.objs/main.o: src/config.h src/validate.h

$(DIRS):
	-mkdir -p $@

clean:
	-rm -f $(TARGET) $(OBJS)
	-rmdir -p $(DIRS)

install:
	install -o 0 -g 0 -m 4755 $(TARGET) $(DESTDIR)/bin/
