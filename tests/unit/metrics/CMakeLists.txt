# tests/unit/metrics/
lte_add_unit_test(
    NAME    metrics_test
    SOURCES metrics_test.cpp
    LIBS    lte_metrics
    LABELS  "unit;metrics"
)