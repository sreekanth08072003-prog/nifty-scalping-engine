cmake_minimum_required(VERSION 3.10)
project(AutoTradingApp)
set(CMAKE_CXX_STANDARD 17)

find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

include_directories(include)
add_executable(trading_app src/main.cpp)

target_link_libraries(trading_app 
    Boost::system 
    OpenSSL::SSL 
    OpenSSL::Crypto 
    CURL::libcurl
    oath
    Threads::Threads
)
