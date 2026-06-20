# =============================================================================
# tests/unit/pdcp/
# Unit tests for src/pdcp — each test file becomes its own executable so
# build and run are granular.
#
# Build targets:
#   pdcp_tx_test                  — PdcpEntity TX path (SN, header, callback)
#   pdcp_rx_am_noreorder_test     — §5.1.2.1.2 RX AM no-reorder procedure
#
# Meta target:
#   pdcp_unit_tests               — builds all of the above
#
# Usage:
#   cmake --build build --target pdcp_unit_tests
#   ctest --test-dir build -L pdcp -j4
#   ctest --test-dir build -R "PdcpTx\." --output-on-failure
# =============================================================================

lte_add_unit_test(
    NAME    pdcp_tx_test
    SOURCES pdcp_tx_test.cpp
    LIBS    lte_pdcp lte_common test_fixtures
    LABELS  "unit;pdcp"
)

lte_add_unit_test(
    NAME    pdcp_rx_am_noreorder_test
    SOURCES pdcp_rx_am_noreorder_test.cpp
    LIBS    lte_pdcp lte_common lte_metrics test_fixtures
    LABELS  "unit;pdcp"
)

lte_add_unit_test(
    NAME    pdcp_rx_um_noreorder_test
    SOURCES pdcp_rx_um_noreorder_test.cpp
    LIBS    lte_pdcp lte_common lte_metrics test_fixtures
    LABELS  "unit;pdcp"
)

add_custom_target(pdcp_unit_tests
    DEPENDS
        pdcp_tx_test
        pdcp_rx_am_noreorder_test
        pdcp_rx_um_noreorder_test
)
