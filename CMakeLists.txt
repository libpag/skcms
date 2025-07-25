cmake_minimum_required(VERSION 3.5)
project(skcms)

set(CMAKE_CXX_STANDARD 14)

file(GLOB_RECURSE SKCMS_SOURCES
        skcms.cc
        skcms.h
        src/skcms_internals.h
        src/skcms_public.h
        src/skcms_TransformBaseline.cc
        src/skcms_Transform.h
        src/Transform_inl.h
)

file(GLOB_RECURSE SKCMS_TRANSFORM_HSW
        src/skcms_Transform.h
        src/skcms_TransformHsw.cc
        src/skcms_internals.h
        src/skcms_public.h
)

file(GLOB_RECURSE SKCMS_TRANSFORM_SKX
        src/skcms_Transform.h
        src/skcms_TransformSkx.cc
        src/skcms_internals.h
        src/skcms_public.h
)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64" AND NOT ANDROID)
    add_library(skcms_transform_hsw STATIC ${SKCMS_TRANSFORM_HSW})
    add_library(skcms_transform_skx STATIC ${SKCMS_TRANSFORM_SKX})

    set_target_properties(skcms_transform_hsw PROPERTIES LINKER_LANGUAGE CXX)
    set_target_properties(skcms_transform_skx PROPERTIES LINKER_LANGUAGE CXX)

    if(WIN32)
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            target_compile_options(skcms_transform_hsw PRIVATE -mavx2 -mf16c -ffp-contract=off)
            target_compile_options(skcms_transform_skx PRIVATE -mavx512f -mavx512dq -mavx512cd -mavx512bw -mavx512vl -ffp-contract=off)
        else()
            target_compile_options(skcms_transform_hsw PRIVATE /arch:AVX2)
            target_compile_options(skcms_transform_skx PRIVATE /arch:AVX512)
        endif()
    else()
        target_compile_options(skcms_transform_hsw PRIVATE -mavx2 -mf16c)
        target_compile_options(skcms_transform_skx PRIVATE -mavx512f -mavx512dq -mavx512cd -mavx512bw -mavx512vl)
    endif()
    add_library(skcms ${SKCMS_SOURCES} $<TARGET_OBJECTS:skcms_transform_hsw> $<TARGET_OBJECTS:skcms_transform_skx>)
else()
    add_library(skcms STATIC ${SKCMS_SOURCES})
    target_compile_definitions(skcms PRIVATE SKCMS_DISABLE_HSW SKCMS_DISABLE_SKX)
endif()
