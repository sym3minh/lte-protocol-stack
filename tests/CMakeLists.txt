# =============================================================================
# tests/
# Orchestration-only — does NOT define any target directly.
# All test targets are defined in sub-directories.
#
# Directory layout
# ----------------
# tests/
#   cmake/AddUnitTest.cmake   ← shared helper; included here once
#   fixtures/                 ← INTERFACE lib: test_fixtures
#   unit/
#     common/                 ← BufferPool, Metrics
#     pdcp/                   ← PDCP TX, RX AM/UM
#   integration/              ← cross-layer loopback etc.
#   benchmarks/               ← (future) throughput regression
#
# Quick-reference commands
# ------------------------
# Build & run all unit tests:
#   cmake --build build --target all_unit_tests
#   ctest --test-dir build -L unit -j8 --output-on-failure
#
# Build & run one module:
#   cmake --build build --target pdcp_unit_tests
#   ctest --test-dir build -L pdcp
#
# Build & run integration only:
#   cmake --build build --target integration_tests
#   ctest --test-dir build -L integration
#
# Run a single test case:
#   ctest --test-dir build -R "PdcpTx\.SequenceNumberStartsAtZero"
#
# Skip benchmarks:
#   ctest --test-dir build -LE benchmark
# =============================================================================

# Load helper once — all sub-directories inherit this function.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AddUnitTest.cmake)

# Shared test fixtures library must be defined before any test target.
add_subdirectory(fixtures)

# ---- Unit tests (fast, isolated, mock all deps) ----------------------------
add_subdirectory(unit/common)
add_subdirectory(unit/metrics)
add_subdirectory(unit/pdcp)
add_subdirectory(unit/rlc)

# ---- Integration tests (cross-layer, slower) --------------------------------
add_subdirectory(integration)

# ---- Top-level meta-targets -------------------------------------------------
add_custom_target(all_unit_tests
    DEPENDS
        common_unit_tests
        pdcp_unit_tests
        rlc_unit_tests
)

add_custom_target(all_tests
    DEPENDS
        all_unit_tests
        integration_tests
)
