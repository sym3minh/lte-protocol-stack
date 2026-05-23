#src/pdcp
set(PDCP_SOURCES
    src/pdcp_entity.cpp
    src/pdcp_entity_rx_am.cpp
    src/pdcp_entity_rx_um.cpp
    src/pdcp_entity_rx_reorder.cpp
    src/pdcp_pdu.cpp
    src/pdcp_rohc.cpp
    src/pdcp_security.cpp
)
add_library(lte_pdcp STATIC ${PDCP_SOURCES})
target_include_directories(lte_pdcp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lte_pdcp PUBLIC lte_common lte_metrics)