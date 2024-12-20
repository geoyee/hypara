cmake_minimum_required(VERSION 3.15 FATAL_ERROR)

set(CMAKE_CXX_STANDARD 17)

project(hypara LANGUAGES C CXX)

include(GNUInstallDirs)

if (PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "Please create a build directory and run CMake from there")
endif ()

string(COMPARE EQUAL ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR} IS_MAIN_PROJECT)
option(ENABLE_SAMPLE "Enable sample." ${IS_MAIN_PROJECT})

add_library(${PROJECT_NAME} INTERFACE)
target_include_directories(${PROJECT_NAME} INTERFACE $<BUILD_INTERFACE:${${PROJECT_NAME}_SOURCE_DIR}>
                                                     $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if (ENABLE_SAMPLE)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(LINK_WHAT_YOU_USE ON)

  if (MSVC)
    add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/W4>)
    add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/WX>)
    add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
  else ()
    add_compile_options($<$<CXX_COMPILER_ID:GNU,Clang>:-fPIC>)
    add_compile_options($<$<CXX_COMPILER_ID:GNU,Clang>:-Wall$<SEMICOLON>-Wextra>)
    add_compile_options($<$<CXX_COMPILER_ID:GNU,Clang>:-Werror>)
    add_compile_options(
      $<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-unknown-pragmas$<SEMICOLON>-Werror=uninitialized$<SEMICOLON>-Werror=return-type$<SEMICOLON>-Wconversion$<SEMICOLON>-Wsign-compare$<SEMICOLON>-Werror=unused-result$<SEMICOLON>-Werror=suggest-override$<SEMICOLON>-Wzero-as-null-pointer-constant$<SEMICOLON>-Wmissing-declarations$<SEMICOLON>-Wold-style-cast$<SEMICOLON>-Wnon-virtual-dtor>
      )
    add_link_options($<$<CXX_COMPILER_ID:GNU,Clang>:-flto>)
  endif ()

  set(SAMPLE_NAME ${PROJECT_NAME}-sample)
  add_executable(${SAMPLE_NAME} sample/main.cpp hypara.hpp)
  target_link_libraries(${SAMPLE_NAME} ${PROJECT_NAME})
endif ()

install(FILES "hypara.hpp" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
