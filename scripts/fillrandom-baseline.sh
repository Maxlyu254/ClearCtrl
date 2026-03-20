#! /bin/bash

rm -rf /tmp/rocksdb-compaction
mkdir -p /tmp/rocksdb-compaction

~/workspace/CS-525/rocksdb/db_bench \
  --db=/tmp/rocksdb-baseline \
  --benchmarks=fillrandom \
  --num=100000 \
  --value_size=1024 \
  --threads=2 \
  --compression_type=none \
  --statistics=1 \
  --stats_interval_seconds=5 \
  2>&1 | tee ~/workspace/CS-525/logs/fillrandom-baseline.log