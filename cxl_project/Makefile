CC := gcc
CFLAGS := -O2 -Wall -Wextra -Iinclude
LDFLAGS := -pthread -lnuma

TARGET := cxl_numa_csma
SRC := src/main.c src/config.c src/utils.c src/numa_backend.c src/stats.c src/worker.c
OBJ := $(SRC:.c=.o)

.PHONY: all clean run-smoke

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run-smoke: $(TARGET)
	./$(TARGET) 0 50 2 3 1 0 1 256 1024
	./$(TARGET) 1 50 2 3 1 0 1 256 1024

clean:
	rm -f $(TARGET) $(OBJ)
