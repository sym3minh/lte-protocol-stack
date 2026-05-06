#src/common
add_library(lte_common STATIC src/buffer_pool.cpp)
target_include_directories(lte_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)