cmake_minimum_required(VERSION 3.16)
project(ClearCtrl LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ROCKSDB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../rocksdb" CACHE PATH "RocksDB source tree (make static_lib)")

find_package(Threads REQUIRED)

add_executable(clearctrl_demo main.cc)
target_include_directories(clearctrl_demo PRIVATE "${ROCKSDB_ROOT}/include")
target_link_libraries(clearctrl_demo PRIVATE
  "${ROCKSDB_ROOT}/librocksdb.a"
  Threads::Threads
  rt
  dl
  gflags
  z
)
