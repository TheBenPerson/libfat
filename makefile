CC := gcc
CF := -Wall -Werror -Wpedantic -O0
LF := -fno-exceptions -g -fsanitize=address

all: bin/libfat.so bin/a.out

bin/libfat.so: LF += -fPIC -shared
bin/libfat.so: src/fat.cc
	$(CC) $(CF) $^ $(LF) -o $@

bin/a.out: bin/libfat.so src/main.cc
	$(CC) $(CF) $^ $(LF) -o $@

.PHONY: clean
clean:
	-rm bin/* 2> /dev/null
