#metrics
add_library(lte_metrics STATIC src/metrics_collector.cpp)
target_include_directories(lte_metrics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)