cmake_minimum_required(VERSION 3.10)
project(backendify)

# Enable verbose output
set(CMAKE_VERBOSE_MAKEFILE ON)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option to enable ThreadSanitizer (for GCC/Clang)
# Default OFF
option(ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer for detecting data races" OFF)

if(ENABLE_THREAD_SANITIZER)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(STATUS "ThreadSanitizer enabled. Adding -fsanitize=thread and -g flags.")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -g")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=thread") # If you ever build shared libs
    else()
        message(WARNING "ENABLE_THREAD_SANITIZER is ON, but the current compiler (${CMAKE_CXX_COMPILER_ID}) might not support -fsanitize=thread. This flag is primarily for GCC and Clang.")
    endif()
endif()

# Set VCPKG_INSTALLED_DIR variable
if(WIN32)
    set(VCPKG_INSTALLED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg_installed/x64-windows")
else()
    set(VCPKG_INSTALLED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg_installed/x64-linux")
endif()

message(STATUS "VCPKG_INSTALLED_DIR: ${VCPKG_INSTALLED_DIR}")

# Include vcpkg
include(${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake)

# Set package directories
set(GTest_DIR "${VCPKG_INSTALLED_DIR}/share/GTest")
set(httplib_DIR "${VCPKG_INSTALLED_DIR}/share/httplib")
set(nlohmann_json_DIR "${VCPKG_INSTALLED_DIR}/share/nlohmann_json")
set(OPENSSL_ROOT_DIR "${VCPKG_INSTALLED_DIR}")

message(STATUS "Package directories:")
message(STATUS "  GTest_DIR: ${GTest_DIR}")
message(STATUS "  httplib_DIR: ${httplib_DIR}")
message(STATUS "  nlohmann_json_DIR: ${nlohmann_json_DIR}")

# Find packages
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
# Find hiredis using vcpkg
find_package(hiredis CONFIG REQUIRED)

message(STATUS "Found packages:")
message(STATUS "  GTest: ${GTest_FOUND}")
message(STATUS "  httplib: ${httplib_FOUND}")
message(STATUS "  nlohmann_json: ${nlohmann_json_FOUND}")
message(STATUS "  hiredis: ${HIREDIS_FOUND}")

# Add source files
set(SOURCES
    src/main.cpp
    src/cache/InMemoryCache.cpp
    src/cache/RedisCache.cpp
    src/core/Backendify.cpp
    src/core/CircuitBreaker.cpp
    src/core/ThreadLocalTime.cpp
    src/metrics/StatsDClient.cpp
)

# Add header files
set(HEADERS
    src/cache/InMemoryCache.hpp
    src/cache/RedisCache.hpp
    src/config/AppConfig.hpp
    src/core/Backendify.hpp
    src/core/CircuitBreaker.hpp
    src/core/ThreadLocalTime.hpp
    src/core/ThreadPoolQueue.hpp
    src/interfaces/CacheInterface.hpp
    src/logging/ConsoleLogger.hpp
    src/models/CompanyInfo.hpp
    src/metrics/DummyStatsDClient.hpp
    src/metrics/StatsDClient.hpp
    src/models/BackendUrlInfo.hpp
    src/third_party/UDPSender.hpp
    src/utils/Utils.hpp
)

# Verify source files exist
foreach(src ${SOURCES})
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
        message(FATAL_ERROR "Source file not found: ${CMAKE_CURRENT_SOURCE_DIR}/${src}")
    endif()
endforeach()

# Verify header files exist
foreach(header ${HEADERS})
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${header}")
        message(FATAL_ERROR "Header file not found: ${CMAKE_CURRENT_SOURCE_DIR}/${header}")
    endif()
endforeach()

# Add library
add_library(backendify_lib
    src/cache/InMemoryCache.cpp    
    src/cache/RedisCache.cpp
    src/core/Backendify.cpp
    src/core/CircuitBreaker.cpp
    src/core/ThreadLocalTime.cpp
    src/metrics/DummyStatsDClient.cpp
    src/metrics/StatsDClient.cpp
    src/logging/ConsoleLogger.cpp
    # ...other files...
)

# Add include directories
target_include_directories(backendify_lib PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include
)

target_link_libraries(backendify_lib PRIVATE
    nlohmann_json::nlohmann_json
    # ...existing libraries...
)

# Add executable
add_executable(backendify 
    src/main.cpp
)

# Include directories
target_include_directories(backendify PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${VCPKG_INSTALLED_DIR}/include
)

# Link libraries
target_link_libraries(backendify PRIVATE
    backendify_lib
    hiredis::hiredis
    nlohmann_json::nlohmann_json
    httplib::httplib
    Threads::Threads
)

# Copy configuration file to build directory
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/backendify.config 
               ${CMAKE_CURRENT_BINARY_DIR}/backendify.config COPYONLY)

# For tests, also copy to test directory
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/backendify.config 
               ${CMAKE_CURRENT_BINARY_DIR}/tests/backendify.config COPYONLY)

# Add tests
enable_testing()
add_subdirectory(tests)

# Print final configuration
message(STATUS "Final configuration:")
message(STATUS "  CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")
message(STATUS "  CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS}")
message(STATUS "  CMAKE_EXE_LINKER_FLAGS: ${CMAKE_EXE_LINKER_FLAGS}")
message(STATUS "  CMAKE_MODULE_PATH: ${CMAKE_MODULE_PATH}")
message(STATUS "  CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")
message(STATUS "  CMAKE_INCLUDE_PATH: ${CMAKE_INCLUDE_PATH}")
message(STATUS "  CMAKE_LIBRARY_PATH: ${CMAKE_LIBRARY_PATH}")

# Print all variables for debugging
get_cmake_property(_variableNames VARIABLES)
foreach(_variableName ${_variableNames})
    message(STATUS "${_variableName}=${${_variableName}}")
endforeach()
