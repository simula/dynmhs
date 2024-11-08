#include <stdio.h>
#include <stdlib.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <linux/rtnetlink.h>
#include <string.h>

#include <filesystem>
#include <iostream>
#include <boost/asio/ip/address.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>

#include "logger.h"
#include "package-version.h"


// Example code:
// * https://chromium.googlesource.com/chromium/src/+/master/net/base/address_tracker_linux.cc
// * https://gist.github.com/cl4u2/5204374


// ###### Handle link change event ##########################################
static void handleLinkEvent(const nlmsghdr*      header,
                            const unsigned short eventType)
{
   const ifinfomsg* ifinfo = (const ifinfomsg*)NLMSG_DATA(header);
   int length = header->nlmsg_len - NLMSG_LENGTH(sizeof(*ifinfo));

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
   switch(eventType) {
      case RTM_NEWLINK:
         eventName = "NEW";
       break;
      case RTM_GETLINK:
         eventName = "GET";
       break;
      case RTM_DELLINK:
         eventName = "DELETE";
       break;
      default:
         eventName = "UNKNOWN";
       break;
   }
   DMHS_LOG(info) << "Link event:"
                  << boost::format(" event=%s ifindex=%d ifname=%s")
                        % eventName
                        % ifinfo->ifi_index
                        % (ifName != nullptr) ? ifName : "UNKNOWN?!";
}


// ###### Handle address change event ##########################################
static void handleAddressEvent(const nlmsghdr*      header,
                               const unsigned short eventType)
{
   const ifaddrmsg* ifaddr = (const ifaddrmsg*)NLMSG_DATA(header);
   int length = header->nlmsg_len - NLMSG_LENGTH(sizeof(*ifaddr));

   if( (eventType == RTM_NEWADDR) || (eventType == RTM_GETADDR) ) {
      DMHS_LOG(info) << "Address added: ifindex=" << ifaddr->ifa_index;
   }
   else if(eventType == RTM_DELADDR) {
      DMHS_LOG(info) << "Address removed: ifindex=" << ifaddr->ifa_index;
   }
}


// ###### Handle route change event #########################################
static void handleRouteEvent(const nlmsghdr*      header,
                             const unsigned short eventType)
{
   const rtmsg* rt = (const rtmsg*)NLMSG_DATA(header);
   int length = header->nlmsg_len - NLMSG_LENGTH(sizeof(*rt));

   if( (eventType == RTM_NEWROUTE) || (eventType == RTM_GETROUTE) ) {
      DMHS_LOG(info) << "Route added: table=" << rt->rtm_table;
   }
   else if(eventType == RTM_DELROUTE) {
      DMHS_LOG(info) << "Route removed: table=" << rt->rtm_table;
   }

   // puts("---");
//
//    struct rtattr *attribute;
//
//    for(const rtattr* rta = (const rtattr*)NLMSG_PAYLOAD(header, sizeof(rtattr));
//        RTA_OK(rta, length); rta = RTA_NEXT(rta, length)) {
//       puts("xx");
//    }
}


// ###### Send Netlink request ##############################################
static bool sendNetlinkRequest(const int sd, const int type)
{
   static unsigned int seqNumber = 0;

   struct {
     struct nlmsghdr header;
     struct rtgenmsg msg;
   } request = {};
   request.header.nlmsg_len   = NLMSG_LENGTH(sizeof(request.msg));
   request.header.nlmsg_type  = type;
   request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
   request.header.nlmsg_pid   = 0;  // This field is opaque to netlink.
   request.header.nlmsg_seq   = seqNumber++;
   request.header.nlmsg_flags |= NLM_F_ACK;
   request.msg.rtgen_family   = AF_UNSPEC;

   struct sockaddr_nl sa;
   memset(&sa, 0, sizeof(sa));
   sa.nl_family = AF_NETLINK;

   struct iovec  iov = { &request, request.header.nlmsg_len };
   struct msghdr msg = { &sa, sizeof(sa), &iov, 1, nullptr, 0, 0 };
   if(sendmsg(sd, &msg, 0) < 0) {
      return false;
   }
   return true;
}


// ###### Read Netlink message ##############################################
static bool readNetlinkMessage(const int sd)
{
   // 8192 to avoid message truncation on platforms with page size > 4096
   struct nlmsghdr buffer[8192 / sizeof(struct nlmsghdr)];
   struct iovec       iov = { buffer, sizeof(buffer) };
   struct sockaddr_nl sa;
   struct msghdr      msg { &sa, sizeof(sa), &iov, 1, nullptr, 0, 0 };
   struct nlmsghdr*   header;

   int length = recvmsg(sd, &msg, 0);
   while(length > 0) {
      for(const nlmsghdr* header = (const nlmsghdr*)buffer;
          NLMSG_OK(header, length); header = NLMSG_NEXT(header, length)) {
         switch(header->nlmsg_type) {
            case NLMSG_DONE:
               // The end of multipart message
               return true;
             break;
            case NLMSG_ERROR: {
                  const nlmsgerr* errormsg = (const nlmsgerr*)header;
                  if(errormsg == nullptr) {
                     break;
                  }
                  printf("Unexpected netlink error %d!\n", errormsg->error);
               }
             break;
            case RTM_NEWLINK:
            case RTM_DELLINK:
            case RTM_GETLINK:
               handleLinkEvent(header, header->nlmsg_type);
             break;
            case RTM_NEWADDR:
            case RTM_DELADDR:
            case RTM_GETADDR:
               handleAddressEvent(header, header->nlmsg_type);
             break;
            case RTM_NEWROUTE:
            case RTM_DELROUTE:
            case RTM_GETROUTE:
               handleRouteEvent(header, header->nlmsg_type);
             break;
            default:
               puts("UNKNOWN!");
             break;
         }
      }
      length = recvmsg(sd, &msg, 0);
   }
   return false;
}


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
           "Quiet logging level" );

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
   else if(vm.count("verion")) {
      std::cout << "Dynamic Multi-Homing Setup (DynMHS), Version " << DYNMHS_VERSION << "\n";
      return 0;
   }

   // ====== Initialize =====================================================
   initialiseLogger(logLevel, logColor,
                    (logFile != std::filesystem::path()) ? logFile.string().c_str() : nullptr);


   // ====== Open Netlink socket ============================================
   int sd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
   if(sd < 0) {
      perror("socket");
      return 1;
   }
   const int sndbuf = 32768;
   if(setsockopt(sd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) < 0) {
      perror("setsockopt(SO_SNDBUF)");
      return 1;
   }
   const int rcvbuf = 1024*1024;
   if(setsockopt(sd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0) {
      perror("setsockopt(SO_RCVBUF)");
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
      perror("bind");
      return 1;
   }

   // ====== Request initial configuration ==================================
   if(!sendNetlinkRequest(sd, RTM_GETLINK)) {
      perror("sendmsg(RTM_GETLINK)");
      return 1;
   }
   if(!readNetlinkMessage(sd)) {
      perror("recvmsg()");
      return 1;
   }

   if(!sendNetlinkRequest(sd, RTM_GETADDR)) {
      perror("sendmsg(RTM_GETADDR)");
      return 1;
   }
   if(!readNetlinkMessage(sd)) {
      perror("recvmsg()");
      return 1;
   }

   if(!sendNetlinkRequest(sd, RTM_GETROUTE)) {
      perror("sendmsg(RTM_GETROUTE)");
      return 1;
   }
   if(!readNetlinkMessage(sd)) {
      perror("recvmsg()");
      return 1;
   }

   // ====== Main loop ======================================================
   puts("Main loop ...");
   while(readNetlinkMessage(sd)) {
   }

   return 0;
}
