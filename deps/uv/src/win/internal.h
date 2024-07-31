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

#ifndef UV_WIN_INTERNAL_H_
#define UV_WIN_INTERNAL_H_

#include "uv.h"
#include "../uv-common.h"

#include "uv/tree.h"
#include "winapi.h"
#include "winsock.h"

#ifdef _MSC_VER
# define INLINE __inline
# define UV_THREAD_LOCAL __declspec( thread )
#else
# define INLINE inline
# define UV_THREAD_LOCAL __thread
#endif


#ifdef _DEBUG

extern UV_THREAD_LOCAL int uv__crt_assert_enabled;

#define UV_BEGIN_DISABLE_CRT_ASSERT()                           \
  {                                                             \
    int uv__saved_crt_assert_enabled = uv__crt_assert_enabled;  \
    uv__crt_assert_enabled = FALSE;


#define UV_END_DISABLE_CRT_ASSERT()                             \
    uv__crt_assert_enabled = uv__saved_crt_assert_enabled;      \
  }

#else
#define UV_BEGIN_DISABLE_CRT_ASSERT()
#define UV_END_DISABLE_CRT_ASSERT()
#endif

/*
 * TCP
 */

typedef enum {
  UV__IPC_SOCKET_XFER_NONE = 0,
  UV__IPC_SOCKET_XFER_TCP_CONNECTION,
  UV__IPC_SOCKET_XFER_TCP_SERVER
} uv__ipc_socket_xfer_type_t;

typedef struct {
  WSAPROTOCOL_INFOW socket_info;
  uint32_t delayed_error;
} uv__ipc_socket_xfer_info_t;

int uv__tcp_listen(uv_tcp_t* handle, int backlog, uv_connection_cb cb);
int uv__tcp_accept(uv_tcp_t* server, uv_tcp_t* client);
int uv__tcp_read_start(uv_tcp_t* handle, uv_alloc_cb alloc_cb,
    uv_read_cb read_cb);
int uv__tcp_write(uv_loop_t* loop, uv_write_t* req, uv_tcp_t* handle,
    const uv_buf_t bufs[], unsigned int nbufs, uv_write_cb cb);
int uv__tcp_try_write(uv_tcp_t* handle, const uv_buf_t bufs[],
    unsigned int nbufs);

void uv__process_tcp_read_req(uv_loop_t* loop, uv_tcp_t* handle, uv_req_t* req);
void uv__process_tcp_write_req(uv_loop_t* loop, uv_tcp_t* handle,
    uv_write_t* req);
void uv__process_tcp_accept_req(uv_loop_t* loop, uv_tcp_t* handle,
    uv_req_t* req);
void uv__process_tcp_connect_req(uv_loop_t* loop, uv_tcp_t* handle,
    uv_connect_t* req);
void uv__process_tcp_shutdown_req(uv_loop_t* loop,
                                  uv_tcp_t* stream,
                                  uv_shutdown_t* req);

void uv__tcp_close(uv_loop_t* loop, uv_tcp_t* tcp);
void uv__tcp_endgame(uv_loop_t* loop, uv_tcp_t* handle);

int uv__tcp_xfer_export(uv_tcp_t* handle,
                        int pid,
                        uv__ipc_socket_xfer_type_t* xfer_type,
                        uv__ipc_socket_xfer_info_t* xfer_info);
int uv__tcp_xfer_import(uv_tcp_t* tcp,
                        uv__ipc_socket_xfer_type_t xfer_type,
                        uv__ipc_socket_xfer_info_t* xfer_info);


/*
 * UDP
 */
void uv__process_udp_recv_req(uv_loop_t* loop, uv_udp_t* handle, uv_req_t* req);
void uv__process_udp_send_req(uv_loop_t* loop, uv_udp_t* handle,
    uv_udp_send_t* req);

void uv__udp_close(uv_loop_t* loop, uv_udp_t* handle);
void uv__udp_endgame(uv_loop_t* loop, uv_udp_t* handle);


/*
 * Pipes
 */
int uv__create_stdio_pipe_pair(uv_loop_t* loop,
    uv_pipe_t* parent_pipe, HANDLE* child_pipe_ptr, unsigned int flags);

int uv__pipe_listen(uv_pipe_t* handle, int backlog, uv_connection_cb cb);
int uv__pipe_accept(uv_pipe_t* server, uv_stream_t* client);
int uv__pipe_read_start(uv_pipe_t* handle, uv_alloc_cb alloc_cb,
    uv_read_cb read_cb);
void uv__pipe_read_stop(uv_pipe_t* handle);
int uv__pipe_write(uv_loop_t* loop,
                   uv_write_t* req,
                   uv_pipe_t* handle,
                   const uv_buf_t bufs[],
                   size_t nbufs,
                   uv_stream_t* send_handle,
                   uv_write_cb cb);
void uv__pipe_shutdown(uv_loop_t* loop, uv_pipe_t* handle, uv_shutdown_t* req);

void uv__process_pipe_read_req(uv_loop_t* loop, uv_pipe_t* handle,
    uv_req_t* req);
void uv__process_pipe_write_req(uv_loop_t* loop, uv_pipe_t* handle,
    uv_write_t* req);
void uv__process_pipe_accept_req(uv_loop_t* loop, uv_pipe_t* handle,
    uv_req_t* raw_req);
void uv__process_pipe_connect_req(uv_loop_t* loop, uv_pipe_t* handle,
    uv_connect_t* req);
void uv__process_pipe_shutdown_req(uv_loop_t* loop, uv_pipe_t* handle,
    uv_shutdown_t* req);

void uv__pipe_close(uv_loop_t* loop, uv_pipe_t* handle);
void uv__pipe_endgame(uv_loop_t* loop, uv_pipe_t* handle);


/*
 * TTY
 */
void uv__console_init(void);

int uv__tty_read_start(uv_tty_t* handle, uv_alloc_cb alloc_cb,
    uv_read_cb read_cb);
int uv__tty_read_stop(uv_tty_t* handle);
int uv__tty_write(uv_loop_t* loop, uv_write_t* req, uv_tty_t* handle,
    const uv_buf_t bufs[], unsigned int nbufs, uv_write_cb cb);
int uv__tty_try_write(uv_tty_t* handle, const uv_buf_t bufs[],
    unsigned int nbufs);
void uv__tty_close(uv_tty_t* handle);

void uv__process_tty_read_req(uv_loop_t* loop, uv_tty_t* handle,
    uv_req_t* req);
void uv__process_tty_write_req(uv_loop_t* loop, uv_tty_t* handle,
    uv_write_t* req);
#define uv__process_tty_accept_req(loop, handle, req) abort()
#define uv__process_tty_connect_req(loop, handle, req) abort()
void uv__process_tty_shutdown_req(uv_loop_t* loop,
                                  uv_tty_t* stream,
                                  uv_shutdown_t* req);
void uv__tty_endgame(uv_loop_t* loop, uv_tty_t* handle);


/*
 * Poll watchers
 */
void uv__process_poll_req(uv_loop_t* loop, uv_poll_t* handle,
    uv_req_t* req);

int uv__poll_close(uv_loop_t* loop, uv_poll_t* handle);
void uv__poll_endgame(uv_loop_t* loop, uv_poll_t* handle);


/*
 * Loop watchers
 */
void uv__loop_watcher_endgame(uv_loop_t* loop, uv_handle_t* handle);

void uv__prepare_invoke(uv_loop_t* loop);
void uv__check_invoke(uv_loop_t* loop);
void uv__idle_invoke(uv_loop_t* loop);

void uv__once_init(void);


/*
 * Async watcher
 */
void uv__async_close(uv_loop_t* loop, uv_async_t* handle);
void uv__async_endgame(uv_loop_t* loop, uv_async_t* handle);

void uv__process_async_wakeup_req(uv_loop_t* loop, uv_async_t* handle,
    uv_req_t* req);


/*
 * Signal watcher
 */
void uv__signals_init(void);
int uv__signal_dispatch(int signum);

void uv__signal_close(uv_loop_t* loop, uv_signal_t* handle);
void uv__signal_endgame(uv_loop_t* loop, uv_signal_t* handle);

void uv__process_signal_req(uv_loop_t* loop, uv_signal_t* handle,
    uv_req_t* req);


/*
 * Spawn
 */
void uv__process_proc_exit(uv_loop_t* loop, uv_process_t* handle);
void uv__process_close(uv_loop_t* loop, uv_process_t* handle);
void uv__process_endgame(uv_loop_t* loop, uv_process_t* handle);


/*
 * FS
 */
void uv__fs_init(void);


/*
 * FS Event
 */
void uv__process_fs_event_req(uv_loop_t* loop, uv_req_t* req,
    uv_fs_event_t* handle);
void uv__fs_event_close(uv_loop_t* loop, uv_fs_event_t* handle);
void uv__fs_event_endgame(uv_loop_t* loop, uv_fs_event_t* handle);


/*
 * Stat poller.
 */
void uv__fs_poll_endgame(uv_loop_t* loop, uv_fs_poll_t* handle);


/*
 * Utilities.
 */
void uv__util_init(void);

uint64_t uv__hrtime(unsigned int scale);
__declspec(noreturn) void uv_fatal_error(const int errorno, const char* syscall);
int uv__convert_utf16_to_utf8(const WCHAR* utf16, size_t utf16len, char** utf8);
int uv__copy_utf16_to_utf8(const WCHAR* utf16, size_t utf16len, char* utf8, size_t *size);
int uv__convert_utf8_to_utf16(const char* utf8, WCHAR** utf16);

typedef int (WINAPI *uv__peersockfunc)(SOCKET, struct sockaddr*, int*);

int uv__getsockpeername(const uv_handle_t* handle,
                        uv__peersockfunc func,
                        struct sockaddr* name,
                        int* namelen,
                        int delayed_error);

int uv__random_rtlgenrandom(void* buf, size_t buflen);


/*
 * Process stdio handles.
 */
int uv__stdio_create(uv_loop_t* loop,
                     const uv_process_options_t* options,
                     BYTE** buffer_ptr);
void uv__stdio_destroy(BYTE* buffer);
void uv__stdio_noinherit(BYTE* buffer);
int uv__stdio_verify(BYTE* buffer, WORD size);
WORD uv__stdio_size(BYTE* buffer);
HANDLE uv__stdio_handle(BYTE* buffer, int fd);


/*
 * Winapi and ntapi utility functions
 */
void uv__winapi_init(void);


/*
 * Winsock utility functions
 */
void uv__winsock_init(void);

int uv__ntstatus_to_winsock_error(NTSTATUS status);

BOOL uv__get_acceptex_function(SOCKET socket, LPFN_ACCEPTEX* target);
BOOL uv__get_connectex_function(SOCKET socket, LPFN_CONNECTEX* target);

int WSAAPI uv__wsarecv_workaround(SOCKET socket, WSABUF* buffers,
    DWORD buffer_count, DWORD* bytes, DWORD* flags, WSAOVERLAPPED *overlapped,
    LPWSAOVERLAPPED_COMPLETION_ROUTINE completion_routine);
int WSAAPI uv__wsarecvfrom_workaround(SOCKET socket, WSABUF* buffers,
    DWORD buffer_count, DWORD* bytes, DWORD* flags, struct sockaddr* addr,
    int* addr_len, WSAOVERLAPPED *overlapped,
    LPWSAOVERLAPPED_COMPLETION_ROUTINE completion_routine);

int WSAAPI uv__msafd_poll(SOCKET socket, AFD_POLL_INFO* info_in,
    AFD_POLL_INFO* info_out, OVERLAPPED* overlapped);

/* Whether there are any non-IFS LSPs stacked on TCP */
extern int uv_tcp_non_ifs_lsp_ipv4;
extern int uv_tcp_non_ifs_lsp_ipv6;

/* Ip address used to bind to any port at any interface */
extern struct sockaddr_in uv_addr_ip4_any_;
extern struct sockaddr_in6 uv_addr_ip6_any_;

/*
 * Wake all loops with fake message
 */
void uv__wake_all_loops(void);

/*
 * Init system wake-up detection
 */
void uv__init_detect_system_wakeup(void);

#endif /* UV_WIN_INTERNAL_H_ */
