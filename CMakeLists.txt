cmake_minimum_required(VERSION 3.12)
project(TradingBot)

# Suppress FindBoost deprecation warnings for CMake 4.x+
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Dependencies
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

# Manual Boost Setup: Bypassing broken BoostConfig.cmake in Boost 1.90/Ubuntu Resolute.
find_path(Boost_INCLUDE_DIRS NAMES boost/version.hpp PATHS /usr/include)
find_library(Boost_THREAD_LIB NAMES boost_thread PATHS /usr/lib/x86_64-linux-gnu)
find_library(Boost_ATOMIC_LIB NAMES boost_atomic PATHS /usr/lib/x86_64-linux-gnu)
find_library(Boost_CHRONO_LIB NAMES boost_chrono PATHS /usr/lib/x86_64-linux-gnu)

if(NOT Boost_INCLUDE_DIRS OR NOT Boost_THREAD_LIB OR NOT Boost_ATOMIC_LIB OR NOT Boost_CHRONO_LIB)
    message(FATAL_ERROR "Boost components not found. Ensure libboost-thread-dev, libboost-atomic-dev, and libboost-chrono-dev are installed.")
endif()

add_executable(trading_engine src/main.cpp)

# Setup headers and mandatory definitions for Boost 1.90 header-only System
target_include_directories(trading_engine PRIVATE include ${Boost_INCLUDE_DIRS})

# This definition is crucial: it prevents headers from trying to link to libboost_system.so
target_compile_definitions(trading_engine PRIVATE BOOST_SYSTEM_NO_LIB)

target_link_libraries(trading_engine PRIVATE 
    Threads::Threads 
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
    ${Boost_THREAD_LIB}
    ${Boost_ATOMIC_LIB}
    ${Boost_CHRONO_LIB}
)