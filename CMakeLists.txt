cmake_minimum_required(VERSION 3.10)

project( gcc_tutorials )

set(CMAKE_C_COMPILE "gcc")

set(SOURCE_FILE ${CMAKE_SOURCE_DIR}/example/gimple2rtl.c)

add_library(gimple2rtl OBJECT ${SOURCE_FILE})

target_compile_options(gimple2rtl PRIVATE -fdump-rtl-all -O2)
