/* Copyright libuv project contributors. All rights reserved.
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
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
# define S_IFDIR _S_IFDIR
#endif

int cookie1;
int cookie2;
int cookie3;


TEST_IMPL(handle_type_name) {
  ASSERT_OK(strcmp(uv_handle_type_name(UV_NAMED_PIPE), "pipe"));
  ASSERT_OK(strcmp(uv_handle_type_name(UV_UDP), "udp"));
  ASSERT_OK(strcmp(uv_handle_type_name(UV_FILE), "file"));
  ASSERT_NULL(uv_handle_type_name(UV_HANDLE_TYPE_MAX));
  ASSERT_NULL(uv_handle_type_name(UV_HANDLE_TYPE_MAX + 1));
  ASSERT_NULL(uv_handle_type_name(UV_UNKNOWN_HANDLE));
  return 0;
}


TEST_IMPL(req_type_name) {
  ASSERT_OK(strcmp(uv_req_type_name(UV_REQ), "req"));
  ASSERT_OK(strcmp(uv_req_type_name(UV_UDP_SEND), "udp_send"));
  ASSERT_OK(strcmp(uv_req_type_name(UV_WORK), "work"));
  ASSERT_NULL(uv_req_type_name(UV_REQ_TYPE_MAX));
  ASSERT_NULL(uv_req_type_name(UV_REQ_TYPE_MAX + 1));
  ASSERT_NULL(uv_req_type_name(UV_UNKNOWN_REQ));
  return 0;
}


TEST_IMPL(getters_setters) {
  uv_loop_t* loop;
  uv_pipe_t* pipe;
  uv_fs_t* fs;
  int r;

  loop = malloc(uv_loop_size());
  ASSERT_NOT_NULL(loop);
  r = uv_loop_init(loop);
  ASSERT_OK(r);

  uv_loop_set_data(loop, &cookie1);
  ASSERT_PTR_EQ(loop->data, &cookie1);
  ASSERT_PTR_EQ(uv_loop_get_data(loop), &cookie1);

  pipe = malloc(uv_handle_size(UV_NAMED_PIPE));
  r = uv_pipe_init(loop, pipe, 0);
  ASSERT_OK(r);
  ASSERT_EQ(uv_handle_get_type((uv_handle_t*)pipe), UV_NAMED_PIPE);

  ASSERT_PTR_EQ(uv_handle_get_loop((uv_handle_t*)pipe), loop);
  pipe->data = &cookie2;
  ASSERT_PTR_EQ(uv_handle_get_data((uv_handle_t*)pipe), &cookie2);
  uv_handle_set_data((uv_handle_t*)pipe, &cookie1);
  ASSERT_PTR_EQ(uv_handle_get_data((uv_handle_t*)pipe), &cookie1);
  ASSERT_PTR_EQ(pipe->data, &cookie1);

  ASSERT_OK(uv_stream_get_write_queue_size((uv_stream_t*)pipe));
  pipe->write_queue_size++;
  ASSERT_EQ(1, uv_stream_get_write_queue_size((uv_stream_t*)pipe));
  pipe->write_queue_size--;
  uv_close((uv_handle_t*)pipe, NULL);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  fs = malloc(uv_req_size(UV_FS));
  uv_fs_stat(loop, fs, ".", NULL);

  r = uv_run(loop, UV_RUN_DEFAULT);
  ASSERT_OK(r);

  ASSERT_EQ(uv_fs_get_type(fs), UV_FS_STAT);
  ASSERT_OK(uv_fs_get_result(fs));
  ASSERT_PTR_EQ(uv_fs_get_ptr(fs), uv_fs_get_statbuf(fs));
  ASSERT(uv_fs_get_statbuf(fs)->st_mode & S_IFDIR);
  ASSERT_OK(strcmp(uv_fs_get_path(fs), "."));
  uv_fs_req_cleanup(fs);

  r = uv_loop_close(loop);
  ASSERT_OK(r);

  free(pipe);
  free(fs);
  free(loop);
  return 0;
}
