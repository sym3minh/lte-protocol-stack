# =============================================================================
# AddUnitTest.cmake
# Helper function to register a test executable without boilerplate.
#
# Usage:
#   lte_add_unit_test(
#       NAME    <target_name>
#       SOURCES <file1.cpp> [file2.cpp ...]
#       LIBS    <lib1> [lib2 ...]        # production libs to link against
#       LABELS  <label1> [label2 ...]   # e.g. "unit;pdcp"
#   )
#
# What it does:
#   - Creates the executable
#   - Links GTest + GMock + test_fixtures + caller-supplied LIBS
#   - Calls gtest_discover_tests so CTest sees every TEST_F individually
#   - Applies standard warning flags
# =============================================================================

include(GoogleTest)

function(lte_add_unit_test)
    set(options "")
    set(oneValueArgs NAME)
    set(multiValueArgs SOURCES LIBS LABELS)
    cmake_parse_arguments(T "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT T_NAME)
        message(FATAL_ERROR "lte_add_unit_test: NAME is required")
    endif()
    if(NOT T_SOURCES)
        message(FATAL_ERROR "lte_add_unit_test: SOURCES is required")
    endif()

    add_executable(${T_NAME} ${T_SOURCES})

    target_link_libraries(${T_NAME}
        PRIVATE
            GTest::gtest
            GTest::gtest_main
            GTest::gmock
            ${T_LIBS}
    )

    gtest_discover_tests(${T_NAME}
        PROPERTIES
            LABELS  "${T_LABELS}"
            TIMEOUT 30
        DISCOVERY_TIMEOUT 60
        DISCOVERY_MODE PRE_TEST
    )
endfunction()
