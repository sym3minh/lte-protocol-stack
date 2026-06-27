#src/mac
add_library(lte_mac STATIC
    src/mac_stub.cpp        
)
target_include_directories(lte_mac PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lte_mac PUBLIC lte_rlc lte_common)