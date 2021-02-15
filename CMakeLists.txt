cmake_minimum_required(VERSION 3.5)

project(KALEIDOSCOPE LANGUAGES CXX)

if (MSVC)
  # Fix `__cplusplus` problem
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus")
endif()

# Generate parser
find_package(BISON REQUIRED)
BISON_TARGET(
  PARSER
  ${CMAKE_CURRENT_SOURCE_DIR}/KaleidoscopeParser.yy
  ${CMAKE_CURRENT_SOURCE_DIR}/KaleidoscopeParser.cc
  DEFINES_FILE ${CMAKE_CURRENT_SOURCE_DIR}/KaleidoscopeParser.h
  VERBOSE ${CMAKE_CURRENT_SOURCE_DIR}/KaleidoscopeParser.output
  )

# LLVM library
find_package(LLVM REQUIRED CONFIG)
llvm_map_components_to_libnames(LLVM_LIBRARIES core support scalaropts instcombine)

# Executable
add_executable(
  KALEIDOSCOPE
  ${CMAKE_CURRENT_SOURCE_DIR}/Kaleidoscope.h
  ${CMAKE_CURRENT_SOURCE_DIR}/Kaleidoscope.cc
  ${BISON_PARSER_OUTPUT_HEADER}
  ${BISON_PARSER_OUTPUT_SOURCE}
  )
set_target_properties(
  KALEIDOSCOPE
  PROPERTIES
  OUTPUT_NAME kal
  CXX_STANDARD 14
  )
target_include_directories(KALEIDOSCOPE PRIVATE ${LLVM_INCLUDE_DIRS})
target_link_libraries(KALEIDOSCOPE ${LLVM_LIBRARIES})