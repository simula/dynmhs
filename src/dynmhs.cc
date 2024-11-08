#include <stdio.h>
#include <stdlib.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <linux/rtnetlink.h>
#include <string.h>

// Example: https://chromium.googlesource.com/chromium/src/+/master/net/base/address_tracker_linux.cc


// ###### Send Netlink request ##############################################
bool sendNetlinkRequest(const int sd, const int type)
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
bool readNetlinkMessage(const int sd)
{
   // 8192 to avoid message truncation on platforms with page size > 4096
   struct nlmsghdr buffer[8192 / sizeof(struct nlmsghdr)];
   struct iovec       iov = { buffer, sizeof(buffer) };
   struct sockaddr_nl sa;
   struct msghdr      msg { &sa, sizeof(sa), &iov, 1, nullptr, 0, 0 };
   struct nlmsghdr*   header;

   ssize_t len = recvmsg(sd, &msg, 0);
   while(len > 0) {
      for(const nlmsghdr* header = (const nlmsghdr*)buffer;
          NLMSG_OK(header, len); header = NLMSG_NEXT(header, len)) {
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
               puts("RTM_NEWLINK");
             break;
            case RTM_DELLINK:
               puts("RTM_DELLINK");
             break;
            case RTM_NEWADDR:
               puts("NEW ADDR");
             break;
            case RTM_DELADDR:
               puts("DEL ADDR");
             break;
            case RTM_NEWROUTE:
               puts("RTM_NEWROUTE");
             break;
            case RTM_DELROUTE:
               puts("RTM_DELROUTE");
             break;
            default:
               puts("UNKNOWN!");
             break;
         }
      }
      len = recvmsg(sd, &msg, 0);
   }
   return false;
}


int main(int argc, char** argv)
{
   // ====== Open Netlink socket ============================================
   int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
   if(fd < 0) {
      perror("socket");
      return 1;
   }

   // ====== Bind Netlink socket ============================================
   sockaddr_nl sa;
   memset(&sa, 0, sizeof(sa));
   sa.nl_family = AF_NETLINK;
   sa.nl_groups = RTMGRP_LINK | RTMGRP_NOTIFY |
                  RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR |
                  RTMGRP_IPV4_ROUTE  | RTMGRP_IPV6_ROUTE;
   if(bind(fd, (struct sockaddr *) &sa, sizeof(sa)) != 0) {
      perror("bind");
      return 1;
   }

   // ====== Request initial configuration ==================================
   if(!sendNetlinkRequest(fd, RTM_GETLINK)) {
      perror("sendmsg(RTM_GETLINK)");
      return 1;
   }
   if(!readNetlinkMessage(fd)) {
      perror("recvmsg()");
      return 1;
   }

   if(!sendNetlinkRequest(fd, RTM_GETADDR)) {
      perror("sendmsg(RTM_GETADDR)");
      return 1;
   }
   if(!readNetlinkMessage(fd)) {
      perror("recvmsg()");
      return 1;
   }
   if(!sendNetlinkRequest(fd, RTM_GETROUTE)) {
      perror("sendmsg(RTM_GETROUTE)");
      return 1;
   }
   if(!readNetlinkMessage(fd)) {
      perror("recvmsg()");
      return 1;
   }

   // ====== Main loop ======================================================
   puts("Main loop ...");
   while(readNetlinkMessage(fd)) {
   }

   return 0;
}
