CC       := gcc
CFLAGS   := -Wall -Wextra -O2
LDFLAGS  := -lX11

# Project naming
PROJECT   := winplace
SRC_DIR   := src
BUILD_DIR := build

SRCS := $(SRC_DIR)/main.c
OBJS := $(BUILD_DIR)/main.o
TARGET := $(BUILD_DIR)/$(PROJECT)

.PHONY: all clean install uninstall dirs

all: $(TARGET)

dirs:
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) | dirs
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/$(PROJECT)

uninstall:
	rm -f /usr/local/bin/$(PROJECT)
