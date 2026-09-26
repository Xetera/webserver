CC := clang
CFLAGS := -Wall -Werror -std=c99
CPPFLAGS := -MMD -MP
TARGET := webserver
SRC_DIR := src
BUILD_DIR := build

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

debug: CFLAGS += -g -O0 -fsanitize=address -fno-omit-frame-pointer
debug: clean $(TARGET)

clean:
	rm -rf $(TARGET) $(BUILD_DIR)

-include $(DEPS)

.PHONY: all debug clean
