override CFLAGS=-std=c17 -D_GNU_SOURCE -Wall -Wextra -Wshadow -Wvla -Wno-unused-parameter -Wno-unused-const-variable -g -O2 -I. -Ilogging -Icommon
override LDLIBS=-lpthread -latomic -lwiringPi
override LDFLAGS=-Wl,-z,now,-z,relro
ifdef CI
override CFLAGS=-std=c17 -D_GNU_SOURCE -Wall -Wextra -Wshadow -Wvla -Werror -Wno-unused-parameter -Wno-unused-const-variable -I. -Ilogging -Icommon
endif

NAME=device
BUILD_DIR=build
COMMON_DIR=common
TARGET=$(BUILD_DIR)/$(NAME)

# Select illumination backend: make ILLUMINATION=laser (default) or ILLUMINATION=led
ILLUMINATION ?= laser

ifeq ($(ILLUMINATION),laser)
  ILLUM_SRC = Laser/laser.c
  ILLUM_HDR = Laser/laser.h
else ifeq ($(ILLUMINATION),led)
  ILLUM_SRC = Led/led.c
  ILLUM_HDR = Led/led.h
else
  $(error Unknown ILLUMINATION=$(ILLUMINATION); use laser or led)
endif

# Select motor backend: make MOTOR=a4988 (default) or MOTOR=sg90
MOTOR ?= a4988

ifeq ($(MOTOR),a4988)
  MOTOR_SRC = A4988/stepper.c
  MOTOR_HDR = A4988/stepper.h
else ifeq ($(MOTOR),sg90)
  MOTOR_SRC = SG-90/servo.c
  MOTOR_HDR = SG-90/servo.h
else
  $(error Unknown MOTOR=$(MOTOR); use a4988 or sg90)
endif

ROOT_SRCS = main.c
OBJS = $(ROOT_SRCS:%.c=$(BUILD_DIR)/%.o) \
	$(BUILD_DIR)/utils.o \
	$(BUILD_DIR)/logger.o \
	$(BUILD_DIR)/loggerconf.o \
	$(BUILD_DIR)/crash.o \
	$(BUILD_DIR)/illumination.o \
	$(BUILD_DIR)/motor.o

.PHONY: clean all

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

# Root sources (headers live under -I paths; don't list moved headers here)
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/utils.o: $(COMMON_DIR)/utils.c $(COMMON_DIR)/utils.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $(COMMON_DIR)/utils.c -o $@

$(BUILD_DIR)/logger.o: logging/logger.c logging/logger.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c logging/logger.c -o $@

$(BUILD_DIR)/loggerconf.o: logging/loggerconf.c logging/loggerconf.h logging/logger.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c logging/loggerconf.c -o $@

$(BUILD_DIR)/crash.o: logging/crash.c logging/crash.h logging/logger.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c logging/crash.c -o $@

$(BUILD_DIR)/illumination.o: $(ILLUM_SRC) $(ILLUM_HDR) illumination_api.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $(ILLUM_SRC) -o $@

$(BUILD_DIR)/motor.o: $(MOTOR_SRC) $(MOTOR_HDR) motor_api.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $(MOTOR_SRC) -o $@

clean:
	rm -rf $(BUILD_DIR)
