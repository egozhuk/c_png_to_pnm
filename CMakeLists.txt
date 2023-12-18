cmake_minimum_required(VERSION 3.24)
project(lab2 C)

set(CMAKE_C_STANDARD 23)

#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g")
#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -O0 -g")

include_directories(. /usr/local/include /Users/vladislavzukov/isa-l-master/ ${ZLIB_INCLUDE_DIRS})

add_executable(lab2 lab2.c)

target_link_libraries(lab2 z)

target_link_libraries(lab2 /opt/homebrew/lib/libdeflate.a ${ZLIB_LIBRARIES})

#find_path(LIBDEFLATE_INCLUDE_DIR NAMES include/igzip_lib.h)
#include_directories(${LIBDEFLATE_INCLUDE_DIR})