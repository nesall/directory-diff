cmake_minimum_required(VERSION 3.16)
project(directory-diff LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11) 

if(MSVC)
  message(STATUS "Using MSVC compiler")
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP /GS")
  if(debug)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /RTC1")
  endif()  
endif()

include(FetchContent)

FetchContent_Declare(
  utils_log
  GIT_REPOSITORY https://github.com/nesall/utils-log.git
  GIT_TAG main
)
FetchContent_MakeAvailable(utils_log)

add_library(utils_log INTERFACE)
target_include_directories(utils_log INTERFACE ${utils_log_SOURCE_DIR})

add_library(directory_diff_lib
  src/lib/filediff.cpp
  src/lib/dirdiff.cpp
  src/lib/xxhash.c
)

target_include_directories(directory_diff_lib
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty
)

target_link_libraries(directory_diff_lib
  PUBLIC utils_log
  PRIVATE Threads::Threads
)

target_compile_definitions(directory_diff_lib PRIVATE
  FMT_HEADER_ONLY
  FMT_USE_LOCALE=0
)

add_executable(directory_diff_cli
  src/cli/main.cpp
)

target_link_libraries(directory_diff_cli
  PRIVATE directory_diff_lib
)

if(WIN32)
  target_link_libraries(directory_diff_lib PRIVATE ws2_32 wsock32)
elseif(APPLE)
  find_library(COCOA Cocoa)
  find_library(COREFOUNDATION CoreFoundation REQUIRED)
  target_link_libraries(directory_diff_lib PRIVATE ${COCOA} ${COREFOUNDATION})
else()
  target_link_libraries(directory_diff_lib PRIVATE dl)
endif()

find_package(Threads REQUIRED)
