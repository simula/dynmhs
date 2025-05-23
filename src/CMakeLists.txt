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
# Copyright (C) 2024-2025 by Thomas Dreibholz
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

#############################################################################
#### VERSION FILE                                                        ####
#############################################################################

CONFIGURE_FILE (
   "${CMAKE_CURRENT_SOURCE_DIR}/package-version.h.in"
   "${CMAKE_CURRENT_BINARY_DIR}/package-version.h"
)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR})


#############################################################################
#### PROGRAMS                                                            ####
#############################################################################

ADD_EXECUTABLE(dynmhs dynmhs.cc
   assure.cc
   logger.cc
)
TARGET_LINK_LIBRARIES(dynmhs ${Boost_LIBRARIES})
INSTALL(TARGETS dynmhs         RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
INSTALL(FILES   dynmhs.1       DESTINATION         ${CMAKE_INSTALL_MANDIR}/man1)
INSTALL(FILES   dynmhs.service DESTINATION         ${CMAKE_INSTALL_PREFIX}/lib/systemd/system)
INSTALL(FILES   dynmhs.conf    DESTINATION         /etc/dynmhs)
INSTALL(FILES   dynmhs.bash-completion
        DESTINATION ${CMAKE_INSTALL_DATADIR}/bash-completion/completions
        RENAME      dynmhs)
