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

#include "uv.h"
#include "task.h"

#include <stdlib.h>
#include <string.h>


static uv_write_t write_req;
static uv_shutdown_t shutdown_req;
static uv_connect_t connect_req;

static char buffer[32767];

static int req_cb_called;
static int connect_cb_called;
static int write_cb_called;
static int shutdown_cb_called;
static int close_cb_called;


static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}


static void do_close(void* handle) {
  close_cb_called = 0;
  uv_close((uv_handle_t*)handle, close_cb);
  ASSERT_OK(close_cb_called);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_EQ(1, close_cb_called);
}


static void fail_cb(void) {
  FATAL("fail_cb should not have been called");
}


static void fail_cb2(void) {
  ASSERT(0 && "fail_cb2 should not have been called");
}


static void req_cb(uv_handle_t* req, int status) {
  req_cb_called++;
}


static void shutdown_cb(uv_shutdown_t* req, int status) {
  ASSERT_PTR_EQ(req, &shutdown_req);
  shutdown_cb_called++;
}


static void write_cb(uv_write_t* req, int status) {
  ASSERT_PTR_EQ(req, &write_req);
  uv_shutdown(&shutdown_req, req->handle, shutdown_cb);
  write_cb_called++;
}


static void connect_and_write(uv_connect_t* req, int status) {
  uv_buf_t buf = uv_buf_init(buffer, sizeof buffer);
  ASSERT_PTR_EQ(req, &connect_req);
  ASSERT_OK(status);
  uv_write(&write_req, req->handle, &buf, 1, write_cb);
  connect_cb_called++;
}



static void connect_and_shutdown(uv_connect_t* req, int status) {
  ASSERT_PTR_EQ(req, &connect_req);
  ASSERT_OK(status);
  uv_shutdown(&shutdown_req, req->handle, shutdown_cb);
  connect_cb_called++;
}


TEST_IMPL(ref) {
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(idle_ref) {
  uv_idle_t h;
  uv_idle_init(uv_default_loop(), &h);
  uv_idle_start(&h, (uv_idle_cb) fail_cb2);
  uv_unref((uv_handle_t*)&h);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  do_close(&h);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(async_ref) {
  uv_async_t h;
  uv_async_init(uv_default_loop(), &h, NULL);
  uv_unref((uv_handle_t*)&h);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  do_close(&h);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(prepare_ref) {
  uv_prepare_t h;
  uv_prepare_init(uv_default_loop(), &h);
  uv_prepare_start(&h, (uv_prepare_cb) fail_cb2);
  uv_unref((uv_handle_t*)&h);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  do_close(&h);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(check_ref) {
  uv_check_t h;
  uv_check_init(uv_default_loop(), &h);
  uv_check_start(&h, (uv_check_cb) fail_cb2);
  uv_unref((uv_handle_t*)&h);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  do_close(&h);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


static void prepare_cb(uv_prepare_t* h) {
  ASSERT_NOT_NULL(h);
  uv_unref((uv_handle_t*)h);
}


TEST_IMPL(unref_in_prepare_cb) {
  uv_prepare_t h;
  uv_prepare_init(uv_default_loop(), &h);
  uv_prepare_start(&h, prepare_cb);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  do_close(&h);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(timer_ref) {
  uv_timer_t h;
  uv_timer_init(uv_default_loop(), &h);
  uv_unref((uv_handle_t*)&h);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  do_close(&h);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(timer_ref2) {
  uv_timer_t h;
  uv_timer_init(uv_default_loop(), &h);
  uv_timer_start(&h, (uv_timer_cb)fail_cb, 42, 42);
  uv_unref((uv_handle_t*)&h);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  do_close(&h);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(fs_event_ref) {
#if defined(NO_FS_EVENTS)
  RETURN_SKIP(NO_FS_EVENTS);
#endif
  uv_fs_event_t h;
  uv_fs_event_init(uv_default_loop(), &h);
  uv_fs_event_start(&h, (uv_fs_event_cb)fail_cb, ".", 0);
  uv_unref((uv_handle_t*)&h);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  do_close(&h);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(fs_poll_ref) {
  uv_fs_poll_t h;
  uv_fs_poll_init(uv_default_loop(), &h);
  uv_fs_poll_start(&h, NULL, ".", 999);
  uv_unref((uv_handle_t*)&h);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  do_close(&h);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_ref) {
  uv_tcp_t h;
  uv_tcp_init(uv_default_loop(), &h);
  uv_unref((uv_handle_t*)&h);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  do_close(&h);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_ref2) {
  uv_tcp_t h;
  uv_tcp_init(uv_default_loop(), &h);
  uv_listen((uv_stream_t*)&h, 128, (uv_connection_cb)fail_cb);
  uv_unref((uv_handle_t*)&h);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  do_close(&h);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_ref2b) {
  uv_tcp_t h;
  uv_tcp_init(uv_default_loop(), &h);
  uv_listen((uv_stream_t*)&h, 128, (uv_connection_cb)fail_cb);
  uv_unref((uv_handle_t*)&h);
  uv_close((uv_handle_t*)&h, close_cb);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_EQ(1, close_cb_called);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_ref3) {
  struct sockaddr_in addr;
  uv_tcp_t h;
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  uv_tcp_init(uv_default_loop(), &h);
  uv_tcp_connect(&connect_req,
                 &h,
                 (const struct sockaddr*) &addr,
                 connect_and_shutdown);
  uv_unref((uv_handle_t*)&h);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_EQ(1, connect_cb_called);
  ASSERT_EQ(1, shutdown_cb_called);
  do_close(&h);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_ref4) {
  struct sockaddr_in addr;
  uv_tcp_t h;
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  uv_tcp_init(uv_default_loop(), &h);
  uv_tcp_connect(&connect_req,
                 &h,
                 (const struct sockaddr*) &addr,
                 connect_and_write);
  uv_unref((uv_handle_t*)&h);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_EQ(1, connect_cb_called);
  ASSERT_EQ(1, write_cb_called);
  ASSERT_EQ(1, shutdown_cb_called);
  do_close(&h);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(udp_ref) {
  uv_udp_t h;
  uv_udp_init(uv_default_loop(), &h);
  uv_unref((uv_handle_t*)&h);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  do_close(&h);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(udp_ref2) {
  struct sockaddr_in addr;
  uv_udp_t h;
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  uv_udp_init(uv_default_loop(), &h);
  uv_udp_bind(&h, (const struct sockaddr*) &addr, 0);
  uv_udp_recv_start(&h, (uv_alloc_cb)fail_cb, (uv_udp_recv_cb)fail_cb);
  uv_unref((uv_handle_t*)&h);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  do_close(&h);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(udp_ref3) {
  struct sockaddr_in addr;
  uv_buf_t buf = uv_buf_init("PING", 4);
  uv_udp_send_t req;
  uv_udp_t h;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  uv_udp_init(uv_default_loop(), &h);
  uv_udp_send(&req,
              &h,
              &buf,
              1,
              (const struct sockaddr*) &addr,
              (uv_udp_send_cb) req_cb);
  uv_unref((uv_handle_t*)&h);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_EQ(1, req_cb_called);
  do_close(&h);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(pipe_ref) {
  uv_pipe_t h;
  uv_pipe_init(uv_default_loop(), &h, 0);
  uv_unref((uv_handle_t*)&h);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  do_close(&h);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(pipe_ref2) {
  uv_pipe_t h;
  uv_pipe_init(uv_default_loop(), &h, 0);
  uv_listen((uv_stream_t*)&h, 128, (uv_connection_cb)fail_cb);
  uv_unref((uv_handle_t*)&h);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  do_close(&h);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(pipe_ref3) {
  uv_pipe_t h;
  uv_pipe_init(uv_default_loop(), &h, 0);
  uv_pipe_connect(&connect_req, &h, TEST_PIPENAME, connect_and_shutdown);
  uv_unref((uv_handle_t*)&h);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_EQ(1, connect_cb_called);
  ASSERT_EQ(1, shutdown_cb_called);
  do_close(&h);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(pipe_ref4) {
  uv_pipe_t h;
  uv_pipe_init(uv_default_loop(), &h, 0);
  uv_pipe_connect(&connect_req, &h, TEST_PIPENAME, connect_and_write);
  uv_unref((uv_handle_t*)&h);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_EQ(1, connect_cb_called);
  ASSERT_EQ(1, write_cb_called);
  ASSERT_EQ(1, shutdown_cb_called);
  do_close(&h);
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(process_ref) {
  /* spawn_helper4 blocks indefinitely. */
  char *argv[] = { NULL, "spawn_helper4", NULL };
  uv_process_options_t options;
  size_t exepath_size;
  char exepath[256];
  uv_process_t h;
  int r;

  memset(&options, 0, sizeof(options));
  exepath_size = sizeof(exepath);

  r = uv_exepath(exepath, &exepath_size);
  ASSERT_OK(r);

  argv[0] = exepath;
  options.file = exepath;
  options.args = argv;
  options.exit_cb = NULL;

  r = uv_spawn(uv_default_loop(), &h, &options);
  ASSERT_OK(r);

  uv_unref((uv_handle_t*)&h);
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  r = uv_process_kill(&h, /* SIGTERM */ 15);
  ASSERT_OK(r);

  do_close(&h);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(has_ref) {
  uv_idle_t h;
  uv_idle_init(uv_default_loop(), &h);
  uv_ref((uv_handle_t*)&h);
  ASSERT_EQ(1, uv_has_ref((uv_handle_t*)&h));
  uv_unref((uv_handle_t*)&h);
  ASSERT_OK(uv_has_ref((uv_handle_t*)&h));
  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
