# =============================================================================
# tests/fixtures/
#
# INTERFACE (header-only) library that exposes shared test helpers to all
# test executables.  Linked via lte_add_unit_test() automatically.
#
# When RLC/MAC arrive, add mock .cpp files here:
#   target_sources(test_fixtures PRIVATE src/mock_rlc.cpp)
#   target_include_directories(test_fixtures INTERFACE include)
# =============================================================================

add_library(test_fixtures INTERFACE)

target_include_directories(test_fixtures
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# test_fixtures needs access to production headers to compile helper code
target_link_libraries(test_fixtures
    INTERFACE
        lte_pdcp
        lte_common
)
