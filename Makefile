# Path to RocksDB source tree (contains include/ and built librocksdb.a).
ROCKSDB_DIR ?= $(HOME)/workspace/CS-525/rocksdb

CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra \
            -Iinclude -Isrc \
            -I$(ROCKSDB_DIR)/include

SRCS := src/compaction_controller.cc \
        src/metrics.cc \
        src/policy.cc

OBJS := $(SRCS:.cc=.o)
LIB  := libclearctrl.a

.PHONY: all clean

all: $(LIB)

$(LIB): $(OBJS)
	ar rcs $@ $^

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Header dependencies (manual, kept minimal for a research project).
src/compaction_controller.o: src/compaction_controller.cc \
    include/compaction_controller.h src/metrics.h src/policy.h
src/metrics.o: src/metrics.cc src/metrics.h
src/policy.o:  src/policy.cc  src/policy.h  src/metrics.h

clean:
	rm -f $(OBJS) $(LIB)
