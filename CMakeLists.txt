cmake_minimum_required(VERSION 3.15 FATAL_ERROR)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(LINK_WHAT_YOU_USE ON)

project(hypara LANGUAGES C CXX)

include(GNUInstallDirs)

if (PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "Please create a build directory and run CMake from there")
endif ()

string(COMPARE EQUAL ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR} IS_MAIN_PROJECT)
option(HYPARA_ENABLE_SAMPLE "Enable sample of hypara." ${IS_MAIN_PROJECT})
option(HYPARA_ENABLE_TEST "Enable test of hypara." ${IS_MAIN_PROJECT})

add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                                                     $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if (${IS_MAIN_PROJECT})
  if (MSVC)
    target_compile_options(
      ${PROJECT_NAME}
      INTERFACE /utf-8
                /W4
                /EHsc
                /Zc:__cplusplus
                /Zc:preprocessor
                /Gy
                $<$<CONFIG:Debug>:/Od>
                $<$<CONFIG:Release>:/O2
                /GL>)
  
    target_link_options(
      ${PROJECT_NAME}
      INTERFACE
      $<$<CONFIG:Release>:
      /LTCG
      /OPT:REF
      /OPT:ICF
      >)
  else ()
    target_compile_options(
      ${PROJECT_NAME}
      INTERFACE -fPIC
                -Wall
                -Wextra
                -Wconversion
                -Wsign-compare
                -Werror=uninitialized
                -Werror=return-type
                -Werror=unused-result
                -Werror=suggest-override
                -Wzero-as-null-pointer-constant
                -Wmissing-declarations
                -Wold-style-cast
                -Wnon-virtual-dtor
                $<$<CONFIG:Debug>:-g>
                $<$<CONFIG:Release>:-g2
                -flto>)
  
    target_link_options(${PROJECT_NAME} INTERFACE $<$<CONFIG:Release>:-flto>)
  endif ()
endif ()

set_target_properties(${PROJECT_NAME} PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

if (HYPARA_ENABLE_SAMPLE)
  set(SAMPLE_NAME ${PROJECT_NAME}-sample)
  add_executable(${SAMPLE_NAME} sample/main.cpp)
  target_link_libraries(${SAMPLE_NAME} ${PROJECT_NAME})
  set_target_properties(${SAMPLE_NAME} PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif ()

if (HYPARA_ENABLE_TEST)
  include(FetchContent)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1)
  FetchContent_MakeAvailable(Catch2)

  set(TEST_NAME ${PROJECT_NAME}-test)
  add_executable(${TEST_NAME} test/main.cpp)
  target_link_libraries(${TEST_NAME} ${PROJECT_NAME} Catch2::Catch2WithMain)
  set_target_properties(${TEST_NAME} PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif ()

install(FILES "hypara.hpp" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
