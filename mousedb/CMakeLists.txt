cmake_minimum_required(VERSION 3.31)

project(lib_database LANGUAGES CXX)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "Clang is required.")
endif()

if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 20.1)
  message(FATAL_ERROR "Clang 20.1 or higher is required.")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Boost CONFIG REQUIRED COMPONENTS dynamic_bitset)
find_package(GTest CONFIG REQUIRED)
find_package(benchmark CONFIG REQUIRED)

enable_testing()

include(GNUInstallDirs)

function(set_target_cxx_properties_ target)
  set_target_properties(
    ${target}
    PROPERTIES CXX_EXTENSIONS ON
               CXX_STANDARD 23
               CXX_STANDARD_REQUIRED ON)
endfunction()

function(target_compile_and_link_options_ target build_type)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)

  if(build_type STREQUAL "Debug")
    target_compile_options(${target} PRIVATE -g -O0)
    target_link_options(
      ${target} PRIVATE -fsanitize=address,memory,thread,undefined
      -fno-omit-frame-pointer)
  elseif(build_type STREQUAL "Release")
    target_compile_options(${target} PRIVATE -DNDEBUG -O3 -Werror)
    target_link_options(${target} PRIVATE -flto)
  else()
    message(
      FATAL_ERROR
        "target_compile_and_link_options_: unknown build_type '${build_type}' for target '${target}'"
    )
  endif()
endfunction()

set(LIB_DATABASE_SRC src/arena.cpp src/filter.cpp src/memtable.cpp
                     src/random.cpp)
add_library(lib_database ${LIB_DATABASE_SRC})
set_target_properties(lib_database PROPERTIES EXPORT_NAME database OUTPUT_NAME
                                                                   database)
set_target_cxx_properties_(lib_database)
target_compile_and_link_options_(lib_database ${CMAKE_BUILD_TYPE})
target_include_directories(
  lib_database
  PRIVATE src
  PUBLIC include)
target_link_libraries(lib_database PRIVATE Boost::dynamic_bitset)

file(GLOB_RECURSE LIB_DATABASE_TEST_SRC CONFIGURE_DEPENDS *.test.cpp)
if(LIB_DATABASE_TEST_SRC)
  add_executable(lib_database_test ${LIB_DATABASE_TEST_SRC})
  set_target_properties(lib_database_test PROPERTIES OUTPUT_NAME exe/test)
  set_target_cxx_properties_(lib_database_test)
  target_compile_and_link_options_(lib_database_test ${CMAKE_BUILD_TYPE})
  target_include_directories(lib_database_test PRIVATE src)
  target_link_libraries(lib_database_test PRIVATE GTest::gtest_main
                                                  lib_database)

  include(GoogleTest)
  gtest_discover_tests(lib_database_test)
endif()

file(GLOB_RECURSE LIB_DATABASE_BENCH_SRC CONFIGURE_DEPENDS *.bench.cpp)
if(LIB_DATABASE_BENCH_SRC)
  add_executable(lib_database_bench ${LIB_DATABASE_BENCH_SRC})
  set_target_properties(lib_database_bench PROPERTIES OUTPUT_NAME exe/bench)
  set_target_cxx_properties_(lib_database_bench)
  target_compile_and_link_options_(lib_database_bench Release)
  target_include_directories(lib_database_bench PRIVATE src)
  target_link_libraries(lib_database_bench PRIVATE benchmark::benchmark_main
                                                   lib_database)
endif()
