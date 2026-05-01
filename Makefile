THIRD_PARTY := $(CURDIR)/third_party/install
LIBDIR := $(THIRD_PARTY)/lib
OLD_GCC_LIBDIR ?= $(CURDIR)
RUN_LIB_PATH := $(OLD_GCC_LIBDIR):$(LIBDIR)
LIBPROFILER := $(LIBDIR)/libprofiler.a
LIBUNWIND := $(LIBDIR)/libunwind.so

CXX ?= g++
CXXFLAGS += -O0 -g -std=c++17 -fno-omit-frame-pointer -pthread -I$(THIRD_PARTY)/include
LDLIBS += $(LIBPROFILER) -L$(LIBDIR) -lunwind -ldl -pthread

TARGET := minimal_gperftools_libunwind_helper

.PHONY: all deps run run-helper run-upstream-helper clean distclean

all: $(TARGET)

deps:
	cmake -S third_party -B third_party/build -DCMAKE_INSTALL_PREFIX=$(THIRD_PARTY)
	cmake --build third_party/build --target helper --parallel

$(TARGET): main.cpp $(LIBPROFILER) $(LIBUNWIND)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDLIBS)

$(LIBPROFILER) $(LIBUNWIND):
	$(MAKE) deps

$(LIBDIR)/unwind_safeness_helper.so $(LIBDIR)/unwind_safeness_helper_upstream.so:
	$(MAKE) deps

run: $(TARGET)
	LD_LIBRARY_PATH=$(RUN_LIB_PATH) CPUPROFILE_FREQUENCY=4000 ./$(TARGET)

run-helper: $(TARGET) $(LIBDIR)/unwind_safeness_helper.so
	LD_PRELOAD=$(LIBDIR)/unwind_safeness_helper.so LD_LIBRARY_PATH=$(RUN_LIB_PATH) CPUPROFILE_FREQUENCY=4000 ./$(TARGET)

run-upstream-helper: $(TARGET) $(LIBDIR)/unwind_safeness_helper_upstream.so
	LD_PRELOAD=$(LIBDIR)/unwind_safeness_helper_upstream.so LD_LIBRARY_PATH=$(RUN_LIB_PATH) CPUPROFILE_FREQUENCY=4000 ./$(TARGET)

clean:
	rm -f $(TARGET) cpu_profile.out

distclean: clean
	rm -rf third_party/build third_party/install third_party/src third_party/downloads
