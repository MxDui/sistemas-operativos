CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude
SRC     = src/process.c src/schedulers.c src/simulador.c
BIN     = simulador

.PHONY: all test run clean

all: $(BIN)

$(BIN): $(SRC) include/process.h include/schedulers.h
	$(CC) $(CFLAGS) -o $@ $(SRC)

test: $(BIN)
	./test.sh

run: $(BIN)
	mkdir -p results
	./$(BIN) -f Procesos.txt | tee results/simulacion.txt

clean:
	rm -f $(BIN)
	rm -rf results
