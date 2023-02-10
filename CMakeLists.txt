cmake_minimum_required(VERSION 3.0)

project(offsetAllocator)

set(SOURCE_FILES
   offsetAllocator.cpp
   offsetAllocator.hpp
)

add_library(${PROJECT_NAME} ${SOURCE_FILES})
setup_target_libs(${PROJECT_NAME})
