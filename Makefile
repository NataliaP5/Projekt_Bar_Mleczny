CC=gcc
CFLAGS=-std=c11 -O2 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -Iinclude
LDFLAGS=

BIN=bin
SRC=src

OBJS_COMMON=$(SRC)/common.o $(SRC)/ipc.o

all: dirs $(BIN)/manager $(BIN)/cashier $(BIN)/worker $(BIN)/client

dirs:
	mkdir -p $(BIN)

$(SRC)/common.o: $(SRC)/common.c include/common.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC)/ipc.o: $(SRC)/ipc.c include/ipc.h include/shared_state.h include/common.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN)/manager: $(SRC)/manager.c $(OBJS_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN)/cashier: $(SRC)/cashier.c $(OBJS_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN)/worker: $(SRC)/worker.c $(OBJS_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN)/client: $(SRC)/client.c $(OBJS_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(SRC)/*.o
	rm -rf $(BIN)

run:
	./bin/manager 2 2 2 1 --clients 50 --arrival-ms 200 --eat-ms-min 800 --eat-ms-max 1800 --noorder-pct 5 --reserve-seats 4
