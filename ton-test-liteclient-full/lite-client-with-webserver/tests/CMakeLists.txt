cmake_minimum_required(VERSION 3.12)
project(httpserver-test)

set(CMAKE_CXX_STANDARD 14)

file(GLOB SRC *.cpp)

add_executable(${PROJECT_NAME} ${SRC})
target_link_libraries(${PROJECT_NAME} )