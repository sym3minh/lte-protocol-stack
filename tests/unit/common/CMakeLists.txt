# =============================================================================
# tests/unit/common/
# Unit tests for src/common (BufferPool, MetricsCollector).
#
# Build targets:
#   buffer_pool_test     — BufferPool alloc/dealloc/thread-safety
#   metrics_test         — MetricsCollector counters and snapshot
#
# Meta target:
#   common_unit_tests    — builds all of the above
#
# Usage:
#   cmake --build build --target common_unit_tests
#   ctest --test-dir build -L common -j4
# =============================================================================

lte_add_unit_test(
    NAME    buffer_pool_test
    SOURCES buffer_pool_test.cpp
    LIBS    lte_common
    LABELS  "unit;common"
)

lte_add_unit_test(
    NAME    metrics_test
    SOURCES metrics_test.cpp
    LIBS    lte_metrics
    LABELS  "unit;common"
)

add_custom_target(common_unit_tests
    DEPENDS
        buffer_pool_test
        metrics_test
)
