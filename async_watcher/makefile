CC = gcc

SRC = $(wildcard *.c)

OBJ = $(SRC:%.c=%)

all: $(OBJ)

%: %.c
	sudo $(CC) -o ~/bin/async_watcher/$@ $^
