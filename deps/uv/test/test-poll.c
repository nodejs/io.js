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

#include <errno.h>

#ifdef _WIN32
# include <fcntl.h>
# define close _close
#else
# include <sys/socket.h>
# include <unistd.h>
#endif

#include "uv.h"
#include "task.h"

#ifdef __linux__
# include <sys/epoll.h>
#endif

#ifdef UV_HAVE_KQUEUE
# include <sys/types.h>
# include <sys/event.h>
# include <sys/time.h>
#endif


#define NUM_CLIENTS 5
#define TRANSFER_BYTES (1 << 16)

#undef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b));


typedef enum {
  UNIDIRECTIONAL,
  DUPLEX
} test_mode_t;

typedef struct connection_context_s {
  uv_poll_t poll_handle;
  uv_timer_t timer_handle;
  uv_os_sock_t sock;
  size_t read, sent;
  int is_server_connection;
  int open_handles;
  int got_fin, sent_fin, got_disconnect;
  unsigned int events, delayed_events;
} connection_context_t;

typedef struct server_context_s {
  uv_poll_t poll_handle;
  uv_os_sock_t sock;
  int connections;
} server_context_t;


static void delay_timer_cb(uv_timer_t* timer);


static test_mode_t test_mode = DUPLEX;

static int closed_connections = 0;

static int valid_writable_wakeups = 0;
static int spurious_writable_wakeups = 0;

#if !defined(__sun) && !defined(_AIX) && !defined(__MVS__)
static int disconnects = 0;
#endif /* !__sun && !_AIX  && !__MVS__ */

static int got_eagain(void) {
#ifdef _WIN32
  return WSAGetLastError() == WSAEWOULDBLOCK;
#else
  return errno == EAGAIN
      || errno == EINPROGRESS
#ifdef EWOULDBLOCK
      || errno == EWOULDBLOCK;
#endif
      ;
#endif
}


static uv_os_sock_t create_bound_socket (struct sockaddr_in bind_addr) {
  uv_os_sock_t sock;
  int r;

  sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
#ifdef _WIN32
  ASSERT_NE(sock, INVALID_SOCKET);
#else
  ASSERT_GE(sock, 0);
#endif

#ifndef _WIN32
  {
    /* Allow reuse of the port. */
    int yes = 1;
    r = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    ASSERT_OK(r);
  }
#endif

  r = bind(sock, (const struct sockaddr*) &bind_addr, sizeof bind_addr);
  ASSERT_OK(r);

  return sock;
}


static void close_socket(uv_os_sock_t sock) {
  int r;
#ifdef _WIN32
  r = closesocket(sock);
#else
  r = close(sock);
#endif
  /* On FreeBSD close() can fail with ECONNRESET if the socket was shutdown by
   * the peer before all pending data was delivered.
   */
  ASSERT(r == 0 || errno == ECONNRESET);
}


static connection_context_t* create_connection_context(
    uv_os_sock_t sock, int is_server_connection) {
  int r;
  connection_context_t* context;

  context = (connection_context_t*) malloc(sizeof *context);
  ASSERT_NOT_NULL(context);

  context->sock = sock;
  context->is_server_connection = is_server_connection;
  context->read = 0;
  context->sent = 0;
  context->open_handles = 0;
  context->events = 0;
  context->delayed_events = 0;
  context->got_fin = 0;
  context->sent_fin = 0;
  context->got_disconnect = 0;

  r = uv_poll_init_socket(uv_default_loop(), &context->poll_handle, sock);
  context->open_handles++;
  context->poll_handle.data = context;
  ASSERT_OK(r);

  r = uv_timer_init(uv_default_loop(), &context->timer_handle);
  context->open_handles++;
  context->timer_handle.data = context;
  ASSERT_OK(r);

  return context;
}


static void connection_close_cb(uv_handle_t* handle) {
  connection_context_t* context = (connection_context_t*) handle->data;

  if (--context->open_handles == 0) {
    if (test_mode == DUPLEX || context->is_server_connection) {
      ASSERT_EQ(context->read, TRANSFER_BYTES);
    } else {
      ASSERT_OK(context->read);
    }

    if (test_mode == DUPLEX || !context->is_server_connection) {
      ASSERT_EQ(context->sent, TRANSFER_BYTES);
    } else {
      ASSERT_OK(context->sent);
    }

    closed_connections++;

    free(context);
  }
}


static void destroy_connection_context(connection_context_t* context) {
  uv_close((uv_handle_t*) &context->poll_handle, connection_close_cb);
  uv_close((uv_handle_t*) &context->timer_handle, connection_close_cb);
}


static void connection_poll_cb(uv_poll_t* handle, int status, int events) {
  connection_context_t* context = (connection_context_t*) handle->data;
  unsigned int new_events;
  int r;

  ASSERT_OK(status);
  ASSERT(events & context->events);
  ASSERT(!(events & ~context->events));

  new_events = context->events;

  if (events & UV_READABLE) {
    int action = rand() % 7;

    switch (action) {
      case 0:
      case 1: {
        /* Read a couple of bytes. */
        static char buffer[74];

        do
          r = recv(context->sock, buffer, sizeof buffer, 0);
        while (r == -1 && errno == EINTR);
        ASSERT_GE(r, 0);

        if (r > 0) {
          context->read += r;
        } else {
          /* Got FIN. */
          context->got_fin = 1;
          new_events &= ~UV_READABLE;
        }

        break;
      }

      case 2:
      case 3: {
        /* Read until EAGAIN. */
        static char buffer[931];

        for (;;) {
          do
            r = recv(context->sock, buffer, sizeof buffer, 0);
          while (r == -1 && errno == EINTR);

          if (r <= 0)
            break;

          context->read += r;
        }

        if (r == 0) {
          /* Got FIN. */
          context->got_fin = 1;
          new_events &= ~UV_READABLE;
        } else {
          ASSERT(got_eagain());
        }

        break;
      }

      case 4:
        /* Ignore. */
        break;

      case 5:
        /* Stop reading for a while. Restart in timer callback. */
        new_events &= ~UV_READABLE;
        if (!uv_is_active((uv_handle_t*) &context->timer_handle)) {
          context->delayed_events = UV_READABLE;
          uv_timer_start(&context->timer_handle, delay_timer_cb, 10, 0);
        } else {
          context->delayed_events |= UV_READABLE;
        }
        break;

      case 6:
        /* Fudge with the event mask. */
        uv_poll_start(&context->poll_handle, UV_WRITABLE, connection_poll_cb);
        uv_poll_start(&context->poll_handle, UV_READABLE, connection_poll_cb);
        context->events = UV_READABLE;
        break;

      default:
        ASSERT(0);
    }
  }

  if (events & UV_WRITABLE) {
    if (context->sent < TRANSFER_BYTES &&
        !(test_mode == UNIDIRECTIONAL && context->is_server_connection)) {
      /* We have to send more bytes. */
      int action = rand() % 7;

      switch (action) {
        case 0:
        case 1: {
          /* Send a couple of bytes. */
          static char buffer[103];

          int send_bytes = MIN(TRANSFER_BYTES - context->sent, sizeof buffer);
          ASSERT_GT(send_bytes, 0);

          do
            r = send(context->sock, buffer, send_bytes, 0);
          while (r == -1 && errno == EINTR);

          if (r < 0) {
            ASSERT(got_eagain());
            spurious_writable_wakeups++;
            break;
          }

          ASSERT_GT(r, 0);
          context->sent += r;
          valid_writable_wakeups++;
          break;
        }

        case 2:
        case 3: {
          /* Send until EAGAIN. */
          static char buffer[1234];

          int send_bytes = MIN(TRANSFER_BYTES - context->sent, sizeof buffer);
          ASSERT_GT(send_bytes, 0);

          do
            r = send(context->sock, buffer, send_bytes, 0);
          while (r == -1 && errno == EINTR);

          if (r < 0) {
            ASSERT(got_eagain());
            spurious_writable_wakeups++;
            break;
          }

          ASSERT_GT(r, 0);
          valid_writable_wakeups++;
          context->sent += r;

          while (context->sent < TRANSFER_BYTES) {
            send_bytes = MIN(TRANSFER_BYTES - context->sent, sizeof buffer);
            ASSERT_GT(send_bytes, 0);

            do
              r = send(context->sock, buffer, send_bytes, 0);
            while (r == -1 && errno == EINTR);
            ASSERT(r);

            if (r < 0) {
              ASSERT(got_eagain());
              break;
            }

            context->sent += r;
          }
          break;
        }

        case 4:
          /* Ignore. */
         break;

        case 5:
          /* Stop sending for a while. Restart in timer callback. */
          new_events &= ~UV_WRITABLE;
          if (!uv_is_active((uv_handle_t*) &context->timer_handle)) {
            context->delayed_events = UV_WRITABLE;
            uv_timer_start(&context->timer_handle, delay_timer_cb, 100, 0);
          } else {
            context->delayed_events |= UV_WRITABLE;
          }
          break;

        case 6:
          /* Fudge with the event mask. */
          uv_poll_start(&context->poll_handle,
                        UV_READABLE,
                        connection_poll_cb);
          uv_poll_start(&context->poll_handle,
                        UV_WRITABLE,
                        connection_poll_cb);
          context->events = UV_WRITABLE;
          break;

        default:
          ASSERT(0);
      }

    } else {
      /* Nothing more to write. Send FIN. */
      int r;
#ifdef _WIN32
      r = shutdown(context->sock, SD_SEND);
#else
      r = shutdown(context->sock, SHUT_WR);
#endif
      ASSERT_OK(r);
      context->sent_fin = 1;
      new_events &= ~UV_WRITABLE;
    }
  }
#if !defined(__sun) && !defined(_AIX) && !defined(__MVS__)
  if (events & UV_DISCONNECT) {
    context->got_disconnect = 1;
    ++disconnects;
    new_events &= ~UV_DISCONNECT;
  }

  if (context->got_fin && context->sent_fin && context->got_disconnect) {
#else /* __sun && _AIX  && __MVS__ */
  if (context->got_fin && context->sent_fin) {
#endif /* !__sun && !_AIX && !__MVS__  */
    /* Sent and received FIN. Close and destroy context. */
    close_socket(context->sock);
    destroy_connection_context(context);
    context->events = 0;

  } else if (new_events != context->events) {
    /* Poll mask changed. Call uv_poll_start again. */
    context->events = new_events;
    uv_poll_start(handle, new_events, connection_poll_cb);
  }

  /* Assert that uv_is_active works correctly for poll handles. */
  if (context->events != 0) {
    ASSERT_EQ(1, uv_is_active((uv_handle_t*) handle));
  } else {
    ASSERT_OK(uv_is_active((uv_handle_t*) handle));
  }
}


static void delay_timer_cb(uv_timer_t* timer) {
  connection_context_t* context = (connection_context_t*) timer->data;
  int r;

  /* Timer should auto stop. */
  ASSERT_OK(uv_is_active((uv_handle_t*) timer));

  /* Add the requested events to the poll mask. */
  ASSERT(context->delayed_events != 0);
  context->events |= context->delayed_events;
  context->delayed_events = 0;

  r = uv_poll_start(&context->poll_handle,
                    context->events,
                    connection_poll_cb);
  ASSERT_OK(r);
}


static server_context_t* create_server_context(
    uv_os_sock_t sock) {
  int r;
  server_context_t* context;

  context = (server_context_t*) malloc(sizeof *context);
  ASSERT_NOT_NULL(context);

  context->sock = sock;
  context->connections = 0;

  r = uv_poll_init_socket(uv_default_loop(), &context->poll_handle, sock);
  context->poll_handle.data = context;
  ASSERT_OK(r);

  return context;
}


static void server_close_cb(uv_handle_t* handle) {
  server_context_t* context = (server_context_t*) handle->data;
  free(context);
}


static void destroy_server_context(server_context_t* context) {
  uv_close((uv_handle_t*) &context->poll_handle, server_close_cb);
}


static void server_poll_cb(uv_poll_t* handle, int status, int events) {
  server_context_t* server_context = (server_context_t*)
                                          handle->data;
  connection_context_t* connection_context;
  struct sockaddr_in addr;
  socklen_t addr_len;
  uv_os_sock_t sock;
  int r;

  addr_len = sizeof addr;
  sock = accept(server_context->sock, (struct sockaddr*) &addr, &addr_len);
#ifdef _WIN32
  ASSERT_NE(sock, INVALID_SOCKET);
#else
  ASSERT_GE(sock, 0);
#endif

  connection_context = create_connection_context(sock, 1);
  connection_context->events = UV_READABLE | UV_WRITABLE | UV_DISCONNECT;
  r = uv_poll_start(&connection_context->poll_handle,
                    UV_READABLE | UV_WRITABLE | UV_DISCONNECT,
                    connection_poll_cb);
  ASSERT_OK(r);

  if (++server_context->connections == NUM_CLIENTS) {
    close_socket(server_context->sock);
    destroy_server_context(server_context);
  }
}


static void start_server(void) {
  server_context_t* context;
  struct sockaddr_in addr;
  uv_os_sock_t sock;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  sock = create_bound_socket(addr);
  context = create_server_context(sock);

  r = listen(sock, 100);
  ASSERT_OK(r);

  r = uv_poll_start(&context->poll_handle, UV_READABLE, server_poll_cb);
  ASSERT_OK(r);
}


static void start_client(void) {
  uv_os_sock_t sock;
  connection_context_t* context;
  struct sockaddr_in server_addr;
  struct sockaddr_in addr;
  int r;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &server_addr));
  ASSERT_OK(uv_ip4_addr("0.0.0.0", 0, &addr));

  sock = create_bound_socket(addr);
  context = create_connection_context(sock, 0);

  context->events = UV_READABLE | UV_WRITABLE | UV_DISCONNECT;
  r = uv_poll_start(&context->poll_handle,
                    UV_READABLE | UV_WRITABLE | UV_DISCONNECT,
                    connection_poll_cb);
  ASSERT_OK(r);

  r = connect(sock, (struct sockaddr*) &server_addr, sizeof server_addr);
  ASSERT(r == 0 || got_eagain());
}


static void start_poll_test(void) {
  int i, r;

#ifdef _WIN32
  {
    struct WSAData wsa_data;
    int r = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    ASSERT_OK(r);
  }
#endif

  start_server();

  for (i = 0; i < NUM_CLIENTS; i++)
    start_client();

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  /* Assert that at most five percent of the writable wakeups was spurious. */
  ASSERT_NE(spurious_writable_wakeups == 0 ||
            (valid_writable_wakeups + spurious_writable_wakeups) /
            spurious_writable_wakeups > 20, 0);

  ASSERT_EQ(closed_connections, NUM_CLIENTS * 2);
#if !defined(__sun) && !defined(_AIX) && !defined(__MVS__)
  ASSERT_EQ(disconnects, NUM_CLIENTS * 2);
#endif
  MAKE_VALGRIND_HAPPY(uv_default_loop());
}

 
/* Issuing a shutdown() on IBM i PASE with parameter SHUT_WR
 * also sends a normal close sequence to the partner program.
 * This leads to timing issues and ECONNRESET failures in the
 * test 'poll_duplex' and 'poll_unidirectional'.
 * 
 * https://www.ibm.com/support/knowledgecenter/en/ssw_ibm_i_74/apis/shutdn.htm
 */
TEST_IMPL(poll_duplex) {
#if defined(NO_SELF_CONNECT)
  RETURN_SKIP(NO_SELF_CONNECT);
#elif defined(__PASE__)
  RETURN_SKIP("API shutdown() may lead to timing issue on IBM i PASE");
#endif
  test_mode = DUPLEX;
  start_poll_test();
  return 0;
}


TEST_IMPL(poll_unidirectional) {
#if defined(NO_SELF_CONNECT)
  RETURN_SKIP(NO_SELF_CONNECT);
#elif defined(__PASE__)
  RETURN_SKIP("API shutdown() may lead to timing issue on IBM i PASE");
#endif
  test_mode = UNIDIRECTIONAL;
  start_poll_test();
  return 0;
}


/* Windows won't let you open a directory so we open a file instead.
 * OS X lets you poll a file so open the $PWD instead. Both fail
 * on Linux so it doesn't matter which one we pick. Both succeed
 * on Solaris and AIX so skip the test on those platforms.
 * On *BSD/Darwin, we disallow polling of regular files, directories.
 * In addition to regular files, we also disallow FIFOs on Darwin.
 */
#ifdef __APPLE__
#define TEST_POLL_FIFO_PATH "/tmp/uv-test-poll-fifo"
#endif
TEST_IMPL(poll_bad_fdtype) {
#if !defined(__sun) && \
    !defined(_AIX) && !defined(__MVS__) && \
    !defined(__CYGWIN__) && !defined(__MSYS__)
  uv_poll_t poll_handle;
  int fd[2];

#if defined(_WIN32)
  fd[0] = _open("test/fixtures/empty_file", UV_FS_O_RDONLY);
#else
  fd[0] = open(".", UV_FS_O_RDONLY);
#endif
  ASSERT_NE(fd[0], -1);
  ASSERT_NE(0, uv_poll_init(uv_default_loop(), &poll_handle, fd[0]));
  ASSERT_OK(close(fd[0]));
#if defined(__APPLE__)     || \
    defined(__DragonFly__) || \
    defined(__FreeBSD__)   || \
    defined(__OpenBSD__)   || \
    defined(__NetBSD__)
  fd[0] = open("test/fixtures/empty_file", UV_FS_O_RDONLY);
  ASSERT_NE(fd[0], -1);
  /* Regular files should be banned from kqueue. */
  ASSERT_NE(0, uv_poll_init(uv_default_loop(), &poll_handle, fd[0]));
  ASSERT_OK(close(fd[0]));
#ifdef __APPLE__
  ASSERT_OK(pipe(fd));
  /* Pipes should be permitted in kqueue. */
  ASSERT_EQ(0, uv_poll_init(uv_default_loop(), &poll_handle, fd[0]));
  ASSERT_OK(close(fd[0]));
  ASSERT_OK(close(fd[1]));

  ASSERT_OK(mkfifo(TEST_POLL_FIFO_PATH, 0600));
  fd[0] = open(TEST_POLL_FIFO_PATH, O_RDONLY | O_NONBLOCK);
  ASSERT_NE(fd[0], -1);
  fd[1] = open(TEST_POLL_FIFO_PATH, O_WRONLY | O_NONBLOCK);
  ASSERT_NE(fd[1], -1);
  /* FIFOs should be banned from kqueue. */
  ASSERT_NE(0, uv_poll_init(uv_default_loop(), &poll_handle, fd[0]));
  ASSERT_OK(close(fd[0]));
  ASSERT_OK(close(fd[1]));
  unlink(TEST_POLL_FIFO_PATH);
#endif
#endif
#endif

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


#ifdef __linux__
TEST_IMPL(poll_nested_epoll) {
  uv_poll_t poll_handle;
  int fd;

  fd = epoll_create(1);
  ASSERT_NE(fd, -1);

  ASSERT_OK(uv_poll_init(uv_default_loop(), &poll_handle, fd));
  ASSERT_OK(uv_poll_start(&poll_handle, UV_READABLE, (uv_poll_cb) abort));
  ASSERT_NE(0, uv_run(uv_default_loop(), UV_RUN_NOWAIT));

  uv_close((uv_handle_t*) &poll_handle, NULL);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_OK(close(fd));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
#endif  /* __linux__ */


#ifdef UV_HAVE_KQUEUE
TEST_IMPL(poll_nested_kqueue) {
  uv_poll_t poll_handle;
  int fd;

  fd = kqueue();
  ASSERT_NE(fd, -1);

  ASSERT_OK(uv_poll_init(uv_default_loop(), &poll_handle, fd));
  ASSERT_OK(uv_poll_start(&poll_handle, UV_READABLE, (uv_poll_cb) abort));
  ASSERT_NE(0, uv_run(uv_default_loop(), UV_RUN_NOWAIT));

  uv_close((uv_handle_t*) &poll_handle, NULL);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));
  ASSERT_OK(close(fd));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
#endif  /* UV_HAVE_KQUEUE */
