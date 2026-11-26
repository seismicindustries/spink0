        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #######

cmake_minimum_required(VERSION 3.18) # Ableton Link 4.1
project(pink CXX)

set(PINK_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR})
set(PINK_BUILD_DIR ${CMAKE_BINARY_DIR})
set(PINK_MODULES_DIR ${PINK_ROOT_DIR}/modules)

option(USE_PI_ZERO "Use the spink0 front panel (display, encoder, buttons, LEDs) via libgpiod" ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17) # required by Ableton Link 4
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake-modules")
include(colors)

# Version ---------------------------------------------------------------------------------------- #

string(TIMESTAMP PINK_BUILD_DATE "%Y-%m-%d")

set(PINK_VERSION_MAJOR 0)
set(PINK_VERSION_MINOR 2)
set(PINK_VERSION_MICRO 0)
set(PINK_VERSION_STRING "${PINK_VERSION_MAJOR}.${PINK_VERSION_MINOR}.${PINK_VERSION_MICRO}")

execute_process(
  COMMAND git describe --always --dirty
  WORKING_DIRECTORY ${PINK_ROOT_DIR}
  OUTPUT_VARIABLE PINK_GIT_REVISION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

configure_file(
  "${PINK_ROOT_DIR}/support/config/pink-config.h.in"
  "${PROJECT_BINARY_DIR}/pink-config.h"
)

set(PINK_BUILD_DESCRIPTION "pink v. ${PINK_VERSION_STRING} (${PINK_GIT_REVISION}), ${CMAKE_BUILD_TYPE}")

MESSAGE( STATUS "            ")
MESSAGE( STATUS "            ${Magenta}             88             ${White}  88")
MESSAGE( STATUS "            ${Magenta}             \"\"             ${White}  88")
MESSAGE( STATUS "            ${Magenta}                              ${White}88             ${ColourReset}")
MESSAGE( STATUS "            ${Magenta}8b,dPPYba,   88${White}  8b,dPPYba,   88   ,d8")
MESSAGE( STATUS "            ${Magenta}88P'    \"8a  88${White}  88P'   `\"8a  88 ,a8\"")
MESSAGE( STATUS "            ${Magenta}88       d8  88${White}  88       88  8888[")
MESSAGE( STATUS "            ${Magenta}88b,   ,a8\"  88${White}  88       88  88`\"Yba,")
MESSAGE( STATUS "            ${Magenta}88`YbbdP\"'   88${White}  88       88  88   `Y8a")
MESSAGE( STATUS "            ${Magenta}88             ${White}")
MESSAGE( STATUS "            ${Magenta}88             ${ColourReset}")
MESSAGE( STATUS "            ")

MESSAGE( STATUS "            Ableton Link for Raspberry Pi - v. ${PINK_VERSION_STRING}")
MESSAGE( STATUS "                                               ")
message( STATUS "[${PINK_BUILD_DATE}] ${PINK_BUILD_DESCRIPTION}")

# Dependencies ----------------------------------------------------------------------------------- #

include(${PINK_MODULES_DIR}/link/AbletonLinkConfig.cmake)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO REQUIRED IMPORTED_TARGET portaudio-2.0)
find_package(Threads REQUIRED)

if(USE_PI_ZERO AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  pkg_check_modules(GPIOD REQUIRED IMPORTED_TARGET libgpiod>=2.0)
  set(PINK_USE_GPIOD ON)
endif()

# Sources ---------------------------------------------------------------------------------------- #

set(PINK_SRCS
  src/Audio.cpp
  src/Audio.h
  src/Engine.cpp
  src/Engine.h
  src/MidiOut.cpp
  src/MidiOut.h
  src/Pink.cpp
  src/Pink.h
  src/StatusFile.cpp
  src/StatusFile.h
  src/main.cpp
  src/ui/UserInterface.h
)

if(USE_PI_ZERO)
  message("   spink0 front panel enabled")
  list(APPEND PINK_SRCS
    src/ui/Font7Seg.h
    src/ui/Gpio.cpp
    src/ui/Gpio.h
    src/ui/NetworkControl.cpp
    src/ui/NetworkControl.h
    src/ui/UserInterfacePiZero.cpp
    src/ui/UserInterfacePiZero.h
  )
else()
  list(APPEND PINK_SRCS src/ui/UserInterfaceNone.h)
endif()

add_executable(pink ${PINK_SRCS})
set_target_properties(pink PROPERTIES OUTPUT_NAME pinkd)
target_include_directories(pink PRIVATE src ${PINK_BUILD_DIR})
target_compile_options(pink PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(pink PRIVATE Ableton::Link PkgConfig::PORTAUDIO Threads::Threads)

if(USE_PI_ZERO)
  target_compile_definitions(pink PRIVATE UI_USE_RASPBERRY_PI_ZERO)
endif()
if(PINK_USE_GPIOD)
  target_compile_definitions(pink PRIVATE PINK_USE_GPIOD)
  target_link_libraries(pink PRIVATE PkgConfig::GPIOD)
endif()

# Install ---------------------------------------------------------------------------------------- #
# The binary only; system integration (systemd units, network daemon, setup page, config.txt) is
# done by install.sh.

install(TARGETS pink DESTINATION bin)
