cmake_minimum_required(VERSION 3.20)
project(lte_stack VERSION 0.2.0 LANGUAGES CXX)

# ============================================================
# C++ standard — require C++17
# ============================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================
# Build type default
# ============================================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

find_package(Threads REQUIRED)

include(FetchContent)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK             ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST           OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    googletest
    SOURCE_DIR "D:/googletest-1.14.0" 
)

FetchContent_MakeAvailable(googletest)

add_subdirectory(src)

# ============================================================
# Test executable
# ============================================================
enable_testing()
add_subdirectory(tests)

# ============================================================
# Summary
# ============================================================
message(STATUS "")
message(STATUS "=== LTE Protocol Stack ===")
message(STATUS "  Compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Binary dir  :  ${CMAKE_BINARY_DIR}")
message(STATUS "")