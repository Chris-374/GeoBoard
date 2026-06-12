CC ?= gcc
AR ?= ar
CFLAGS ?= -Wall -Wextra -Werror -std=c11 -O2 -Iinclude
BUILD_DIR := build
LIB := $(BUILD_DIR)/libgeoboard.a
OBJ := $(BUILD_DIR)/geoboard.o
TEST := $(BUILD_DIR)/test_geoboard

.PHONY: all test run clean

all: $(LIB)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OBJ): src/geoboard.c include/geoboard.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/geoboard.c -o $(OBJ)

$(LIB): $(OBJ)
	$(AR) rcs $(LIB) $(OBJ)

test: $(LIB) tests/test_geoboard.c
	$(CC) $(CFLAGS) tests/test_geoboard.c -L$(BUILD_DIR) -lgeoboard -o $(TEST)

run: test
	./$(TEST)

clean:
	rm -rf $(BUILD_DIR)
