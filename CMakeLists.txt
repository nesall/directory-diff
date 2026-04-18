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


# logging
add_library(utils_log INTERFACE)
target_include_directories(utils_log INTERFACE ${utils_log_SOURCE_DIR})


# Create executable
add_executable(${PROJECT_NAME} 
  README.md
  include/dirdiff.h
  include/filediff.h
  src/main.cpp
  src/dirdiff.cpp
  src/filediff.cpp
  src/xxhash.c
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE 
  utils_log
)

# Include directories
target_include_directories(${PROJECT_NAME} 
  PRIVATE 
  include/
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
  FMT_HEADER_ONLY
  FMT_USE_LOCALE=0
)

# Enable threading (required by hnswlib)
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads )


# Platform-specific settings
if(WIN32)
  target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 wsock32)
endif()

if(APPLE)
  find_library(COCOA Cocoa)
  find_library(COREFOUNDATION CoreFoundation REQUIRED)
  target_link_libraries(${PROJECT_NAME} PRIVATE ${COCOA} ${COREFOUNDATION})
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(${PROJECT_NAME} PRIVATE dl)
endif()