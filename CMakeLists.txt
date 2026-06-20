# ===========================================================================
#                    ____              __  __ _   _ ____
#                   |  _ \ _   _ _ __ |  \/  | | | / ___|
#                   | | | | | | | '_ \| |\/| | |_| \___ \
#                   | |_| | |_| | | | | |  | |  _  |___) |
#                   |____/ \__, |_| |_|_|  |_|_| |_|____/
#                          |___/
#
#                ---  Dynamic Multi-Homing Setup (DynMHS)  ---
#                     https://www.nntb.no/~dreibh/dynmhs/
# ===========================================================================
#
# Dynamic Multi-Homing Setup (DynMHS)
# Copyright (C) 2024-2026 by Thomas Dreibholz
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Contact: dreibh@simula.no

CMAKE_MINIMUM_REQUIRED(VERSION 3.10)
PROJECT(dynmhs LANGUAGES C CXX)

SET(BUILD_MAJOR "0")
SET(BUILD_MINOR "3")
SET(BUILD_PATCH "5~rc0")
SET(BUILD_VERSION ${BUILD_MAJOR}.${BUILD_MINOR}.${BUILD_PATCH})

SET(CMAKE_CXX_STANDARD 17)


#############################################################################
#### CMAKE INCLUDES                                                      ####
#############################################################################

INCLUDE(CheckCCompilerFlag)
INCLUDE(CheckCSourceCompiles)
INCLUDE(CheckCSourceRuns)
INCLUDE(CheckIncludeFile)
INCLUDE(CheckStructHasMember)
INCLUDE(GNUInstallDirs)


#############################################################################
#### PACKAGING                                                           ####
#############################################################################

SET(CPACK_SOURCE_GENERATOR "TXZ")
SET(CPACK_SOURCE_PACKAGE_FILE_NAME
    "${CMAKE_PROJECT_NAME}-${BUILD_MAJOR}.${BUILD_MINOR}.${BUILD_PATCH}")
SET(CPACK_SOURCE_IGNORE_FILES "\\\\.git;\\\\.swp$;~$;\\\\.\\\\#;/\\\\#")
LIST(APPEND CPACK_SOURCE_IGNORE_FILES "^${PROJECT_SOURCE_DIR}/${CMAKE_PROJECT_NAME}[_-]")
LIST(APPEND CPACK_SOURCE_IGNORE_FILES "\\\\.make$|\\\\.log$")
LIST(APPEND CPACK_SOURCE_IGNORE_FILES "/CMakeCache\\\\.txt$")
LIST(APPEND CPACK_SOURCE_IGNORE_FILES "/(CMakeFiles|CMakeScripts|_CPack_Packages)/")
LIST(APPEND CPACK_SOURCE_IGNORE_FILES "/package-version\\\\.h$")
LIST(APPEND CPACK_SOURCE_IGNORE_FILES "/packaging\\\\.conf$")
LIST(APPEND CPACK_SOURCE_IGNORE_FILES "^${PROJECT_SOURCE_DIR}/(po.*/|src.*/|)Makefile$")
LIST(APPEND CPACK_SOURCE_IGNORE_FILES "^${PROJECT_SOURCE_DIR}/(po.*|src.*|[^/]*)\\\\.cmake$")
INCLUDE(CPack)

ADD_CUSTOM_TARGET(dist COMMAND ${CMAKE_MAKE_PROGRAM} clean package_source)


#############################################################################
#### UNINSTALL                                                           ####
#############################################################################

IF(NOT TARGET uninstall)
   CONFIGURE_FILE(
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
      "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
      IMMEDIATE @ONLY)

   ADD_CUSTOM_TARGET(uninstall
      COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
ENDIF()


#############################################################################
#### OS DEPENDENT                                                        ####
#############################################################################

IF (CMAKE_SYSTEM_NAME MATCHES "Linux")
   MESSAGE(STATUS "${CMAKE_SYSTEM_NAME} supported")
ELSE()
   MESSAGE(FATAL_ERROR "${CMAKE_SYSTEM_NAME} not supported (yet?)")
ENDIF()


#############################################################################
#### REQUIREMENTS                                                        ####
#############################################################################

# ====== Boost ==============================================================
IF(CMAKE_VERSION VERSION_GREATER_EQUAL "3.30")
   # Use BoostConfig.cmake to find BOOST libraries, if possible.
   CMAKE_POLICY(SET CMP0167 NEW)
ENDIF()
FIND_PACKAGE(Boost COMPONENTS log program_options thread)
IF (Boost_FOUND)
   MESSAGE(STATUS "BOOST libraries found:")
   MESSAGE(STATUS " Boost_VERSION:      ${Boost_VERSION}")
   MESSAGE(STATUS " Boost_LIBRARY_DIRS: ${Boost_LIBRARY_DIRS}")
   MESSAGE(STATUS " Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")
   MESSAGE(STATUS " Boost_LIBRARIES:    ${Boost_LIBRARIES}")
   FIND_PACKAGE(Boost REQUIRED COMPONENTS log program_options thread)
ELSE()
   MESSAGE(FATAL_ERROR "Cannot find BOOST libraries! Try:\n"
           " * Ubuntu/Debian: sudo apt install -y libboost-all-dev\n"
           " * Fedora:        sudo dnf install -y boost-devel\n"
           " * OpenSUSE:      sudo zypper install -y boost-devel\n"
           " * Alpine:        sudo apk add boost-dev\n"
           " * FreeBSD:       sudo pkg install -y boost\n"
           " * NetBSD:        sudo pkgin -y install boost\n"
           " * OpenBSD:       sudo pkg_add boost\n"
           " * Apple:         brew install boost")
ENDIF()


#############################################################################
#### SUBDIRECTORIES                                                      ####
#############################################################################

ADD_SUBDIRECTORY(src)
