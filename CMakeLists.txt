cmake_minimum_required(VERSION 3.0)
project(sbfs)

set(PROJECT_BINARY_DIR ${PROJECT_SOURCE_DIR}/build)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build/bin)

include_directories(
	${PROJECT_SOURCE_DIR}/include
)

add_compile_options(
    # -fsanitize=address,undefined
	-std=c++17
	-O2
	-g
    -Wall
    -Wextra
    -Wno-write-strings
    -Wno-sign-compare
    -Wno-unused-variable
    -Wno-unused-parameter
    -Werror
)

add_definitions(
    -DNDEBUG
    -DFUSE_USE_VERSION=31
    -DPATH_CACHE
    -DBLOCK_CACHE
)

link_libraries(
    # asan
    # ubsan
    glog
    gflags
    fuse3
    rocksdb
)

file(GLOB SBFS_SOURCES src/*.cpp)

add_executable(main ${SBFS_SOURCES})
add_executable(test_rocksdb test/test_rocksdb.cpp)
