# CMakeLists.txt for ZooKeeper TDX Crypto JNI Library
cmake_minimum_required(VERSION 3.10)
project(zookeeper_tdx_crypto)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find JNI
find_package(JNI REQUIRED)
if(NOT JNI_FOUND)
    message(FATAL_ERROR "JNI not found")
endif()

# Include directories
include_directories(${JNI_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Find required libraries for TDX
# These might need to be adjusted based on your system
find_library(TDX_ATTEST_LIB tdx_attest)

# Find OpenSSL
find_package(OpenSSL REQUIRED)

# Source files - now using local files
set(SOURCES
    zookeeper_tdx_crypto_jni.cpp
    tdx_crypto_util.cpp
)

# Create shared library
add_library(zookeeper_tdx_crypto SHARED ${SOURCES})

# Link libraries
target_link_libraries(zookeeper_tdx_crypto 
    ${JNI_LIBRARIES}
    ${TDX_ATTEST_LIB}
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Compiler flags
target_compile_options(zookeeper_tdx_crypto PRIVATE
    -fPIC
    -Wall
    -Wextra
    -O2
)

# Include directories for the target
target_include_directories(zookeeper_tdx_crypto PRIVATE
    ${JNI_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Install target
install(TARGETS zookeeper_tdx_crypto
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# Print configuration info
message(STATUS "JNI Include Dirs: ${JNI_INCLUDE_DIRS}")
message(STATUS "JNI Libraries: ${JNI_LIBRARIES}")
message(STATUS "OpenSSL Version: ${OPENSSL_VERSION}")
message(STATUS "TDX Attest Library: ${TDX_ATTEST_LIB}")
