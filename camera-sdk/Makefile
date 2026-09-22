CC = gcc
CXX = g++
CFLAGS = -std=c17 -D_GNU_SOURCE -Wall -Wextra -Wshadow -O2 -I. -I.. -g
CXXFLAGS = -std=c++17 -D_GNU_SOURCE -Wall -Wextra -Wshadow -O2 -I. -I.. $(shell pkg-config --cflags opencv4) -g
LDLIBS = -lueye_api -lm -lpthread
OPENCV_LIBS = -lopencv_imgcodecs -lopencv_imgproc -lopencv_core

BUILDDIR = build
BINDIR = $(BUILDDIR)/bin
OBJDIR = $(BUILDDIR)/obj

LOGGER_OBJS = $(OBJDIR)/logger.o $(OBJDIR)/loggerconf.o $(OBJDIR)/crash.o

.PHONY: all clean

all: $(BINDIR)/sweep_run  $(BINDIR)/calibrate $(BINDIR)/calibrate2

$(BINDIR)/sweep_run: $(OBJDIR)/sweep_run.o $(OBJDIR)/sweep_tui.o $(OBJDIR)/cam.o \
		$(OBJDIR)/roi.o $(OBJDIR)/utils.o $(OBJDIR)/servo_fifo.o $(LOGGER_OBJS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BINDIR)/calibrate: $(OBJDIR)/calibrate.o $(OBJDIR)/cam.o $(OBJDIR)/roi.o $(OBJDIR)/detect.o $(OBJDIR)/utils.o $(LOGGER_OBJS)
	@mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS) $(OPENCV_LIBS)

$(BINDIR)/calibrate2: $(OBJDIR)/calibrate2.o $(OBJDIR)/cam.o $(OBJDIR)/roi.o $(OBJDIR)/roi_interp.o \
		$(OBJDIR)/servo_fifo.o $(OBJDIR)/detect.o $(OBJDIR)/utils.o $(LOGGER_OBJS)
	@mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(OPENCV_LIBS)


$(OBJDIR)/%.o: %.c utils.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/logger.o: ../logger.c ../logger.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c ../logger.c -o $@

$(OBJDIR)/loggerconf.o: ../loggerconf.c ../loggerconf.h ../logger.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c ../loggerconf.c -o $@

$(OBJDIR)/crash.o: ../crash.c ../crash.h ../logger.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c ../crash.c -o $@

$(OBJDIR)/detect.o: detect.cpp detect.hpp roi.h utils.h
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c detect.cpp -o $@

$(OBJDIR)/calibrate.o: calibrate.cpp detect.hpp cam.h roi.h utils.h
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c calibrate.cpp -o $@

$(OBJDIR)/calibrate2.o: calibrate2.cpp detect.hpp cam.h roi.h roi_interp.h servo_fifo.h utils.h
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c calibrate2.cpp -o $@

$(OBJDIR)/roi_interp.o: roi_interp.c roi_interp.h roi.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c roi_interp.c -o $@

$(OBJDIR)/sweep_tui.o: sweep_tui.c sweep_tui.h ../tuibox.h ../vt100utils.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c sweep_tui.c -o $@

$(OBJDIR)/servo_fifo.o: servo_fifo.c servo_fifo.h utils.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c servo_fifo.c -o $@

clean:
	rm -rf $(BUILDDIR)
