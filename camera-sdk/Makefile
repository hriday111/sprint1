CC = gcc
CXX = g++
CFLAGS = -std=c17 -D_GNU_SOURCE -Wall -Wextra -Wshadow -O2 -I. -I.. -I../logging -I../TUI -I../common -I../ipc -g
CXXFLAGS = -std=c++17 -D_GNU_SOURCE -Wall -Wextra -Wshadow -O2 -I. -I.. -I../logging -I../TUI -I../common -I../ipc $(shell pkg-config --cflags opencv4) -g
OPENCV_LIBS = -lopencv_imgcodecs -lopencv_imgproc -lopencv_core

BUILDDIR = build
BINDIR = $(BUILDDIR)/bin
OBJDIR = $(BUILDDIR)/obj
LOGGER_OBJS = $(OBJDIR)/logger.o $(OBJDIR)/loggerconf.o $(OBJDIR)/crash.o
COMMON_DIR = ../common
IPC_BASE = ../ipc
# Select camera backend: make CAMERA=ueye (default)
CAMERA ?= ueye

ifeq ($(CAMERA),ueye)
  CAM_SRC = thorlabs-dcc1545m/cam.c
  CAM_HDR = thorlabs-dcc1545m/cam.h
  LDLIBS = -lueye_api -lm -lpthread
else
  $(error Unknown CAMERA=$(CAMERA); only ueye is implemented)
endif

.PHONY: all clean

all: $(BINDIR)/sweep_run $(BINDIR)/calibrate $(BINDIR)/calibrate2

$(BINDIR)/sweep_run: $(OBJDIR)/sweep_run.o $(OBJDIR)/sweep_tui.o $(OBJDIR)/cam.o \
		$(OBJDIR)/roi.o $(OBJDIR)/utils.o $(OBJDIR)/ipc_client.o $(LOGGER_OBJS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BINDIR)/calibrate: $(OBJDIR)/calibrate.o $(OBJDIR)/cam.o $(OBJDIR)/roi.o $(OBJDIR)/detect.o $(OBJDIR)/utils.o $(LOGGER_OBJS) $(OBJDIR)/ipc_client.o
	@mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS) $(OPENCV_LIBS)

$(BINDIR)/calibrate2: $(OBJDIR)/calibrate2.o $(OBJDIR)/cam.o $(OBJDIR)/roi.o $(OBJDIR)/roi_interp.o \
		$(OBJDIR)/ipc_client.o $(OBJDIR)/detect.o $(OBJDIR)/utils.o $(LOGGER_OBJS)
	@mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(OPENCV_LIBS)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Shared utils from ../common (not camera-sdk/utils.c)
$(OBJDIR)/utils.o: $(COMMON_DIR)/utils.c $(COMMON_DIR)/utils.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $(COMMON_DIR)/utils.c -o $@

$(OBJDIR)/cam.o: $(CAM_SRC) $(CAM_HDR) cam_api.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $(CAM_SRC) -o $@

$(OBJDIR)/logger.o: ../logging/logger.c ../logging/logger.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c ../logging/logger.c -o $@

$(OBJDIR)/loggerconf.o: ../logging/loggerconf.c ../logging/loggerconf.h ../logging/logger.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c ../logging/loggerconf.c -o $@

$(OBJDIR)/crash.o: ../logging/crash.c ../logging/crash.h ../logging/logger.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c ../logging/crash.c -o $@

$(OBJDIR)/detect.o: detect.cpp detect.hpp roi.h $(COMMON_DIR)/utils.h
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c detect.cpp -o $@

$(OBJDIR)/calibrate.o: calibrate.cpp detect.hpp cam_api.h roi.h $(COMMON_DIR)/utils.h
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c calibrate.cpp -o $@

$(OBJDIR)/calibrate2.o: calibrate2.cpp detect.hpp cam_api.h roi.h roi_interp.h $(IPC_BASE)/ipc_client.h $(COMMON_DIR)/utils.h
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c calibrate2.cpp -o $@

$(OBJDIR)/roi_interp.o: roi_interp.c roi_interp.h roi.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c roi_interp.c -o $@

$(OBJDIR)/sweep_tui.o: sweep_tui.c sweep_tui.h ../TUI/tuibox.h ../TUI/vt100utils.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c sweep_tui.c -o $@

$(OBJDIR)/ipc_client.o: $(IPC_BASE)/ipc_client.c $(IPC_BASE)/ipc_client.h $(COMMON_DIR)/utils.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $(IPC_BASE)/ipc_client.c -o $@

clean:
	rm -rf $(BUILDDIR)
