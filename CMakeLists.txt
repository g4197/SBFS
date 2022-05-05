cmake_minimum_required(VERSION 3.0)
project(sbfs)

set(PROJECT_BINARY_DIR ${PROJECT_SOURCE_DIR}/build)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build/bin)

include_directories(
	${PROJECT_SOURCE_DIR}/
)

add_compile_options(
	-std=c++17
	# -O2
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
    # -DNDEBUG
    -DFUSE_USE_VERSION=31
    # -DPATH_CACHE
)

link_libraries(
    glog
    gflags
    fuse3
)

file(GLOB SBFS_SOURCES *.cpp)

add_executable(main ${SBFS_SOURCES})