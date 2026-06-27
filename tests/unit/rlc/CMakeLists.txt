lte_add_unit_test(
    NAME    rlc_pdu_test
    SOURCES rlc_pdu_test.cpp
    LIBS    lte_rlc lte_common
    LABELS "unit;rlc"
)

lte_add_unit_test(
    NAME    rlc_tm_test
    SOURCES rlc_tm_test.cpp
    LIBS    lte_rlc lte_common test_fixtures
    LABELS  "unit;rlc"
)

add_custom_target(rlc_unit_tests
    DEPENDS
        rlc_pdu_test
        rlc_tm_test
)