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
// Copyright (C) 2024-2025 by Thomas Dreibholz
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

#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <queue>
#include <vector>
#include <boost/asio/ip/address.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <signal.h>
#include <sys/signalfd.h>
#include <linux/rtnetlink.h>
#include <linux/fib_rules.h>

#include "assure.h"
#include "logger.h"
#include "package-version.h"


static std::map<std::string, unsigned int>            InterfaceMap;
static std::queue<std::pair<const nlmsghdr*, size_t>> RequestQueue;
static unsigned int                                   SeqNumber = 0;


// ###### Arribute helper ###################################################
#define NLMSG_TAIL(message) \
           ((rtattr*)(((long)(message)) + (long)NLMSG_ALIGN((message)->nlmsg_len)))
static int addattr(nlmsghdr* message, const unsigned int maxlen,
                   const int type, const void* data, const unsigned int alen)
{
   int     len = RTA_LENGTH(alen);
   rtattr* rta;

   assure((unsigned int)NLMSG_ALIGN(message->nlmsg_len) + (unsigned int)RTA_ALIGN(len) <= maxlen);
   rta = NLMSG_TAIL(message);
   rta->rta_type = type;
   rta->rta_len = len;
   if(alen) {
      memcpy(RTA_DATA(rta), data, alen);
   }
   message->nlmsg_len = NLMSG_ALIGN(message->nlmsg_len) + RTA_ALIGN(len);
   return 0;
}


// ###### Handle error ######################################################
static void handleError(const nlmsghdr* message)
{
   const nlmsgerr* errormsg = (const nlmsgerr*)NLMSG_DATA(message);
   if(errormsg != nullptr) {
      // ====== Acknowledgement =============================================
      if(errormsg->error == 0) {
        DMHS_LOG(trace) << boost::format("ack for seqnum %u")
                              % errormsg->msg.nlmsg_seq;
      }
      // ====== Error =======================================================
      else {
        DMHS_LOG(warning) << boost::format("Netlink error %d (%s) for seqnum %u")
                                % errormsg->error
                                % strerror(-errormsg->error)
                                % errormsg->msg.nlmsg_seq;
      }
   }
}


// ###### Handle link change event ##########################################
static void handleLinkEvent(const nlmsghdr* message)
{
   const ifinfomsg* ifinfo = (const ifinfomsg*)NLMSG_DATA(message);
   int length = message->nlmsg_len - NLMSG_LENGTH(sizeof(*ifinfo));

   // ====== Parse attributes ===============================================
   const char* ifName = nullptr;
   for(const rtattr* rta = IFLA_RTA(ifinfo);
       RTA_OK(rta, length); rta = RTA_NEXT(rta, length)) {
      if(rta->rta_type == IFLA_IFNAME) {
         ifName = (const char*)RTA_DATA(rta);
      }
   }

   // ====== Show results ===================================================
   const char* eventName;
   switch(message->nlmsg_type) {
      case RTM_NEWLINK:
         eventName = "NEW";
       break;
      case RTM_DELLINK:
         eventName = "DELETE";
       break;
      default:
         eventName = "UNKNOWN";
       break;
   }
   DMHS_LOG(debug) << "Link event:"
                   << boost::format(" event=%s ifindex=%d ifname=%s")
                         % eventName
                         % ifinfo->ifi_index
                         % ((ifName != nullptr) ? ifName : "UNKNOWN?!");
}


// ###### Handle address change event ##########################################
static void handleAddressEvent(const nlmsghdr*      message)
{
   const ifaddrmsg* ifa       = (const ifaddrmsg*)NLMSG_DATA(message);
   const int        ifalength = message->nlmsg_len;

   // ====== Parse attributes ===============================================
   int                      ifIndex = ifa->ifa_index;
   char                     ifNameBuffer[IF_NAMESIZE];
   const char*              ifName;
   boost::asio::ip::address address;
   const char*              addressPtr  = nullptr;
   bool                     isLinkLocal = false;
   const unsigned int       prefixLength = ifa->ifa_prefixlen;
   int                      length = ifalength - NLMSG_LENGTH(sizeof(*ifa));
   for(const rtattr* rta = IFA_RTA(ifa); RTA_OK(rta, length); rta = RTA_NEXT(rta, length)) {
      switch(rta->rta_type) {
         case IFA_ADDRESS:
            if(ifa->ifa_family == AF_INET) {
               address    = boost::asio::ip::make_address_v4(*((boost::asio::ip::address_v4::bytes_type*)RTA_DATA(rta)));
               addressPtr = (const char*)RTA_DATA(rta);
            }
            else if(ifa->ifa_family == AF_INET6) {
               const boost::asio::ip::address_v6 a =
                  boost::asio::ip::make_address_v6(*((boost::asio::ip::address_v6::bytes_type*)RTA_DATA(rta)));
               if(a.is_link_local()) {
                  isLinkLocal = true;
               }
               address    = a;
               addressPtr = (const char*)RTA_DATA(rta);
            }
          break;
      }
   }
   ifName = if_indextoname(ifIndex, (char*)&ifNameBuffer);
   if(ifName == nullptr) {
      ifName = "UNKNOWN";
   }

   // ====== Show results ===================================================
   const char* eventName;
   switch(message->nlmsg_type) {
      case RTM_NEWADDR:
         eventName = "NEW";
       break;
      case RTM_DELADDR:
         eventName = "DELETE";
       break;
      default:
         eventName = "UNKNOWN";
       break;
   }
   DMHS_LOG(debug) << "Address event:"
                   << boost::format(" event=%s IF=%s (%d) address=%s")
                         % eventName
                         % ifName
                         % ifIndex
                         % address.to_string();

   if( (addressPtr != nullptr) &&
       (!isLinkLocal) &&
       ( (message->nlmsg_type == RTM_NEWADDR) ||
         (message->nlmsg_type == RTM_DELADDR) ) ) {

      // ====== Check whether an update in the custom table is necessary =======
      const auto found = InterfaceMap.find(ifName);
      if(found != InterfaceMap.end()) {
         const uint32_t customTable = found->second;
         DMHS_LOG(info) << "Update of rules for table " << customTable << " is necessary ...";

         // ------ Build RTM_NEWRULE/RTM_DELRULE request --------------------
         struct _request {
            nlmsghdr     header;
            fib_rule_hdr frh;
            char         buffer[1024];
         };
         _request* request = (_request*)new char[sizeof(_request)];
         assure(request != nullptr);

         request->header.nlmsg_len   = NLMSG_LENGTH(sizeof(request->frh));
         request->header.nlmsg_type  = (message->nlmsg_type == RTM_NEWADDR) ?
                                          RTM_NEWRULE : RTM_DELRULE;
         request->header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP | NLM_F_ACK;
         request->header.nlmsg_pid   = 0;  // This field is opaque to netlink.
         request->header.nlmsg_seq   = ++SeqNumber;
         request->frh.family         = ifa->ifa_family;
         request->frh.action         = FR_ACT_TO_TBL;
         request->frh.table          = RT_TABLE_UNSPEC;

         // ------ "from" parameter: address/prefix -------------------------
         assure( addattr(&request->header, sizeof(*request), FRA_SRC,
                         addressPtr, (ifa->ifa_family == AF_INET) ? 4 : 16) == 0 );
         request->frh.src_len = prefixLength;

         // ------ "priority" parameter -------------------------------------
         assure( addattr(&request->header, sizeof(*request), FRA_PRIORITY,
                         &customTable, sizeof(uint32_t)) == 0 );

         // ------ "lookup" parameter ---------------------------------------
         assure( addattr(&request->header, sizeof(*request), FRA_TABLE,
                         &customTable, sizeof(uint32_t)) == 0 );

         // ------ Enqueue message for sending it later ---------------------
         RequestQueue.push(std::pair<const nlmsghdr*, size_t>(
            &request->header, request->header.nlmsg_len));
         DMHS_LOG(trace) << "Request seqnum " << SeqNumber;
      }
   }
}


// ###### Handle route change event #########################################
static void handleRouteEvent(const nlmsghdr*      message)
{
   const int    messageLength = message->nlmsg_len;
   const rtmsg* rtm           = (const rtmsg*)NLMSG_DATA(message);
   const int    rtmLength     = message->nlmsg_len;

   // ====== Parse attributes ===============================================
   boost::asio::ip::address destination =
      (rtm->rtm_family == AF_INET) ?
         boost::asio::ip::address(boost::asio::ip::address_v4()) :
         boost::asio::ip::address(boost::asio::ip::address_v6());
   const unsigned int       destinationPrefixLength = rtm->rtm_dst_len;
   boost::asio::ip::address gateway;
   bool                     hasGateway = false;
   int*                     tablePtr   = nullptr;
   int                      metric     = -1;
   int                      oifIndex   = -1;
   char                     oifNameBuffer[IF_NAMESIZE];
   const char*              oifName;
   int                      length = rtmLength - NLMSG_LENGTH(sizeof(*rtm));
   for(const rtattr* rta = RTM_RTA(rtm); RTA_OK(rta, length); rta = RTA_NEXT(rta, length)) {
      switch(rta->rta_type) {
         case RTA_DST:
            if(rtm->rtm_family == AF_INET) {
               destination = boost::asio::ip::make_address_v4(*((boost::asio::ip::address_v4::bytes_type*)RTA_DATA(rta)));
            }
            else if(rtm->rtm_family == AF_INET6) {
               destination = boost::asio::ip::make_address_v6(*((boost::asio::ip::address_v6::bytes_type*)RTA_DATA(rta)));
            }
          break;
         case RTA_GATEWAY:
            if(rtm->rtm_family == AF_INET) {
               gateway = boost::asio::ip::make_address_v4(*((boost::asio::ip::address_v4::bytes_type*)RTA_DATA(rta)));
            }
            else if(rtm->rtm_family == AF_INET6) {
               gateway = boost::asio::ip::make_address_v6(*((boost::asio::ip::address_v6::bytes_type*)RTA_DATA(rta)));
            }
          break;
         case RTA_TABLE:
            tablePtr = (int*)RTA_DATA(rta);
          break;
         case RTA_METRICS:
            metric = *(int*)RTA_DATA(rta);
          break;
         case RTA_OIF:
            oifIndex = *(int*)RTA_DATA(rta);
            if(oifIndex >= 0) {
               oifName = if_indextoname(oifIndex, (char*)&oifNameBuffer);
               if(oifName == nullptr) {
                  oifName = "UNKNOWN";
               }
            }
          break;
      }
   }
   assure(tablePtr != nullptr);

   // ====== Only the "main" table is of interest here ======================
   if(*tablePtr == RT_TABLE_MAIN) {
      // ====== Show results ================================================
      const char* eventName;
      switch(message->nlmsg_type) {
         case RTM_NEWROUTE:
            eventName = "NEW";
         break;
         case RTM_DELROUTE:
            eventName = "DELETE";
         break;
         default:
            eventName = "UNKNOWN";
         break;
      }
      const char* scopeName;
      switch(rtm->rtm_scope) {
         case RT_SCOPE_UNIVERSE:
            scopeName = "universe";
          break;
         case RT_SCOPE_LINK:
            scopeName = "link";
          break;
         default:
            scopeName = "UNKNOWN";
         break;
      }
      DMHS_LOG(debug) << "Route event:"
                      << boost::format(" event=%s: T=%d D=%s scope=%s %s IF=%s (%d) %s")
                            % eventName
                            % *tablePtr
                            % (destination.to_string() + "/" +
                                  std::to_string(destinationPrefixLength))
                            % scopeName
                            % ((hasGateway == true) ? ("G=" + gateway.to_string()) : "G=---")
                            % oifName
                            % oifIndex
                            % ((metric >= 0) ? std::to_string(metric) : "");

      // ====== Check whether an update in the custom table is necessary ====
      const auto found = InterfaceMap.find(oifName);
      if(found != InterfaceMap.end()) {
         const unsigned int customTable = found->second;
         DMHS_LOG(info) << "Update of table " << customTable << " is necessary ...";

         // ------ Update ---------------------------------------------------
         *tablePtr = customTable;

         // ------ Copy the message and enqueue it for sending it later -----
         nlmsghdr* updateMessage = (nlmsghdr*)new char[messageLength];
         assure(updateMessage != nullptr);
         memcpy(updateMessage, message, messageLength);

         updateMessage->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
         updateMessage->nlmsg_seq   = ++SeqNumber;

         RequestQueue.push(std::pair<const nlmsghdr*, size_t>(
            updateMessage, messageLength));
         DMHS_LOG(trace) << "Request seqnum " << SeqNumber;
      }
   }
}


// ###### Send simple Netlink request #######################################
static bool sendSimpleNetlinkRequest(const int sd, const int type)
{
   struct {
     struct nlmsghdr header;
     struct rtgenmsg msg;
   } request { };
   request.header.nlmsg_len   = NLMSG_LENGTH(sizeof(request.msg));
   request.header.nlmsg_type  = type;
   request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP | NLM_F_ACK;
   request.header.nlmsg_pid   = 0;  // This field is opaque to netlink.
   request.header.nlmsg_seq   = ++SeqNumber;
   request.msg.rtgen_family   = AF_UNSPEC;

   struct sockaddr_nl sa;
   memset(&sa, 0, sizeof(sa));
   sa.nl_family = AF_NETLINK;

   struct iovec  iov { &request, request.header.nlmsg_len };
   struct msghdr msg { &sa, sizeof(sa), &iov, 1, nullptr, 0, 0 };
   if(sendmsg(sd, &msg, 0) < 0) {
      return false;
   }
   return true;
}


// ###### Send queued Netlink requests ######################################
static bool sendQueuedRequests(const int sd)
{
   while(!RequestQueue.empty()) {
      // ------ Send queued Netlink request ---------------------------------
      std::pair<const nlmsghdr*, size_t>& command = RequestQueue.front();
      const nlmsghdr* message       = command.first;
      const size_t    messageLength = command.second;
      sockaddr_nl sa;
      memset(&sa, 0, sizeof(sa));
      sa.nl_family = AF_NETLINK;
      const iovec  iov { (void*)message, messageLength };
      const msghdr msg { &sa, sizeof(sa), (iovec*)&iov, 1, nullptr, 0, 0 };
      if(sendmsg(sd, &msg, 0) < 0) {
         DMHS_LOG(error) << "sendmsg() failed: " << strerror(errno);
         return false;
      }

      // ------ Remove Netlink request from queue ---------------------------
      delete [] message;
      RequestQueue.pop();
   }
   return true;
}


// ###### Read Netlink message ##############################################
static bool receiveNetlinkMessages(const int  sd,
                                   const bool nonBlocking = false,
                                   const bool errorOnly   = false)
{
   nlmsghdr    buffer[65536 / sizeof(struct nlmsghdr)];
   iovec       iov { buffer, sizeof(buffer) };
   sockaddr_nl sa;
   msghdr      msg { &sa, sizeof(sa), &iov, 1, nullptr, 0, 0 };
   const int   flags = (nonBlocking == true) ? MSG_DONTWAIT : 0;
   int         length;

   while( (length = recvmsg(sd, &msg, flags)) > 0) {
      for(const nlmsghdr* header = (const nlmsghdr*)buffer;
          NLMSG_OK(header, length); header = NLMSG_NEXT(header, length)) {
         if( (errorOnly) && (header->nlmsg_type != NLMSG_ERROR) ) {
            continue;
         }
         switch(header->nlmsg_type) {
            case NLMSG_DONE:
               // The end of a multipart message
               if(nonBlocking) {
                  continue;
               }
               return true;
             break;
            case NLMSG_ERROR:
               if(header->nlmsg_len >= NLMSG_LENGTH(sizeof(nlmsgerr))) {
                  handleError(header);
               }
             break;
            case RTM_NEWLINK:
            case RTM_DELLINK:
               if(header->nlmsg_len >= NLMSG_LENGTH(sizeof(ifinfomsg))) {
                  handleLinkEvent(header);
               }
             break;
            case RTM_NEWADDR:
            case RTM_DELADDR:
               if(header->nlmsg_len >= NLMSG_LENGTH(sizeof(ifaddrmsg))) {
                  handleAddressEvent(header);
               }
             break;
            case RTM_NEWROUTE:
            case RTM_DELROUTE:
               if(header->nlmsg_len >= NLMSG_LENGTH(sizeof(rtmsg))) {
                  handleRouteEvent(header);
               }
             break;
            default:
               DMHS_LOG(warning) << "Received unexpected header type "
                                 << (int)header->nlmsg_type;
             break;
         }
      }
   }

   if( (length < 0) && (errno == EWOULDBLOCK) ) {
     return true;
   }
   return false;
}


// ###### Initialise DynMHS #################################################
bool initialiseDynMHS(int sd)
{
   // ====== Request and process links ======================================
   if(!sendSimpleNetlinkRequest(sd, RTM_GETLINK)) {
      DMHS_LOG(error) << "sendmsg(RTM_GETLINK) failed: " << strerror(errno);
      return false;
   }
   if(!receiveNetlinkMessages(sd)) {
      DMHS_LOG(error) << "recvmsg(RTM_GETLINK) failed: " << strerror(errno);
      return false;
   }

   // ====== Request and process addresses ==================================
   if(!sendSimpleNetlinkRequest(sd, RTM_GETADDR)) {
      DMHS_LOG(error) << "sendmsg(RTM_GETADDR) failed: " << strerror(errno);
      return false;
   }
   if(!receiveNetlinkMessages(sd)) {
      DMHS_LOG(error) << "recvmsg(RTM_GETADDR) failed: " << strerror(errno);
      return false;
   }

   // ====== Request and process routes =====================================
   if(!sendSimpleNetlinkRequest(sd, RTM_GETROUTE)) {
      DMHS_LOG(error) << "sendmsg(RTM_GETROUTE) failed: " << strerror(errno);
      return false;
   }
   if(!receiveNetlinkMessages(sd)) {
      DMHS_LOG(error) << "recvmsg(RTM_GETROUTE) failed: " << strerror(errno);
      return false;
   }
   return true;
}


// ###### Clean up DynMHS ###################################################
void cleanUpDynMHS(int sd)
{
   for(auto iterator = InterfaceMap.begin(); iterator != InterfaceMap.end(); iterator++) {
      const std::string& interfaceName = iterator->first;
      const unsigned int customTable   = iterator->second;

      // ====== Remove the rules leading to the custom table ================
      DMHS_LOG(info) << "Cleaning up table " << customTable << " ...";

      // ------ Build RTM_DELRULE request -----------------------------------
      for(unsigned int v = 0; v <= 1; v++) {   // Requests for IPv4 and IPv6
         struct _request {
            nlmsghdr     header;
            fib_rule_hdr frh;
            char         buffer[64];
         } request { };
         request.header.nlmsg_len   = NLMSG_LENGTH(sizeof(request.frh));
         request.header.nlmsg_type  = RTM_DELRULE;
         request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
         request.header.nlmsg_pid   = 0;  // This field is opaque to netlink.
         request.header.nlmsg_seq   = ++SeqNumber;
         request.frh.family         = (v == 0) ? AF_INET : AF_INET6;
         request.frh.action         = FR_ACT_TO_TBL;
         request.frh.table          = RT_TABLE_UNSPEC;

         // ------ "priority" parameter -------------------------------------
         assure( addattr(&request.header, sizeof(request), FRA_PRIORITY,
                         &customTable, sizeof(uint32_t)) == 0 );

         // ------ "lookup" parameter ---------------------------------------
         assure( addattr(&request.header, sizeof(request), FRA_TABLE,
                        &customTable, sizeof(uint32_t)) == 0 );

         DMHS_LOG(trace) << "Request seqnum " << SeqNumber;
         struct sockaddr_nl sa;
         memset(&sa, 0, sizeof(sa));
         sa.nl_family = AF_NETLINK;
         struct iovec  iov { &request, request.header.nlmsg_len };
         struct msghdr msg { &sa, sizeof(sa), &iov, 1, nullptr, 0, 0 };
         if(sendmsg(sd, &msg, 0) < 0) {
            DMHS_LOG(error) << "sendmsg() failed: " << strerror(errno);
         }
      }

      // ====== Remove the the custom table =================================

   }
}



// ###### Main program ######################################################
int main(int argc, char** argv)
{
   // ====== Initialise =====================================================
   unsigned int          logLevel;
   bool                  logColor;
   std::filesystem::path logFile;

   boost::program_options::options_description commandLineOptions;
   commandLineOptions.add_options()
      ( "help,h",
           "Print help message" )
      ( "version",
           "Print program version" )

      ( "loglevel,L",
           boost::program_options::value<unsigned int>(&logLevel)->default_value(boost::log::trivial::severity_level::info),
           "Set logging level" )
      ( "logfile,O",
           boost::program_options::value<std::filesystem::path>(&logFile)->default_value(std::filesystem::path()),
           "Log file" )
      ( "logcolor,Z",
           boost::program_options::value<bool>(&logColor)->default_value(true),
           "Use ANSI color escape sequences for log output" )
      ( "verbose,v",
           boost::program_options::value<unsigned int>(&logLevel)->implicit_value(boost::log::trivial::severity_level::trace),
           "Verbose logging level" )
      ( "quiet,q",
           boost::program_options::value<unsigned int>(&logLevel)->implicit_value(boost::log::trivial::severity_level::warning),
           "Quiet logging level" )

      ( "interface,I",
           boost::program_options::value<std::vector<std::string>>(),
           "Interface" );


   // ====== Handle command-line arguments ==================================
   boost::program_options::variables_map vm;
   try {
      boost::program_options::store(boost::program_options::command_line_parser(argc, argv).
                                       style(
                                          boost::program_options::command_line_style::style_t::default_style|
                                          boost::program_options::command_line_style::style_t::allow_long_disguise
                                       ).
                                       options(commandLineOptions).
                                       run(), vm);
      boost::program_options::notify(vm);
   }
   catch(std::exception& e) {
      std::cerr << "ERROR: Bad parameter: " << e.what() << "\n";
      return 1;
   }

   if(vm.count("help")) {
       std::cerr << "Usage: " << argv[0] << " parameters" << "\n"
                 << commandLineOptions;
       return 1;
   }
   else if(vm.count("version")) {
      std::cout << "Dynamic Multi-Homing Setup (DynMHS), Version " << DYNMHS_VERSION << "\n";
      return 0;
   }
   if(vm.count("interface")) {
      const std::vector<std::string>& interfaceVector =
         vm["interface"].as<std::vector<std::string>>();
      for(auto iterator = interfaceVector.begin(); iterator != interfaceVector.end(); iterator++) {
         const std::string& interfaceConfiguration = *iterator;
         const int delimiter = interfaceConfiguration.find(':');
         if(delimiter == -1) {
            std::cerr << "ERROR: Bad interface configuration " << interfaceConfiguration << "!\n";
            return 1;
         }
         const std::string interface = interfaceConfiguration.substr(0, delimiter);
         const std::string table     = interfaceConfiguration.substr(delimiter + 1,
                                                                     interfaceConfiguration.size());
         unsigned int tableID = atol(table.c_str());
         if( (tableID < 1000) || (tableID >= 30000) ) {
            std::cerr << "ERROR: Bad table ID in interface configuration "
                      << interfaceConfiguration << "!\n";
            return 1;
         }
         InterfaceMap.insert(std::pair<std::string, unsigned int>(interface, tableID));
      }
   }

   // ====== Initialize logger ==============================================
   initialiseLogger(logLevel, logColor,
                    (logFile != std::filesystem::path()) ? logFile.string().c_str() : nullptr);


   // ====== Signal handling ================================================
   sigset_t mask;
   sigemptyset(&mask);
   sigaddset(&mask, SIGINT);
   if(sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
      perror("sigprocmask() call failed!");
   }
   int sfd = signalfd(-1, &mask, 0);
   if(sfd < 0) {
      perror("signalfd() call failed!");
   }


   // ====== Open Netlink socket ============================================
   int sd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
   if(sd < 0) {
      DMHS_LOG(error) << "socket(AF_NETLINK) failed: " << strerror(errno);
      return 1;
   }
   const int sndbuf = 65536;
   if(setsockopt(sd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) < 0) {
      DMHS_LOG(error) << "setsockopt(SO_SNDBUF) failed: " << strerror(errno);
      return 1;
   }
   const int rcvbuf = 1024*1024;
   if(setsockopt(sd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0) {
      DMHS_LOG(error) << "setsockopt(SO_RCVBUF) failed: " << strerror(errno);
      return 1;
   }

   // ====== Bind Netlink socket ============================================
   sockaddr_nl sa;
   memset(&sa, 0, sizeof(sa));
   sa.nl_family = AF_NETLINK;
   sa.nl_groups = RTMGRP_LINK | RTMGRP_NOTIFY |
                  RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR |
                  RTMGRP_IPV4_ROUTE  | RTMGRP_IPV6_ROUTE;
   if(bind(sd, (struct sockaddr *) &sa, sizeof(sa)) != 0) {
      DMHS_LOG(error) << "bind(AF_NETLINK) failed: " << strerror(errno);
      return 1;
   }


   // ====== Request initial configuration ==================================
   // cleanUpDynMHS(sd);
   if(!initialiseDynMHS(sd)) {
      return 1;
   }
   if(!sendQueuedRequests(sd)) {
      return 1;
   }


   // ====== Main loop ======================================================
   DMHS_LOG(info) << "Main loop ...";
   while(true) {
      // ====== Wait for events =============================================
      pollfd pfd[2];
      pfd[0].fd     = sd;
      pfd[0].events = POLLIN;
      pfd[1].fd     = sfd;
      pfd[1].events = POLLIN;
      const int events = poll((pollfd*)&pfd, 2, -1);

      // ====== Handle events ===============================================
      if(events > 0) {
         // ------ Read Netlink responses -----------------------------------
         if(pfd[0].revents & POLLIN) {
            if(!receiveNetlinkMessages(sd, true)) {
               DMHS_LOG(error) << "recvmsg() failed: " << strerror(errno);
               break;
            }
         }

         // ------ Signal (SIGINT) ------------------------------------------
         if(pfd[1].revents & POLLIN) {
            signalfd_siginfo fdsi;
            if(read(sfd, &fdsi, sizeof(fdsi))) {
               std::cout << "\nGot signal " << fdsi.ssi_signo << "\n";
               break;
            }
         }
      }

      if(!sendQueuedRequests(sd)) {
         return 1;
      }
   }


   // ====== Clean up =======================================================
   DMHS_LOG(info) << "Cleaning up ...";

   cleanUpDynMHS(sd);
   if(sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
      perror("sigprocmask() call failed!");
   }

   puts("YYY");
   while(receiveNetlinkMessages(sd, false, true)) {
     puts("XXX");
   }

   close(sd);
   close(sfd);
   DMHS_LOG(info) << "Done!";
   return 0;
}
