CC = gcc

SRC = $(wildcard *.c)

OBJ = $(SRC:%.c=%)

all: $(OBJ)

%: %.c
	sudo $(CC) -o ~/bin/async/$@ $^ -lrsync -lcrypto -lrt -lpthread
