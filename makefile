
CC = gcc

BIN = v4l2jpeg

FLAGS = -Wall -Werror

OBJ = main.o mjpegtojpeg.o

%.o: %.c
	$(CC) -c -o $@ $< $(FLAGS)

$(BIN): $(OBJ)
	$(CC) -o $@ $^ $(FLAGS)

.PHONY: clean
.PHONY: install

clean:
	rm -f $(OBJ)

install:
	mv $(BIN) /usr/local/bin