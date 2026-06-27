add_library(lte_rlc STATIC
    src/rlc_pdu.cpp
    src/rlc_tm.cpp
)
target_include_directories(lte_rlc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lte_rlc PUBLIC lte_common lte_metrics)