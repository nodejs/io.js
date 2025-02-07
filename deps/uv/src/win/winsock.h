/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef UV_WIN_WINSOCK_H_
#define UV_WIN_WINSOCK_H_

#include <winsock2.h>
#include <iptypes.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "winapi.h"


/*
 * MinGW is missing these too
 */
#ifndef SO_UPDATE_CONNECT_CONTEXT
# define SO_UPDATE_CONNECT_CONTEXT 0x7010
#endif

#ifndef TCP_KEEPALIVE
# define TCP_KEEPALIVE 3
#endif

#ifndef IPV6_V6ONLY
# define IPV6_V6ONLY 27
#endif

#ifndef IPV6_HOPLIMIT
# define IPV6_HOPLIMIT 21
#endif

#ifndef SIO_BASE_HANDLE
# define SIO_BASE_HANDLE 0x48000022
#endif

#ifndef MCAST_JOIN_SOURCE_GROUP
# define MCAST_JOIN_SOURCE_GROUP 45
#endif

#ifndef MCAST_LEAVE_SOURCE_GROUP
# define MCAST_LEAVE_SOURCE_GROUP 46
#endif

/*
 * TDI defines that are only in the DDK.
 * We only need receive flags so far.
 */
#ifndef TDI_RECEIVE_NORMAL
  #define TDI_RECEIVE_BROADCAST           0x00000004
  #define TDI_RECEIVE_MULTICAST           0x00000008
  #define TDI_RECEIVE_PARTIAL             0x00000010
  #define TDI_RECEIVE_NORMAL              0x00000020
  #define TDI_RECEIVE_EXPEDITED           0x00000040
  #define TDI_RECEIVE_PEEK                0x00000080
  #define TDI_RECEIVE_NO_RESPONSE_EXP     0x00000100
  #define TDI_RECEIVE_COPY_LOOKAHEAD      0x00000200
  #define TDI_RECEIVE_ENTIRE_MESSAGE      0x00000400
  #define TDI_RECEIVE_AT_DISPATCH_LEVEL   0x00000800
  #define TDI_RECEIVE_CONTROL_INFO        0x00001000
  #define TDI_RECEIVE_FORCE_INDICATION    0x00002000
  #define TDI_RECEIVE_NO_PUSH             0x00004000
#endif

/*
 * The "Auxiliary Function Driver" is the windows kernel-mode driver that does
 * TCP, UDP etc. Winsock is just a layer that dispatches requests to it.
 * Having these definitions allows us to bypass winsock and make an AFD kernel
 * call directly, avoiding a bug in winsock's recvfrom implementation.
 */

#define AFD_NO_FAST_IO   0x00000001
#define AFD_OVERLAPPED   0x00000002
#define AFD_IMMEDIATE    0x00000004

#define AFD_POLL_RECEIVE_BIT            0
#define AFD_POLL_RECEIVE                (1 << AFD_POLL_RECEIVE_BIT)
#define AFD_POLL_RECEIVE_EXPEDITED_BIT  1
#define AFD_POLL_RECEIVE_EXPEDITED      (1 << AFD_POLL_RECEIVE_EXPEDITED_BIT)
#define AFD_POLL_SEND_BIT               2
#define AFD_POLL_SEND                   (1 << AFD_POLL_SEND_BIT)
#define AFD_POLL_DISCONNECT_BIT         3
#define AFD_POLL_DISCONNECT             (1 << AFD_POLL_DISCONNECT_BIT)
#define AFD_POLL_ABORT_BIT              4
#define AFD_POLL_ABORT                  (1 << AFD_POLL_ABORT_BIT)
#define AFD_POLL_LOCAL_CLOSE_BIT        5
#define AFD_POLL_LOCAL_CLOSE            (1 << AFD_POLL_LOCAL_CLOSE_BIT)
#define AFD_POLL_CONNECT_BIT            6
#define AFD_POLL_CONNECT                (1 << AFD_POLL_CONNECT_BIT)
#define AFD_POLL_ACCEPT_BIT             7
#define AFD_POLL_ACCEPT                 (1 << AFD_POLL_ACCEPT_BIT)
#define AFD_POLL_CONNECT_FAIL_BIT       8
#define AFD_POLL_CONNECT_FAIL           (1 << AFD_POLL_CONNECT_FAIL_BIT)
#define AFD_POLL_QOS_BIT                9
#define AFD_POLL_QOS                    (1 << AFD_POLL_QOS_BIT)
#define AFD_POLL_GROUP_QOS_BIT          10
#define AFD_POLL_GROUP_QOS              (1 << AFD_POLL_GROUP_QOS_BIT)

#define AFD_NUM_POLL_EVENTS             11
#define AFD_POLL_ALL                    ((1 << AFD_NUM_POLL_EVENTS) - 1)

typedef struct _AFD_RECV_DATAGRAM_INFO {
    LPWSABUF BufferArray;
    ULONG BufferCount;
    ULONG AfdFlags;
    ULONG TdiFlags;
    struct sockaddr* Address;
    int* AddressLength;
} AFD_RECV_DATAGRAM_INFO, *PAFD_RECV_DATAGRAM_INFO;

typedef struct _AFD_RECV_INFO {
    LPWSABUF BufferArray;
    ULONG BufferCount;
    ULONG AfdFlags;
    ULONG TdiFlags;
} AFD_RECV_INFO, *PAFD_RECV_INFO;


#define _AFD_CONTROL_CODE(operation, method) \
    ((FSCTL_AFD_BASE) << 12 | (operation << 2) | method)

#define FSCTL_AFD_BASE FILE_DEVICE_NETWORK

#define AFD_RECEIVE            5
#define AFD_RECEIVE_DATAGRAM   6
#define AFD_POLL               9

#define IOCTL_AFD_RECEIVE \
    _AFD_CONTROL_CODE(AFD_RECEIVE, METHOD_NEITHER)

#define IOCTL_AFD_RECEIVE_DATAGRAM \
    _AFD_CONTROL_CODE(AFD_RECEIVE_DATAGRAM, METHOD_NEITHER)

#define IOCTL_AFD_POLL \
    _AFD_CONTROL_CODE(AFD_POLL, METHOD_BUFFERED)

int uv__convert_to_localhost_if_unspecified(const struct sockaddr* addr,
                                            struct sockaddr_storage* storage);

#endif /* UV_WIN_WINSOCK_H_ */
