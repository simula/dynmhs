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

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <vector>
#include <boost/algorithm/string.hpp>
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



#define NETLINK_TIMEOUT 5000   // 5000 ms

enum DynMHSOperatingMode {
   Undefined   = 0,
   Reset       = 1,
   Operational = 2
};
static DynMHSOperatingMode                            Mode                     = Undefined;
static uint32_t                                       SeqNumber                = 1000000000;
static uint32_t                                       AwaitedSeqNumber         = 0;
static int                                            LastError                = 0;
static bool                                           WaitingForAcknowlegement = false;
static std::map<std::string, unsigned int>            InterfaceMap;
static std::queue<std::pair<const nlmsghdr*, size_t>> RequestQueue;


// ###### Append strings from source vector to destination vector ###########
static void addStringsToVector(std::vector<std::string>&       destination,
                               const std::vector<std::string>& source)
{
   destination.insert(destination.end(), source.begin(), source.end());
}


// ###### Attribute helper ##################################################
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
   rta->rta_len  = len;
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
        DMHS_LOG(trace) << boost::format("Netlink error %d (%s) for seqnum %u")
                              % errormsg->error
                              % strerror(-errormsg->error)
                              % errormsg->msg.nlmsg_seq;
      }
   }
}


// ###### Handle link change event ##########################################
static void handleLinkEvent(const nlmsghdr* message)
{
   // ====== Initialise =====================================================
   const ifinfomsg* ifinfo = (const ifinfomsg*)NLMSG_DATA(message);
   unsigned int     length = message->nlmsg_len - NLMSG_LENGTH(sizeof(*ifinfo));
   const char*      eventName;
   if(message->nlmsg_type != RTM_NEWLINK) {
      eventName = "RTM_NEWLINK";
   }
   else if(message->nlmsg_type != RTM_DELLINK) {
      eventName = "RTM_DELLINK";
   }
   else {
      return;
   }

   // ====== Parse attributes ===============================================
   const char* ifName = nullptr;
   for(const rtattr* rta = IFLA_RTA(ifinfo); RTA_OK(rta, length); rta = RTA_NEXT(rta, length)) {
      if(rta->rta_type == IFLA_IFNAME) {
         ifName = (const char*)RTA_DATA(rta);
      }
   }

   // ====== Show status ====================================================
   DMHS_LOG(debug) << boost::format("Link event: event=%s ifindex=%d ifname=%s")
                         % eventName
                         % ifinfo->ifi_index
                         % ((ifName != nullptr) ? ifName : "UNKNOWN?!");
}


// ###### Handle address change event ##########################################
static void handleAddressEvent(const nlmsghdr* message)
{
   // ====== Initialise =====================================================
   const ifaddrmsg*   ifa       = (const ifaddrmsg*)NLMSG_DATA(message);
   const unsigned int ifalength = message->nlmsg_len;
   const char*        eventName;
   if(message->nlmsg_type != RTM_NEWADDR) {
      eventName = "RTM_NEWADDR";
   }
   else if(message->nlmsg_type != RTM_DELADDR) {
      eventName = "RTM_DELADDR";
   }
   else {
      return;
   }

   // ====== Parse attributes ===============================================
   const unsigned int       ifIndex = ifa->ifa_index;
   char                     ifNameBuffer[IF_NAMESIZE];
   const char*              ifName;
   boost::asio::ip::address address;
   const char*              addressPtr  = nullptr;
   bool                     isLinkLocal = false;
   const unsigned int       prefixLength = ifa->ifa_prefixlen;
   unsigned int             length = ifalength - NLMSG_LENGTH(sizeof(*ifa));
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


   // ====== Show status ====================================================
   DMHS_LOG(trace) << boost::format("Address event: event=%s if=%s (%d) address=%s/%d")
                         % eventName
                         % ifName
                         % ifIndex
                         % address.to_string()
                         % prefixLength;


   // ====== Check whether an update in the custom table is necessary =======
   /* In Operational mode:
    * If there is an address change on an interface with custom table:
    * Update the rule pointing from the address to the custom table
    * */
   if( (Mode == Operational)   &&
       (addressPtr != nullptr) &&
       (!isLinkLocal) ) {

      // ------ Check whether interface has a custom table ------------------
      const auto found = InterfaceMap.find(ifName);
      if(found != InterfaceMap.end()) {
         const uint32_t customTable = found->second;
         DMHS_LOG(debug) << "Update of rule for table " << customTable << " is necessary ...";

         // ------ Build RTM_NEWRULE/RTM_DELRULE request --------------------
         struct _request {
            nlmsghdr     header;
            fib_rule_hdr frh;
            char         buffer[256];
         };
         _request* request = (_request*)new char[sizeof(*request)];
         assure(request != nullptr);
         memset(request, 0, sizeof(*request));

         request->header.nlmsg_len   = NLMSG_LENGTH(sizeof(request->frh));
         if(message->nlmsg_type == RTM_NEWADDR) {
            request->header.nlmsg_type  = RTM_NEWRULE;
            request->header.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
         }
         else {
            request->header.nlmsg_type  = RTM_DELRULE;
            request->header.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
         }
         request->header.nlmsg_pid   = 0;  // This field is opaque to netlink.
         request->header.nlmsg_seq   = ++SeqNumber;
         request->frh.family         = ifa->ifa_family;
         request->frh.action         = FR_ACT_TO_TBL;
         request->frh.table          = RT_TABLE_UNSPEC;

         // ------ "from" parameter: address/prefix -------------------------
         if(ifa->ifa_family == AF_INET) {
            assure( addattr(&request->header, sizeof(*request), FRA_SRC,
                            addressPtr, 4) == 0 );
            request->frh.src_len = 32;
         }
         else {
            assure( addattr(&request->header, sizeof(*request), FRA_SRC,
                            addressPtr, 16) == 0 );
            request->frh.src_len = 128;
         }

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
static void handleRouteEvent(const nlmsghdr* message)
{
   // ====== Initialise =====================================================
   const unsigned int messageLength = message->nlmsg_len;
   const rtmsg*       rtm           = (const rtmsg*)NLMSG_DATA(message);
   const unsigned int rtmLength     = message->nlmsg_len;
   const char*        eventName;
   if(message->nlmsg_type != RTM_NEWROUTE) {
      eventName = "RTM_NEWROUTE";
   }
   else if(message->nlmsg_type != RTM_DELROUTE) {
      eventName = "RTM_DELROUTE";
   }
   else {
      return;
   }

   // ====== Parse attributes ===============================================
   boost::asio::ip::address destination =
      (rtm->rtm_family == AF_INET) ?
         boost::asio::ip::address(boost::asio::ip::address_v4()) :
         boost::asio::ip::address(boost::asio::ip::address_v6());
   const unsigned int       destinationPrefixLength = rtm->rtm_dst_len;
   boost::asio::ip::address gateway;
   bool                     hasGateway = false;
   unsigned int*            tablePtr   = nullptr;
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
            tablePtr = (unsigned int*)RTA_DATA(rta);
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


   // ====== Show status =================================================
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
   DMHS_LOG(trace) << boost::format("Route event: event=%s: table=%d destination=%s scope=%s %s if=%s (%d) %s")
                         % eventName
                         % *tablePtr
                         % (destination.to_string() + "/" +
                              std::to_string(destinationPrefixLength))
                         % scopeName
                         % ((hasGateway == true) ? ("G=" + gateway.to_string()) : "G=---")
                         % oifName
                         % oifIndex
                         % ((metric >= 0) ? std::to_string(metric) : "");


   // ====== Check whether an update in the custom table is necessary =======
   bool     updateNecessary = false;
   uint16_t updateType;
   if( (Mode == Operational) &&
       (*tablePtr == RT_TABLE_MAIN) &&
       ( (message->nlmsg_type == RTM_NEWROUTE) ||
         (message->nlmsg_type == RTM_DELROUTE) ) ) {
      /* In Operational mode, synchronise a routing change from the main table
       * into the custom table. Only changes in the main table are of interest
       * here! */
      // ------ Find custom table in the InterfaceMap -----------------------
      const auto found = InterfaceMap.find(oifName);
      if(found != InterfaceMap.end()) {
          const unsigned int customTable = found->second;
         DMHS_LOG(debug) << "Update of route in table " << customTable << " is necessary ...";
         updateNecessary = true;
         updateType      = message->nlmsg_type;
         *tablePtr       = customTable;   // <<-- clone entry into custom table
      }
   }
   else if( (Mode == Reset) &&
            (*tablePtr != RT_TABLE_MAIN) ) {
      /* In Reset mode, delete all routing table entries in the custom tables.
       * Here, only the custom tables are of interest! */
      // ------ Check if entry belongs to custom table in the InterfaceMap --
      for(auto iterator = InterfaceMap.begin(); iterator != InterfaceMap.end(); iterator++) {
         const unsigned int customTable = iterator->second;
         if(*tablePtr == customTable) {
            DMHS_LOG(trace) << "Removing route from table " << customTable << " ...";
            updateNecessary = true;
            updateType      = RTM_DELROUTE;
            break;
         }
      }
   }

   // ====== Apply update ===================================================
   if(updateNecessary) {
      // ------ Copy the message and enqueue it for sending it later -----
      nlmsghdr* updateMessage = (nlmsghdr*)new char[messageLength];
      assure(updateMessage != nullptr);
      memcpy(updateMessage, message, messageLength);

      updateMessage->nlmsg_type  = updateType;
      updateMessage->nlmsg_flags = (updateType == RTM_NEWROUTE) ?
         NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK :
         NLM_F_REQUEST | NLM_F_ACK;
      updateMessage->nlmsg_seq   = ++SeqNumber;

      RequestQueue.push(std::pair<const nlmsghdr*, size_t>(
         updateMessage, messageLength));
      DMHS_LOG(trace) << "Request seqnum " << SeqNumber;
   }
}


// ###### Handle rule change event ##########################################
static void handleRuleEvent(const nlmsghdr* message)
{
   // ====== Initialise =====================================================
   const unsigned int  messageLength = message->nlmsg_len;
   const fib_rule_hdr* frh           = (const fib_rule_hdr*)NLMSG_DATA(message);
   const unsigned int  frhLength     = message->nlmsg_len;
   const char*         eventName;
   if(message->nlmsg_type != RTM_NEWRULE) {
      eventName = "RTM_NEWRULE";
   }
   else if(message->nlmsg_type != RTM_DELRULE) {
      eventName = "RTM_DELRULE";
   }
   else {
      return;
   }

   // ====== Parse attributes ===============================================
   unsigned int* tablePtr   = nullptr;
   unsigned int  priority   = 0;
   unsigned int  length     = frhLength - NLMSG_LENGTH(sizeof(*frh));
   for(const rtattr* rta = (const rtattr*)((char*)frh + NLMSG_ALIGN(sizeof(fib_rule_hdr)));
       RTA_OK(rta, length); rta = RTA_NEXT(rta, length)) {
      switch(rta->rta_type) {
         case FRA_TABLE:
            tablePtr = (unsigned int*)RTA_DATA(rta);
          break;
         case FRA_PRIORITY:
            priority = *(unsigned int*)RTA_DATA(rta);
          break;
      }
   }
   assure(tablePtr != nullptr);


   // ====== Show status ====================================================
   DMHS_LOG(trace) << boost::format("Rule event: event=%s: table=%u priority=%u")
                         % eventName
                         % *tablePtr
                         % priority;


   // ====== Check whether a removal of the rule is necessary ===============
   bool removalNecessary = false;
   if( (Mode == Reset) && (tablePtr != nullptr) ) {
      // ------ Check if entry belongs to custom table in the InterfaceMap --
      for(auto iterator = InterfaceMap.begin(); iterator != InterfaceMap.end(); iterator++) {
         const unsigned int customTable = iterator->second;
         if(*tablePtr == customTable) {
            DMHS_LOG(info) << "Removing rule for table " << customTable << " ...";
            removalNecessary = true;
            break;
         }
      }
   }

   // ====== Apply removal ==================================================
   if(removalNecessary) {
      // ------ Copy the message and enqueue it for sending it later -----
      nlmsghdr* updateMessage = (nlmsghdr*)new char[messageLength];
      assure(updateMessage != nullptr);
      memcpy(updateMessage, message, messageLength);

      updateMessage->nlmsg_type  = RTM_DELRULE;
      updateMessage->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
      updateMessage->nlmsg_seq   = ++SeqNumber;

      RequestQueue.push(std::pair<const nlmsghdr*, size_t>(
         updateMessage, messageLength));
      DMHS_LOG(trace) << "Request seqnum " << SeqNumber;
   }
}


// ###### Send simple Netlink request #######################################
static void queueSimpleNetlinkRequest(const int type)
{
   struct _request {
      nlmsghdr header;
      rtgenmsg msg;
   };
   _request* request = (_request*)new char[sizeof(*request)];
   assure(request != nullptr);
   memset(request, 0, sizeof(*request));

   request->header.nlmsg_len   = NLMSG_LENGTH(sizeof(request->msg));
   request->header.nlmsg_type  = type;
   request->header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP | NLM_F_ACK;
   request->header.nlmsg_pid   = 0;  // This field is opaque to netlink.
   request->header.nlmsg_seq   = ++SeqNumber;
   request->msg.rtgen_family   = AF_UNSPEC;

   RequestQueue.push(std::pair<const nlmsghdr*, size_t>(
      &request->header, request->header.nlmsg_len));
   DMHS_LOG(trace) << "Request seqnum " << SeqNumber;
}


// ###### Send queued Netlink requests ######################################
static bool sendQueuedRequests(const int sd)
{
   while(!RequestQueue.empty()) {
      // ------ Send queued Netlink request ---------------------------------
      std::pair<const nlmsghdr*, size_t>& command = RequestQueue.front();
      const nlmsghdr* message       = command.first;
      const size_t    messageLength = command.second;
      sockaddr_nl sa { };
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
                                   const bool nonBlocking = false)
{
   // ====== Initialise structures for recvmsg() ============================
   nlmsghdr    buffer[65536 / sizeof(nlmsghdr)];
   iovec       iov { buffer, sizeof(buffer) };
   sockaddr_nl sa;
   msghdr      msg { &sa, sizeof(sa), &iov, 1, nullptr, 0, 0 };
   const int   flags = (nonBlocking == true) ? MSG_DONTWAIT : 0;
   int         length;

   // ====== Reception loop =================================================
   while( (length = recvmsg(sd, &msg, flags)) > 0) {
      for(const nlmsghdr* header = (const nlmsghdr*)buffer;
          NLMSG_OK(header, length); header = NLMSG_NEXT(header, length)) {

         // ====== Check whether this acknowledgement was waited ============
         if((WaitingForAcknowlegement) &&
            (header->nlmsg_seq == AwaitedSeqNumber)) {
            if( (header->nlmsg_type == NLMSG_ERROR) &&
                (header->nlmsg_len >= NLMSG_LENGTH(sizeof(nlmsgerr))) ) {
               const nlmsgerr* errormsg = (const nlmsgerr*)NLMSG_DATA(header);
               LastError = errormsg->error;
            }
            else {
               LastError = 0;   // success
            }
            DMHS_LOG(trace) << boost::format("Got awaited ack for seqnum %u: error %d (%s)")
                                  % header->nlmsg_seq
                                  % LastError
                                  % strerror(-LastError);
            WaitingForAcknowlegement = false;
         }

         // ====== Handle the different message types =======================
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
            case RTM_NEWRULE:
            case RTM_DELRULE:
               if(header->nlmsg_len >= NLMSG_LENGTH(sizeof(rtmsg))) {
                  handleRuleEvent(header);
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


// ###### Wait for Netlink acknowledgement ##################################
static bool waitForAcknowledgement(const int          sd,
                                   const unsigned int seqNumber,
                                   const unsigned int timeout)
{
   WaitingForAcknowlegement = true;
   AwaitedSeqNumber         = seqNumber;

   // ====== Reception loop =================================================
   const std::chrono::time_point<std::chrono::steady_clock> t1 =
      std::chrono::steady_clock::now();
   std::chrono::time_point<std::chrono::steady_clock> t2;
   while(WaitingForAcknowlegement) {
      t2 = std::chrono::steady_clock::now();
      int ms = timeout - std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
      if(ms < 1) {
         ms = 0;
      }
      pollfd pfd[1];
      pfd[0].fd     = sd;
      pfd[0].events = POLLIN;
      const int events = poll((pollfd*)&pfd, 1, ms);
      if(events > 0) {
         receiveNetlinkMessages(sd, true);
      }
   }

   return (WaitingForAcknowlegement == false);
}


// ###### Initialise DynMHS #################################################
struct SimpleRequest {
   int         RequestType;
   const char* RequestName;
};
static bool initialiseDynMHS(int sd)
{
   static const SimpleRequest InitRequests[] = {
    { RTM_GETLINK,  "RTM_GETLINK"  },
    { RTM_GETADDR,  "RTM_GETADDR"  },
    { RTM_GETROUTE, "RTM_GETROUTE" },
    { RTM_GETRULE,  "RTM_GETRULE"  }
  };

   Mode = Operational;

   for(unsigned int i = 0; i < sizeof(InitRequests) / sizeof(InitRequests[0]); i++) {
      DMHS_LOG(debug) << "Making " << InitRequests[i].RequestName << " request ...";
      queueSimpleNetlinkRequest(InitRequests[i].RequestType);
      sendQueuedRequests(sd);
      if(!waitForAcknowledgement(sd, SeqNumber, NETLINK_TIMEOUT)) {
        DMHS_LOG(error) << "No response to " << InitRequests[i].RequestName << " request";
        return false;
      }
   }

   return true;
}


// ###### Clean up DynMHS ###################################################
static bool cleanUpDynMHS(int sd)
{
   static const SimpleRequest ShutdownRequests[] = {
      { RTM_GETRULE,  "RTM_GETRULE"  },
      { RTM_GETROUTE, "RTM_GETROUTE" }
   };

   Mode = Reset;

   // ====== Remove custom rules and tables =================================
   for(unsigned int i = 0; i < sizeof(ShutdownRequests) / sizeof(ShutdownRequests[0]); i++) {
      DMHS_LOG(debug) << "Making " << ShutdownRequests[i].RequestName << " request ...";
      // ------ Request a dump of the rules/tables --------------------------
      queueSimpleNetlinkRequest(ShutdownRequests[i].RequestType);
      sendQueuedRequests(sd);
      if(!waitForAcknowledgement(sd, SeqNumber, NETLINK_TIMEOUT)) {
         DMHS_LOG(error) << "No response to " << ShutdownRequests[i].RequestName << " request";
      }
      // ------ Remove all entries in rules/tables --------------------------
      if(!RequestQueue.empty()) {
         // The removal requests are queued now. Send them, then wait until
         // they are acknowledged.
         if(!RequestQueue.empty()) {
            sendQueuedRequests(sd);
            if(!waitForAcknowledgement(sd, SeqNumber, NETLINK_TIMEOUT)) {
               DMHS_LOG(error) << "Timeout waiting for acknowledgement";
            }
         }
      }
   }

   // ====== Clean up the request queue =====================================
   while(!RequestQueue.empty()) {
      std::pair<const nlmsghdr*, size_t>& command = RequestQueue.front();
      const nlmsghdr* message = command.first;
      delete [] message;
      RequestQueue.pop();
   }

   return true;
}



// ###### Main program ######################################################
int main(int argc, char** argv)
{
   // ====== Initialise =====================================================
   unsigned int             logLevel;
   bool                     logColor;
   std::filesystem::path    configFile;
   std::filesystem::path    logFile;

   boost::program_options::options_description commandLineOptions;
   commandLineOptions.add_options()
      ( "help,h",
           "Print help message" )
      ( "version",
           "Print program version" )

      ( "config,C",
           boost::program_options::value<std::filesystem::path>(&configFile)->default_value(std::filesystem::path()),
           "Configuration file" )
      ( "loglevel,L",
           boost::program_options::value<unsigned int>(&logLevel)->default_value(boost::log::trivial::severity_level::info),
           "Set logging level" )
      ( "logfile,O",
           boost::program_options::value<std::filesystem::path>(&logFile)->default_value(std::filesystem::path()),
           "Log file" )
      ( "logcolor,Z",
           boost::program_options::value<bool>(&logColor)->default_value(true),
           "Use ANSI color escape sequences for log output" )
      ( "verbose,!",
           boost::program_options::value<unsigned int>(&logLevel)->implicit_value(boost::log::trivial::severity_level::trace),
           "Verbose logging level" )
      ( "quiet,q",
           boost::program_options::value<unsigned int>(&logLevel)->implicit_value(boost::log::trivial::severity_level::warning),
           "Quiet logging level" )

      ( "network,N",
           boost::program_options::value<std::vector<std::string>>(),
           "Network to rule mapping" );

      // ------ Deprecated! -------------------------------------------------
      ( "interface,I",
           boost::program_options::value<std::vector<std::string>>(),
           "Network to rule mapping" );
      // ------ Deprecated! -------------------------------------------------


   // ====== Handle command-line arguments ==================================
   boost::program_options::variables_map commandLineVariablesMap;
   try {
      boost::program_options::store(
         boost::program_options::command_line_parser(argc, argv).
            style(
               boost::program_options::command_line_style::style_t::default_style|
               boost::program_options::command_line_style::style_t::allow_long_disguise
            ).
            options(commandLineOptions).
            run(),
            commandLineVariablesMap);
      boost::program_options::notify(commandLineVariablesMap);
   }
   catch(std::exception& e) {
      std::cerr << "ERROR: Bad parameter: " << e.what() << "\n";
      return 1;
   }

   if(commandLineVariablesMap.count("help")) {
      std::cerr << "Usage: " << argv[0] << " parameters" << "\n"
                << commandLineOptions;
      return 1;
   }
   else if(commandLineVariablesMap.count("version")) {
      std::cout << "Dynamic Multi-Homing Setup (DynMHS), Version " << DYNMHS_VERSION << "\n";
      return 0;
   }

   // ====== Handle parameters from configuration file ======================
   boost::program_options::variables_map configFileVariablesMap;
   if(configFile != std::filesystem::path()) {
      std::ifstream configurationInputStream(configFile);
      if(!configurationInputStream.good()) {
         std::cerr << "ERROR: Unable to read configuration file "
                   << configFile << "\n";
         return 1;
      }

      boost::program_options::options_description configFileOptions;
      configFileOptions.add_options()
         ( "LOGLEVEL",
            boost::program_options::value<unsigned int>(&logLevel)->default_value(boost::log::trivial::severity_level::info) )
         ( "LOGFILE",
            boost::program_options::value<std::filesystem::path>(&logFile) )
         ( "LOGCOLOR",
            boost::program_options::value<bool>(&logColor) )
         ( "NETWORK",
           boost::program_options::value<std::vector<std::string>>() )
         // ------ Deprecated! -------------------------------------------------
         ( "NETWORK1", boost::program_options::value<std::vector<std::string>>() )
         ( "NETWORK2", boost::program_options::value<std::vector<std::string>>() )
         ( "NETWORK3", boost::program_options::value<std::vector<std::string>>() )
         ( "NETWORK4", boost::program_options::value<std::vector<std::string>>() )
         ( "NETWORK5", boost::program_options::value<std::vector<std::string>>() );
         // ------ Deprecated! -------------------------------------------------

      try {
         boost::program_options::store(
            boost::program_options::parse_config_file(
               configurationInputStream , configFileOptions), configFileVariablesMap);
         boost::program_options::notify(configFileVariablesMap);
      } catch(const std::exception& e) {
         std::cerr << "ERROR: Parsing configuration file " << configFile
                  << " failed: " << e.what() << "\n";
         return 1;
      }
   }


   // ====== Initialise InterfaceMap ========================================
   std::vector<std::string> networkVector;
   const char* labels1[] = { "network", "interface" };
   for(unsigned int i = 0; i < sizeof(labels1) / sizeof(labels1[0]); i++) {
      const char* label = labels1[i];
      if(commandLineVariablesMap.count(label)) {
         addStringsToVector(networkVector, commandLineVariablesMap[label].as<std::vector<std::string>>());
      }
   }
   const char* labels2[] = { "NETWORK", "NETWORK1", "NETWORK2", "NETWORK3", "NETWORK4", "NETWORK5" };
   for(unsigned int i = 0; i < sizeof(labels2) / sizeof(labels2[0]); i++) {
      const char* label = labels2[i];
      if(configFileVariablesMap.count(label)) {
         addStringsToVector(networkVector, configFileVariablesMap[label].as<std::vector<std::string>>());
      }
   }
   for(std::string& network : networkVector) {
      boost::trim_if(network, boost::is_any_of("\""));
      if(network != "") {
         const int delimiter = network.rfind(':');
         if(delimiter == -1) {
            std::cerr << "ERROR: Bad network configuration " << network << "!\n";
            return 1;
         }
         const std::string interface = network.substr(0, delimiter);
         const std::string table     = network.substr(delimiter + 1,
                                                      network.size());
         unsigned int tableID = atol(table.c_str());
         if( (tableID < 1000) || (tableID >= 30000) ) {
            std::cerr << "ERROR: Bad table ID in network configuration "
                      << network << "!\n";
            return 1;
         }
         InterfaceMap.insert(std::pair<std::string, unsigned int>(interface, tableID));
      }
   }
   if(InterfaceMap.size() < 1) {
      std::cerr << "ERROR: No networks were defined!\n";
      return 1;
   }

   // ====== Initialize logger ==============================================
   initialiseLogger(logLevel, logColor,
                    (logFile != std::filesystem::path()) ? logFile.string().c_str() : nullptr);

   DMHS_LOG(info) << "Starting DynMHS " << DYNMHS_VERSION << " ...";
   for(auto iterator = InterfaceMap.begin(); iterator != InterfaceMap.end(); iterator++) {
      DMHS_LOG(info) << "Mapping: " << iterator->first
                     << " -> table " << iterator->second;
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
   sockaddr_nl sa { };
   sa.nl_family = AF_NETLINK;
   sa.nl_groups = RTMGRP_LINK | RTMGRP_NOTIFY |
                  RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR |
                  RTMGRP_IPV4_ROUTE  | RTMGRP_IPV6_ROUTE;
   if(bind(sd, (sockaddr*)&sa, sizeof(sa)) != 0) {
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
   Mode = Operational;


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

   if(sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
      perror("sigprocmask() call failed!");
   }
   cleanUpDynMHS(sd);
   close(sd);
   close(sfd);

   DMHS_LOG(info) << "Done!";
   return 0;
}
