# =============================================================================
# tests/integration/
# Cross-layer / end-to-end tests.  These are slower than unit tests and
# are run before pushing, not on every save.
#
# Build targets:
#   pdcp_loopback_test     — single entity acts as both TX and RX
#
# Future targets (when RLC arrives):
#   pdcp_rlc_integration_test
#
# Meta target:
#   integration_tests      — builds all integration targets
#
# Usage:
#   cmake --build build --target integration_tests
#   ctest --test-dir build -L integration --output-on-failure
# =============================================================================

lte_add_unit_test(
    NAME    pdcp_loopback_test
    SOURCES pdcp_loopback_test.cpp
    LIBS    lte_pdcp lte_common test_fixtures
    LABELS  "integration;pdcp"
)

add_custom_target(integration_tests
    DEPENDS
        pdcp_loopback_test
)
