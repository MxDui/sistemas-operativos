CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude
SRC     = src/paging.c src/selftest.c src/main.c
BIN     = simulador

.PHONY: all test run clean verify docs

all: $(BIN)

$(BIN): $(SRC) include/paging.h include/selftest.h
	$(CC) $(CFLAGS) -o $@ $(SRC)

test: $(BIN)
	./test.sh

run: $(BIN)
	mkdir -p results
	./$(BIN) -m 4 traces/clasica.txt | tee results/clasica.txt

clean:
	rm -f $(BIN)

verify: $(BIN)
	python3 scripts/verificar.py

docs:
	$(MAKE) -C docs
