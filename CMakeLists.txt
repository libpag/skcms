cmake_minimum_required(VERSION 3.1)
project(skcms)

set(CMAKE_CXX_STANDARD 14)

file(GLOB_RECURSE SOURCE_FILES
        skcms.h
        skcms.cc
        skcms_internal.h
        src/Transform_inl.h)

include_directories(skcms.h)
add_library(skcms STATIC ${SOURCE_FILES})
