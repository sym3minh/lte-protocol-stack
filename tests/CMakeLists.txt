#tests
set(TEST_SOURCES
    pdcp_loopback_test.cpp
    pdcp_tx_test.cpp
    buffer_pool_test.cpp
    metrics_test.cpp
    pdcp_rx_am_noreorder_test.cpp
)
add_executable(lte_test ${TEST_SOURCES})
target_link_libraries(lte_test PRIVATE 
    lte_pdcp
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

# Register each test file as a CTest suite (so `ctest` can run them
include(GoogleTest)
gtest_discover_tests(lte_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    PROPERTIES        TIMEOUT 60
    DISCOVERY_TIMEOUT 60
    DISCOVERY_MODE    POST_BUILD      # POST_BUILD is required with MinGW
)