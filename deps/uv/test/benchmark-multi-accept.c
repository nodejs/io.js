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

#include "task.h"
#include "uv.h"

#define IPC_PIPE_NAME TEST_PIPENAME
#define NUM_CONNECTS  (250 * 1000)

union stream_handle {
  uv_pipe_t pipe;
  uv_tcp_t tcp;
};

/* Use as (uv_stream_t *) &handle_storage -- it's kind of clunky but it
 * avoids aliasing warnings.
 */
typedef unsigned char handle_storage_t[sizeof(union stream_handle)];

/* Used for passing around the listen handle, not part of the benchmark proper.
 * We have an overabundance of server types here. It works like this:
 *
 *  1. The main thread starts an IPC pipe server.
 *  2. The worker threads connect to the IPC server and obtain a listen handle.
 *  3. The worker threads start accepting requests on the listen handle.
 *  4. The main thread starts connecting repeatedly.
 *
 * Step #4 should perhaps be farmed out over several threads.
 */
struct ipc_server_ctx {
  handle_storage_t server_handle;
  unsigned int num_connects;
  uv_pipe_t ipc_pipe;
};

struct ipc_peer_ctx {
  handle_storage_t peer_handle;
  uv_write_t write_req;
};

struct ipc_client_ctx {
  uv_connect_t connect_req;
  uv_stream_t* server_handle;
  uv_pipe_t ipc_pipe;
  char scratch[16];
};

/* Used in the actual benchmark. */
struct server_ctx {
  handle_storage_t server_handle;
  unsigned int num_connects;
  uv_async_t async_handle;
  uv_thread_t thread_id;
  uv_sem_t semaphore;
};

struct client_ctx {
  handle_storage_t client_handle;
  unsigned int num_connects;
  uv_connect_t connect_req;
  uv_idle_t idle_handle;
};

static void ipc_connection_cb(uv_stream_t* ipc_pipe, int status);
static void ipc_write_cb(uv_write_t* req, int status);
static void ipc_close_cb(uv_handle_t* handle);
static void ipc_connect_cb(uv_connect_t* req, int status);
static void ipc_read_cb(uv_stream_t* handle,
                        ssize_t nread,
                        const uv_buf_t* buf);
static void ipc_alloc_cb(uv_handle_t* handle,
                         size_t suggested_size,
                         uv_buf_t* buf);

static void sv_async_cb(uv_async_t* handle);
static void sv_connection_cb(uv_stream_t* server_handle, int status);
static void sv_read_cb(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf);
static void sv_alloc_cb(uv_handle_t* handle,
                        size_t suggested_size,
                        uv_buf_t* buf);

static void cl_connect_cb(uv_connect_t* req, int status);
static void cl_idle_cb(uv_idle_t* handle);
static void cl_close_cb(uv_handle_t* handle);

static struct sockaddr_in listen_addr;


static void ipc_connection_cb(uv_stream_t* ipc_pipe, int status) {
  struct ipc_server_ctx* sc;
  struct ipc_peer_ctx* pc;
  uv_loop_t* loop;
  uv_buf_t buf;

  loop = ipc_pipe->loop;
  buf = uv_buf_init("PING", 4);
  sc = container_of(ipc_pipe, struct ipc_server_ctx, ipc_pipe);
  pc = calloc(1, sizeof(*pc));
  ASSERT_NOT_NULL(pc);

  if (ipc_pipe->type == UV_TCP)
    ASSERT_OK(uv_tcp_init(loop, (uv_tcp_t*) &pc->peer_handle));
  else if (ipc_pipe->type == UV_NAMED_PIPE)
    ASSERT_OK(uv_pipe_init(loop, (uv_pipe_t*) &pc->peer_handle, 1));
  else
    ASSERT(0);

  ASSERT_OK(uv_accept(ipc_pipe, (uv_stream_t*) &pc->peer_handle));
  ASSERT_OK(uv_write2(&pc->write_req,
                      (uv_stream_t*) &pc->peer_handle,
                      &buf,
                      1,
                      (uv_stream_t*) &sc->server_handle,
                      ipc_write_cb));

  if (--sc->num_connects == 0)
    uv_close((uv_handle_t*) ipc_pipe, NULL);
}


static void ipc_write_cb(uv_write_t* req, int status) {
  struct ipc_peer_ctx* ctx;
  ctx = container_of(req, struct ipc_peer_ctx, write_req);
  uv_close((uv_handle_t*) &ctx->peer_handle, ipc_close_cb);
}


static void ipc_close_cb(uv_handle_t* handle) {
  struct ipc_peer_ctx* ctx;
  ctx = container_of(handle, struct ipc_peer_ctx, peer_handle);
  free(ctx);
}


static void ipc_connect_cb(uv_connect_t* req, int status) {
  struct ipc_client_ctx* ctx;
  ctx = container_of(req, struct ipc_client_ctx, connect_req);
  ASSERT_OK(status);
  ASSERT_OK(uv_read_start((uv_stream_t*) &ctx->ipc_pipe,
                          ipc_alloc_cb,
                          ipc_read_cb));
}


static void ipc_alloc_cb(uv_handle_t* handle,
                         size_t suggested_size,
                         uv_buf_t* buf) {
  struct ipc_client_ctx* ctx;
  ctx = container_of(handle, struct ipc_client_ctx, ipc_pipe);
  buf->base = ctx->scratch;
  buf->len = sizeof(ctx->scratch);
}


static void ipc_read_cb(uv_stream_t* handle,
                        ssize_t nread,
                        const uv_buf_t* buf) {
  struct ipc_client_ctx* ctx;
  uv_loop_t* loop;
  uv_handle_type type;
  uv_pipe_t* ipc_pipe;

  ipc_pipe = (uv_pipe_t*) handle;
  ctx = container_of(ipc_pipe, struct ipc_client_ctx, ipc_pipe);
  loop = ipc_pipe->loop;

  ASSERT_EQ(1, uv_pipe_pending_count(ipc_pipe));
  type = uv_pipe_pending_type(ipc_pipe);
  if (type == UV_TCP)
    ASSERT_OK(uv_tcp_init(loop, (uv_tcp_t*) ctx->server_handle));
  else if (type == UV_NAMED_PIPE)
    ASSERT_OK(uv_pipe_init(loop, (uv_pipe_t*) ctx->server_handle, 0));
  else
    ASSERT(0);

  ASSERT_OK(uv_accept(handle, ctx->server_handle));
  uv_close((uv_handle_t*) &ctx->ipc_pipe, NULL);
}


/* Set up an IPC pipe server that hands out listen sockets to the worker
 * threads. It's kind of cumbersome for such a simple operation, maybe we
 * should revive uv_import() and uv_export().
 */
static void send_listen_handles(uv_handle_type type,
                                unsigned int num_servers,
                                struct server_ctx* servers) {
  struct ipc_server_ctx ctx;
  uv_loop_t* loop;
  unsigned int i;

  loop = uv_default_loop();
  ctx.num_connects = num_servers;

  if (type == UV_TCP) {
    ASSERT_OK(uv_tcp_init(loop, (uv_tcp_t*) &ctx.server_handle));
    ASSERT_OK(uv_tcp_bind((uv_tcp_t*) &ctx.server_handle,
                          (const struct sockaddr*) &listen_addr,
                          0));
  }
  else
    ASSERT(0);
  /* We need to initialize this pipe with ipc=0 - this is not a uv_pipe we'll
   * be sending handles over, it's just for listening for new connections.
   * If we accept a connection then the connected pipe must be initialized
   * with ipc=1.
   */
  ASSERT_OK(uv_pipe_init(loop, &ctx.ipc_pipe, 0));
  ASSERT_OK(uv_pipe_bind(&ctx.ipc_pipe, IPC_PIPE_NAME));
  ASSERT_OK(uv_listen((uv_stream_t*) &ctx.ipc_pipe, 128, ipc_connection_cb));

  for (i = 0; i < num_servers; i++)
    uv_sem_post(&servers[i].semaphore);

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
  uv_close((uv_handle_t*) &ctx.server_handle, NULL);
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  for (i = 0; i < num_servers; i++)
    uv_sem_wait(&servers[i].semaphore);
}


static void get_listen_handle(uv_loop_t* loop, uv_stream_t* server_handle) {
  struct ipc_client_ctx ctx;

  ctx.server_handle = server_handle;
  ctx.server_handle->data = "server handle";

  ASSERT_OK(uv_pipe_init(loop, &ctx.ipc_pipe, 1));
  uv_pipe_connect(&ctx.connect_req,
                  &ctx.ipc_pipe,
                  IPC_PIPE_NAME,
                  ipc_connect_cb);
  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
}


static void server_cb(void *arg) {
  struct server_ctx *ctx;
  uv_loop_t loop;

  ctx = arg;
  ASSERT_OK(uv_loop_init(&loop));

  ASSERT_OK(uv_async_init(&loop, &ctx->async_handle, sv_async_cb));
  uv_unref((uv_handle_t*) &ctx->async_handle);

  /* Wait until the main thread is ready. */
  uv_sem_wait(&ctx->semaphore);
  get_listen_handle(&loop, (uv_stream_t*) &ctx->server_handle);
  uv_sem_post(&ctx->semaphore);

  /* Now start the actual benchmark. */
  ASSERT_OK(uv_listen((uv_stream_t*) &ctx->server_handle,
                      128,
                      sv_connection_cb));
  ASSERT_OK(uv_run(&loop, UV_RUN_DEFAULT));

  uv_loop_close(&loop);
}


static void sv_async_cb(uv_async_t* handle) {
  struct server_ctx* ctx;
  ctx = container_of(handle, struct server_ctx, async_handle);
  uv_close((uv_handle_t*) &ctx->server_handle, NULL);
  uv_close((uv_handle_t*) &ctx->async_handle, NULL);
}


static void sv_connection_cb(uv_stream_t* server_handle, int status) {
  handle_storage_t* storage;
  struct server_ctx* ctx;

  ctx = container_of(server_handle, struct server_ctx, server_handle);
  ASSERT_OK(status);

  storage = malloc(sizeof(*storage));
  ASSERT_NOT_NULL(storage);

  if (server_handle->type == UV_TCP)
    ASSERT_OK(uv_tcp_init(server_handle->loop, (uv_tcp_t*) storage));
  else if (server_handle->type == UV_NAMED_PIPE)
    ASSERT_OK(uv_pipe_init(server_handle->loop, (uv_pipe_t*) storage, 0));
  else
    ASSERT(0);

  ASSERT_OK(uv_accept(server_handle, (uv_stream_t*) storage));
  ASSERT_OK(uv_read_start((uv_stream_t*) storage, sv_alloc_cb, sv_read_cb));
  ctx->num_connects++;
}


static void sv_alloc_cb(uv_handle_t* handle,
                        size_t suggested_size,
                        uv_buf_t* buf) {
  static char slab[32];
  buf->base = slab;
  buf->len = sizeof(slab);
}


static void sv_read_cb(uv_stream_t* handle,
                       ssize_t nread,
                       const uv_buf_t* buf) {
  ASSERT_EQ(nread, UV_EOF);
  uv_close((uv_handle_t*) handle, (uv_close_cb) free);
}


static void cl_connect_cb(uv_connect_t* req, int status) {
  struct client_ctx* ctx = container_of(req, struct client_ctx, connect_req);
  uv_idle_start(&ctx->idle_handle, cl_idle_cb);
  ASSERT_OK(status);
}


static void cl_idle_cb(uv_idle_t* handle) {
  struct client_ctx* ctx = container_of(handle, struct client_ctx, idle_handle);
  uv_close((uv_handle_t*) &ctx->client_handle, cl_close_cb);
  uv_idle_stop(&ctx->idle_handle);
}


static void cl_close_cb(uv_handle_t* handle) {
  struct client_ctx* ctx;

  ctx = container_of(handle, struct client_ctx, client_handle);

  if (--ctx->num_connects == 0) {
    uv_close((uv_handle_t*) &ctx->idle_handle, NULL);
    return;
  }

  ASSERT_OK(uv_tcp_init(handle->loop, (uv_tcp_t*) &ctx->client_handle));
  ASSERT_OK(uv_tcp_connect(&ctx->connect_req,
                           (uv_tcp_t*) &ctx->client_handle,
                           (const struct sockaddr*) &listen_addr,
                           cl_connect_cb));
}


static int test_tcp(unsigned int num_servers, unsigned int num_clients) {
  struct server_ctx* servers;
  struct client_ctx* clients;
  uv_loop_t* loop;
  uv_tcp_t* handle;
  unsigned int i;
  double time;

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &listen_addr));
  loop = uv_default_loop();

  servers = calloc(num_servers, sizeof(servers[0]));
  clients = calloc(num_clients, sizeof(clients[0]));
  ASSERT_NOT_NULL(servers);
  ASSERT_NOT_NULL(clients);

  /* We're making the assumption here that from the perspective of the
   * OS scheduler, threads are functionally equivalent to and interchangeable
   * with full-blown processes.
   */
  for (i = 0; i < num_servers; i++) {
    struct server_ctx* ctx = servers + i;
    ASSERT_OK(uv_sem_init(&ctx->semaphore, 0));
    ASSERT_OK(uv_thread_create(&ctx->thread_id, server_cb, ctx));
  }

  send_listen_handles(UV_TCP, num_servers, servers);

  for (i = 0; i < num_clients; i++) {
    struct client_ctx* ctx = clients + i;
    ctx->num_connects = NUM_CONNECTS / num_clients;
    handle = (uv_tcp_t*) &ctx->client_handle;
    handle->data = "client handle";
    ASSERT_OK(uv_tcp_init(loop, handle));
    ASSERT_OK(uv_tcp_connect(&ctx->connect_req,
                             handle,
                             (const struct sockaddr*) &listen_addr,
                             cl_connect_cb));
    ASSERT_OK(uv_idle_init(loop, &ctx->idle_handle));
  }

  {
    uint64_t t = uv_hrtime();
    ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));
    t = uv_hrtime() - t;
    time = t / 1e9;
  }

  for (i = 0; i < num_servers; i++) {
    struct server_ctx* ctx = servers + i;
    uv_async_send(&ctx->async_handle);
    ASSERT_OK(uv_thread_join(&ctx->thread_id));
    uv_sem_destroy(&ctx->semaphore);
  }

  printf("accept%u: %.0f accepts/sec (%u total)\n",
         num_servers,
         NUM_CONNECTS / time,
         NUM_CONNECTS);

  for (i = 0; i < num_servers; i++) {
    struct server_ctx* ctx = servers + i;
    printf("  thread #%u: %.0f accepts/sec (%u total, %.1f%%)\n",
           i,
           ctx->num_connects / time,
           ctx->num_connects,
           ctx->num_connects * 100.0 / NUM_CONNECTS);
  }

  free(clients);
  free(servers);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


BENCHMARK_IMPL(tcp_multi_accept2) {
  return test_tcp(2, 40);
}


BENCHMARK_IMPL(tcp_multi_accept4) {
  return test_tcp(4, 40);
}


BENCHMARK_IMPL(tcp_multi_accept8) {
  return test_tcp(8, 40);
}
