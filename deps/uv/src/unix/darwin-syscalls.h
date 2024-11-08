#ifndef UV_DARWIN_SYSCALLS_H_
#define UV_DARWIN_SYSCALLS_H_

#include <sys/types.h>
#include <sys/socket.h>

/* https://github.com/apple/darwin-xnu/blob/master/bsd/sys/socket.h */

struct mmsghdr {
    struct msghdr msg_hdr;
    size_t msg_len;
};

ssize_t recvmsg_x(int s, const struct mmsghdr* msgp, u_int cnt, int flags);
ssize_t sendmsg_x(int s, const struct mmsghdr* msgp, u_int cnt, int flags);

#endif /* UV_DARWIN_SYSCALLS_H_ */
