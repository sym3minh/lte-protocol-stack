#src/common
add_library(lte_common STATIC 
            src/buffer_pool.cpp
            src/clock.cpp
            src/byte_buffer.cpp)
target_include_directories(lte_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)