
CC = gcc

BIN = v4l2jpeg

FLAGS = -Wall -Werror

OBJ = main.o mjpegtojpeg.o

%.o: %.c
	$(CC) -c -o $@ $< $(FLAGS)

$(BIN): $(OBJ)
	gcc -o $@ $^ $(FLAGS)

.PHONY: clean

clean:
	rm -f $(OBJ)