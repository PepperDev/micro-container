TARGET  ?= bin/container
DESTDIR ?= /usr/local

SOURCES = $(patsubst %,src/%.c,main param)

OBJS    = $(SOURCES:src/%.c=.objs/%.o)

CFLAGS  = -O2
LDFLAGS =
LIBS    =

all: $(TARGET)
	strip $(TARGET)

$(TARGET): dirs $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@ $(LIBS)

.objs/%.o: src/%.c
	$(CC) $(CFLAGS) -c $? -o $@

dirs:
	-mkdir -p $(dir $(TARGET) $(OBJS))

clean:
	-rm -f $(TARGET) $(OBJS)
	-rmdir -p $(dir $(TARGET) $(OBJS))

install:
	install -o 0 -g 0 -m 4755 $(TARGET) $(DESTDIR)/bin/
