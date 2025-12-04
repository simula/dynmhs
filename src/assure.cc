// ==========================================================================
//                    ____              __  __ _   _ ____
//                   |  _ \ _   _ _ __ |  \/  | | | / ___|
//                   | | | | | | | '_ \| |\/| | |_| \___ \
//                   | |_| | |_| | | | | |  | |  _  |___) |
//                   |____/ \__, |_| |_|_|  |_|_| |_|____/
//                          |___/
//
//                ---  Dynamic Multi-Homing Setup (DynMHS)  ---
//                     https://www.nntb.no/~dreibh/dynmhs/
// ==========================================================================
//
// Dynamic Multi-Homing Setup (DynMHS)
// Copyright (C) 2024-2026 by Thomas Dreibholz
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// Contact: dreibh@simula.no

#include "assure.h"

#include <cstring>
#include <iostream>


// ###### Print error message and abort #####################################
void __assure_fail(const char*  expression,
                   const char*  file,
                   unsigned int line,
                   const char*  function)
{
   std::cerr << "assure(" << expression << ") failed in "
             << function << " (" << file << ":" << line << ")!"
             << std::endl;
   abort();
}


// ###### Print error message and abort with error from errno ###############
void __assure_fail_perror(const char*  expression,
                          const char*  file,
                          unsigned int line,
                          const char*  function)
{
   std::cerr << "assure(" << expression << ") failed in "
             << function << " (" << file << ":" << line << "): "
             << strerror(errno)
             << std::endl;
   abort();
}
