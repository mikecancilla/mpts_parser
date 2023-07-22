cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(mpts_parser VERSION 1.0)

#add_subdirectory(parsers)

#file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp parsers/*.cpp)
set(SRC_FILES main.cpp mpts_parser.cpp parsers/avc_parser.cpp parsers/mpeg2_parser.cpp)
file(GLOB_RECURSE H_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.h parsers/*.h)

add_executable(mpts_parser ${SRC_FILES} ${H_FILES})

# just for example add some compiler flags
target_compile_options(mpts_parser PUBLIC -g -std=c++17)

target_include_directories(mpts_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(mpts_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/parsers)