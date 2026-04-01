# Path to RocksDB source tree (contains include/ and built librocksdb.a).
ROCKSDB_DIR ?= $(HOME)/cs525/project/rocksdb

CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra \
            -Iinclude -Isrc \
            -I$(ROCKSDB_DIR)/include

# Libraries needed to link against the RocksDB static archive.
LDFLAGS  := $(ROCKSDB_DIR)/librocksdb.a \
            -lpthread -ldl -lz

SRCS := src/compaction_controller.cc \
        src/metrics.cc \
        src/policy.cc

OBJS := $(SRCS:.cc=.o)
LIB  := libclearctrl.a

BENCH_BIN := bench

.PHONY: all test clean

all: $(LIB)

# Build the benchmark binary (links controller lib + RocksDB).
$(BENCH_BIN): tests/bench.cc $(LIB)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LIB) $(LDFLAGS)

# Build and run the benchmark; outputs go to logs/.
test: $(BENCH_BIN)
	mkdir -p logs
	./$(BENCH_BIN)

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
	rm -f $(OBJS) $(LIB) $(BENCH_BIN)
