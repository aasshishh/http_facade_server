# Add the test executable
add_executable(backendify_test
    test_main.cpp
    test_utils.cpp
    test_inmemorycache.cpp
    test_backendify.cpp
    ${PROJECT_SOURCE_DIR}/src/cache/InMemoryCache.cpp
    ${PROJECT_SOURCE_DIR}/src/cache/RedisCache.cpp
    ${PROJECT_SOURCE_DIR}/src/core/Backendify.cpp
    ${PROJECT_SOURCE_DIR}/src/core/CircuitBreaker.cpp
    ${PROJECT_SOURCE_DIR}/src/core/ThreadLocalTime.cpp
    ${PROJECT_SOURCE_DIR}/src/metrics/DummyStatsDClient.cpp
    ${PROJECT_SOURCE_DIR}/src/metrics/StatsDClient.cpp
    ${PROJECT_SOURCE_DIR}/src/logging/ConsoleLogger.cpp
)

# Find required packages
find_package(GTest CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(hiredis CONFIG REQUIRED)
find_package(Threads REQUIRED)

# Set compile options for MSVC
if(MSVC)
    target_compile_options(backendify_test PRIVATE 
        /W4
        /EHsc
        /D_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING
    )
endif()

# Include directories
target_include_directories(backendify_test PUBLIC 
    ${PROJECT_SOURCE_DIR}/src
    ${GTEST_INCLUDE_DIRS}
)

# Link libraries in correct order
target_link_libraries(backendify_test PRIVATE 
    GTest::gtest
    GTest::gmock
    GTest::gtest_main
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
    nlohmann_json::nlohmann_json
    httplib::httplib
    hiredis::hiredis
    Threads::Threads
    $<$<CXX_COMPILER_ID:MSVC>:ws2_32>
)

# Copy runtime dependencies
if(WIN32)
    add_custom_command(TARGET backendify_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:backendify_test>
            $<TARGET_FILE_DIR:backendify_test>
        COMMAND_EXPAND_LISTS
    )
endif()

# Set C++ standard
set_target_properties(backendify_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)

# Add test discovery
include(GoogleTest)
gtest_discover_tests(backendify_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    PROPERTIES
        LABELS "backendify"
        TIMEOUT 10
    DISCOVERY_TIMEOUT 60
)
