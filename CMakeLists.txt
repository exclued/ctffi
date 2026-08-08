cmake_minimum_required(VERSION 3.16)
project(ctffi C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CheckCCompilerFlag)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBFFI REQUIRED IMPORTED_TARGET libffi)

find_path(CTF_INCLUDE_DIR ctf-api.h REQUIRED)
find_library(CTF_LIBRARY NAMES ctf REQUIRED)

check_c_compiler_flag("-gctf" HAVE_GCTF)
if(NOT HAVE_GCTF)
    message(FATAL_ERROR "The compiler must support -gctf to build the CTF test fixture")
endif()

add_library(ctffi STATIC ctf_ffi_bridge_phase2.c)
target_include_directories(ctffi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CTF_INCLUDE_DIR})
target_link_libraries(ctffi PUBLIC PkgConfig::LIBFFI ${CTF_LIBRARY} ${CMAKE_DL_LIBS})
target_compile_options(ctffi PRIVATE -Wall -Wextra -Wpedantic)

add_library(test_ctf SHARED test_library.c)
target_compile_options(test_ctf PRIVATE -gctf)

add_library(test_ctf_phase2 SHARED test_library_phase2.c)
target_compile_options(test_ctf_phase2 PRIVATE -gctf)

add_library(test_ctf_phase3 SHARED test_library_phase3.c)
target_compile_options(test_ctf_phase3 PRIVATE -gctf)

# One diagnostic executable owns all Phase 1 integration tests. Keeping the
# test harness separate from the library makes every CTF/libffi operation
# visible in CI logs and permits manual-vs-automatic CIF comparisons.
add_executable(ctffi_debug_tests ctffi_debug_tests.c)
target_link_libraries(ctffi_debug_tests PRIVATE ctffi)
target_compile_options(ctffi_debug_tests PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ctffi_phase2_tests ctffi_phase2_tests.c)
target_link_libraries(ctffi_phase2_tests PRIVATE ctffi)
target_compile_options(ctffi_phase2_tests PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ctffi_phase3_tests ctffi_phase3_tests.c)
target_link_libraries(ctffi_phase3_tests PRIVATE ctffi)
target_compile_options(ctffi_phase3_tests PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME phase1_aggregates
             COMMAND ctffi_debug_tests $<TARGET_FILE:test_ctf>)
    add_test(NAME phase2_type_handling
             COMMAND ctffi_phase2_tests $<TARGET_FILE:test_ctf_phase2>)
    add_test(NAME phase3_arrays
             COMMAND ctffi_phase3_tests $<TARGET_FILE:test_ctf_phase3>)
endif()
