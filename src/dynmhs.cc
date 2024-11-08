#include <stdio.h>
#include <stdlib.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <linux/rtnetlink.h>
#include <string.h>

// Example: https://chromium.googlesource.com/chromium/src/+/master/net/base/address_tracker_linux.cc

int main(int argc, char** argv)
{
   struct sockaddr_nl sa;

   memset(&sa, 0, sizeof(sa));
   sa.nl_family = AF_NETLINK;
   sa.nl_groups = RTMGRP_LINK | RTMGRP_NOTIFY | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR;

   int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
   if(fd < 0) {
      perror("socket");
      return 1;
   }
   if(bind(fd, (struct sockaddr *) &sa, sizeof(sa)) != 0) {
      perror("bind");
      return 1;
   }


   static unsigned int seqNumber = 0;


   struct {
     struct nlmsghdr header;
     struct rtgenmsg msg;
   } request = {};
   request.header.nlmsg_len   = NLMSG_LENGTH(sizeof(request.msg));
   request.header.nlmsg_type  = RTM_GETADDR;
   request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
   request.header.nlmsg_pid   = 0;  // This field is opaque to netlink.
   request.header.nlmsg_seq   = ++seqNumber;
   request.header.nlmsg_flags |= NLM_F_ACK;
   request.msg.rtgen_family   = AF_UNSPEC;

   // struct sockaddr_nl sa;
   memset(&sa, 0, sizeof(sa));
   sa.nl_family = AF_NETLINK;

   struct iovec  iov = { &request, request.header.nlmsg_len };
   struct msghdr msg = { &sa, sizeof(sa), &iov, 1, NULL, 0, 0 };
   if(sendmsg(fd, &msg, 0) < 0) {
      perror("sendmsg(RTM_GETADDR)");
      return 1;
   }

   request.header.nlmsg_type = RTM_GETLINK;
   request.header.nlmsg_seq   = ++seqNumber;
   if(sendmsg(fd, &msg, 0) < 0) {
      perror("sendmsg(RTM_GETLINK)");
      return 1;
   }

   for(;;) {
      int len;
      /* 8192 to avoid message truncation on platforms with
         page size > 4096 */
      struct nlmsghdr buf[8192/sizeof(struct nlmsghdr)];
      struct iovec iov = { buf, sizeof(buf) };
      struct sockaddr_nl sa;
      struct msghdr msg;
      struct nlmsghdr *nh;

      msg = { &sa, sizeof(sa), &iov, 1, NULL, 0, 0 };
      len = recvmsg(fd, &msg, 0);
      printf("len=%d\n", len);

      if(len > 0) {
         for(const nlmsghdr* nh = (const nlmsghdr*)buf;
             NLMSG_OK(nh, len);
             nh = NLMSG_NEXT(nh, len)) {
            switch(nh->nlmsg_type) {
               case NLMSG_DONE:
                  // The end of multipart message
                break;
               case NLMSG_ERROR:
                  printf("Unexpected netlink error!\n");
                break;
               case RTM_NEWADDR:
                  puts("NEW ADDR");
                break;
               case RTM_DELADDR:
                  puts("DEL ADDR");
                break;
               case RTM_NEWLINK:
                  puts("RTM_NEWLINK");
                break;
               case RTM_DELLINK:
                  puts("RTM_DELLINK");
                break;
               default:
                  puts("UNKNOWN!");
                break;
            }
         }
      }
   }

   return 0;
}
