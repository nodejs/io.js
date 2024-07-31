/* Copyright (c) 2015 Saúl Ibarra Corretgé <saghul@gmail.com>.
 * All rights reserved.
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
#include <stdio.h>
#include <stdlib.h>


static int connection_cb_called = 0;
static int connect_cb_called = 0;

#define NUM_CLIENTS 10

typedef struct {
  uv_pipe_t pipe_handle;
  uv_connect_t conn_req;
} client_t;

static uv_pipe_t server_handle;
static client_t clients[NUM_CLIENTS];
static uv_pipe_t connections[NUM_CLIENTS];


static void connection_cb(uv_stream_t* server, int status) {
  int r;
  uv_pipe_t* conn;
  ASSERT_OK(status);

  conn = &connections[connection_cb_called];
  r = uv_pipe_init(server->loop, conn, 0);
  ASSERT_OK(r);

  r = uv_accept(server, (uv_stream_t*)conn);
  ASSERT_OK(r);

  if (++connection_cb_called == NUM_CLIENTS &&
      connect_cb_called == NUM_CLIENTS) {
    uv_stop(server->loop);
  }
}


static void connect_cb(uv_connect_t* connect_req, int status) {
  ASSERT_OK(status);
  if (++connect_cb_called == NUM_CLIENTS &&
      connection_cb_called == NUM_CLIENTS) {
    uv_stop(connect_req->handle->loop);
  }
}


TEST_IMPL(pipe_connect_multiple) {
#if defined(NO_SELF_CONNECT)
  RETURN_SKIP(NO_SELF_CONNECT);
#endif
  int i;
  int r;
  uv_loop_t* loop;

  loop = uv_default_loop();

  r = uv_pipe_init(loop, &server_handle, 0);
  ASSERT_OK(r);

  r = uv_pipe_bind(&server_handle, TEST_PIPENAME);
  ASSERT_OK(r);

  r = uv_listen((uv_stream_t*)&server_handle, 128, connection_cb);
  ASSERT_OK(r);

  for (i = 0; i < NUM_CLIENTS; i++) {
    r = uv_pipe_init(loop, &clients[i].pipe_handle, 0);
    ASSERT_OK(r);
    uv_pipe_connect(&clients[i].conn_req,
                    &clients[i].pipe_handle,
                    TEST_PIPENAME,
                    connect_cb);
  }

  uv_run(loop, UV_RUN_DEFAULT);

  ASSERT_EQ(connection_cb_called, NUM_CLIENTS);
  ASSERT_EQ(connect_cb_called, NUM_CLIENTS);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


static void connection_cb2(uv_stream_t* server, int status) {
  int r;
  uv_pipe_t* conn;
  ASSERT_OK(status);

  conn = &connections[connection_cb_called];
  r = uv_pipe_init(server->loop, conn, 0);
  ASSERT_OK(r);

  r = uv_accept(server, (uv_stream_t*)conn);
  ASSERT_OK(r);

  uv_close((uv_handle_t*)conn, NULL);
  if (++connection_cb_called == NUM_CLIENTS &&
      connect_cb_called == NUM_CLIENTS) {
    uv_close((uv_handle_t*)&server_handle, NULL);
  }
}

static void connect_cb2(uv_connect_t* connect_req, int status) {
  ASSERT_EQ(status, UV_ECANCELED);
  if (++connect_cb_called == NUM_CLIENTS &&
      connection_cb_called == NUM_CLIENTS) {
    uv_close((uv_handle_t*)&server_handle, NULL);
  }
}


TEST_IMPL(pipe_connect_close_multiple) {
#if defined(NO_SELF_CONNECT)
  RETURN_SKIP(NO_SELF_CONNECT);
#endif
  int i;
  int r;
  uv_loop_t* loop;

  loop = uv_default_loop();

  r = uv_pipe_init(loop, &server_handle, 0);
  ASSERT_OK(r);

  r = uv_pipe_bind(&server_handle, TEST_PIPENAME);
  ASSERT_OK(r);

  r = uv_listen((uv_stream_t*)&server_handle, 128, connection_cb2);
  ASSERT_OK(r);

  for (i = 0; i < NUM_CLIENTS; i++) {
    r = uv_pipe_init(loop, &clients[i].pipe_handle, 0);
    ASSERT_OK(r);
    uv_pipe_connect(&clients[i].conn_req,
                    &clients[i].pipe_handle,
                    TEST_PIPENAME,
                    connect_cb2);
  }

  for (i = 0; i < NUM_CLIENTS; i++) {
    uv_close((uv_handle_t*)&clients[i].pipe_handle, NULL);
  }


  uv_run(loop, UV_RUN_DEFAULT);

  ASSERT_EQ(connection_cb_called, NUM_CLIENTS);
  ASSERT_EQ(connect_cb_called, NUM_CLIENTS);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
