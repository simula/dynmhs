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

#ifndef LOGGER_H
#define LOGGER_H

#include <boost/log/trivial.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/global_logger_storage.hpp>


BOOST_LOG_GLOBAL_LOGGER(MyLogger, boost::log::sources::severity_logger_mt<boost::log::trivial::severity_level>)

#define DMHS_LOG(_severity_) BOOST_LOG_SEV(MyLogger::get(), boost::log::trivial::_severity_)


void initialiseLogger(const unsigned int logLevel = 0,
                      const bool         logColor = true,
                      const char*        logFile  = nullptr);


#endif
