#! /bin/bash

rm -rf /tmp/rocksdb-compaction
mkdir -p /tmp/rocksdb-compaction

~/workspace/CS-525/rocksdb/db_bench \
  --db=/tmp/rocksdb-compaction \
  --benchmarks=fillrandom \
  --num=2000000 \
  --value_size=1024 \
  --threads=2 \
  --compression_type=none \
  --write_buffer_size=33554432 \
  --max_write_buffer_number=2 \
  --level0_file_num_compaction_trigger=4 \
  --max_background_jobs=2 \
  --statistics=1 \
  --stats_interval_seconds=5 \
  2>&1 | tee ~/workspace/CS-525/logs/fillrandom-compaction.log