#src/bearer
add_library(lte_bearer STATIC
    src/radio_bearer.cpp
)
target_include_directories(lte_bearer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lte_bearer PUBLIC lte_pdcp lte_rlc lte_mac lte_common)
