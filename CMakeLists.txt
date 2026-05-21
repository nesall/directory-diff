cmake_minimum_required(VERSION 3.16)
project(directory-diff LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11) 

if(MSVC)
  message(STATUS "Using MSVC compiler")
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
  add_compile_options(/MP /GS /utf-8)
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /RTC1")
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
  "${CMAKE_CURRENT_SOURCE_DIR}/include/directory-diff/dirdiff.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/directory-diff/filediff.h"
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


## CLI target

add_executable(directory_diff_cli
  src/cli/main.cpp
)

target_link_libraries(directory_diff_cli
  PRIVATE directory_diff_lib
)


## GUI target

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_PREFIX_PATH "$ENV{QT6_DIR}")
find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui)

add_executable(directory_diff_gui
  ./Qt6.natvis
  src/gui/app.qrc
  src/gui/main.cpp
  src/gui/persistentwnd.h
  src/gui/qdirdiff.h
  src/gui/qdirdiff.cpp
  src/gui/qfilediff.h
  src/gui/qfilediff.cpp
  src/gui/qmainwindow.cpp
  src/gui/qmainwindow.h
  #src/gui/qtutils.h
  #src/gui/qtutils.cpp
)

#set(RESOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${TARGET_NAME}.rc")

target_link_libraries(directory_diff_gui PRIVATE
  Qt6::Widgets
  Qt6::Core
  Qt6::Gui
  directory_diff_lib
)

target_include_directories(directory_diff_gui
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/gui
)


## Platform-specific linking

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
