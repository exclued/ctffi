cmake_minimum_required(VERSION 3.16)
project(ctffi C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CheckCCompilerFlag)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBFFI REQUIRED IMPORTED_TARGET libffi)

find_path(CTF_INCLUDE_DIR ctf-api.h)
find_library(CTF_LIBRARY NAMES ctf)

if(NOT CTF_INCLUDE_DIR OR NOT CTF_LIBRARY)
    message(FATAL_ERROR "libctf development files are required")
endif()

check_c_compiler_flag("-gctf" HAVE_GCTF)
if(NOT HAVE_GCTF)
    message(FATAL_ERROR "The compiler must support -gctf to build the CTF test library")
endif()

add_library(ctffi STATIC ctf_ffi_bridge_phase1.c)
target_include_directories(ctffi PRIVATE ${CTF_INCLUDE_DIR})
target_link_libraries(ctffi PRIVATE PkgConfig::LIBFFI ${CTF_LIBRARY} ${CMAKE_DL_LIBS})
target_compile_definitions(ctffi PRIVATE HAVE_LIBCTF)
target_compile_options(ctffi PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ctffi_test ctf_ffi_bridge.c)
target_compile_definitions(ctffi_test PRIVATE STANDALONE_TEST HAVE_LIBCTF)
target_include_directories(ctffi_test PRIVATE ${CTF_INCLUDE_DIR})
target_link_libraries(ctffi_test PRIVATE PkgConfig::LIBFFI ${CTF_LIBRARY} ${CMAKE_DL_LIBS})
target_compile_options(ctffi_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(phase1_test phase1_test.c)
target_link_libraries(phase1_test PRIVATE ctffi)
target_compile_options(phase1_test PRIVATE -Wall -Wextra -Wpedantic)

add_library(test_ctf SHARED test_library.c)
target_compile_options(test_ctf PRIVATE -gctf)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME smoke COMMAND ctffi_test $<TARGET_FILE:test_ctf> test_function_signature)
    add_test(NAME phase1_aggregates COMMAND phase1_test $<TARGET_FILE:test_ctf>)
endif()
