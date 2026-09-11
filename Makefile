override CFLAGS=-std=c17 -D_GNU_SOURCE -Wall -Wextra -Wshadow -Wvla -Wno-unused-parameter -Wno-unused-const-variable -g -O0 --coverage
override LDLIBS=-lpthread -latomic -lwiringPi

ifdef CI
override CFLAGS=-std=c17 -D_GNU_SOURCE -Wall -Wextra -Wshadow -Wvla -Werror -Wno-unused-parameter -Wno-unused-const-variable
endif

NAME=servo_calib
BUILD_DIR=build
TARGET=$(BUILD_DIR)/$(NAME)

SRCS=main.c stepper.c utils.c logger.c loggerconf.c crash.c
# Prefix all object files with the build directory
OBJS=$(SRCS:%.c=$(BUILD_DIR)/%.o)

.PHONY: clean all

all: $(TARGET)

# Create the build directory if it doesn't exist
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

# Compile C objects directly into the build folder
$(BUILD_DIR)/%.o: %.c stepper.h gpio_pins.h commands.h tuibox.h logger.h loggerconf.h crash.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean removes the entire build directory
clean:
	rm -rf $(BUILD_DIR)
