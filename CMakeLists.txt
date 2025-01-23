cmake_minimum_required(VERSION 3.1)
project(skcms)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx512f")

file(GLOB_RECURSE SOURCE_FILES
        skcms.h
        skcms.cc
        src/*.*)

include_directories(skcms.h)
add_library(skcms STATIC ${SOURCE_FILES})
