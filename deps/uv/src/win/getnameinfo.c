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

#include <assert.h>
#include <stdio.h>

#include "uv.h"
#include "internal.h"
#include "req-inl.h"

#ifndef GetNameInfo
int WSAAPI GetNameInfoW(
  const SOCKADDR *pSockaddr,
  socklen_t SockaddrLength,
  PWCHAR pNodeBuffer,
  DWORD NodeBufferSize,
  PWCHAR pServiceBuffer,
  DWORD ServiceBufferSize,
  INT Flags
);
#endif

static void uv__getnameinfo_work(struct uv__work* w) {
  uv_getnameinfo_t* req;
  WCHAR host[NI_MAXHOST];
  WCHAR service[NI_MAXSERV];
  size_t size;
  int ret;

  req = container_of(w, uv_getnameinfo_t, work_req);
  if (GetNameInfoW((struct sockaddr*)&req->storage,
                   sizeof(req->storage),
                   host,
                   ARRAY_SIZE(host),
                   service,
                   ARRAY_SIZE(service),
                   req->flags)) {
    ret = WSAGetLastError();
    req->retcode = uv__getaddrinfo_translate_error(ret);
    return;
  }

  size = sizeof(req->host);
  ret = uv__copy_utf16_to_utf8(host, -1, req->host, &size);
  if (ret < 0) {
    req->retcode = ret;
    return;
  }

  size = sizeof(req->service);
  ret = uv__copy_utf16_to_utf8(service, -1, req->service, &size);
  if (ret < 0) {
    req->retcode = ret;
  }
}


/*
* Called from uv_run when complete.
*/
static void uv__getnameinfo_done(struct uv__work* w, int status) {
  uv_getnameinfo_t* req;
  char* host;
  char* service;

  req = container_of(w, uv_getnameinfo_t, work_req);
  uv__req_unregister(req->loop);
  host = service = NULL;

  if (status == UV_ECANCELED) {
    assert(req->retcode == 0);
    req->retcode = UV_EAI_CANCELED;
  } else if (req->retcode == 0) {
    host = req->host;
    service = req->service;
  }

  if (req->getnameinfo_cb)
    req->getnameinfo_cb(req, req->retcode, host, service);
}


/*
* Entry point for getnameinfo
* return 0 if a callback will be made
* return error code if validation fails
*/
int uv_getnameinfo(uv_loop_t* loop,
                   uv_getnameinfo_t* req,
                   uv_getnameinfo_cb getnameinfo_cb,
                   const struct sockaddr* addr,
                   int flags) {
  if (req == NULL || addr == NULL)
    return UV_EINVAL;

  if (addr->sa_family == AF_INET) {
    memcpy(&req->storage,
           addr,
           sizeof(struct sockaddr_in));
  } else if (addr->sa_family == AF_INET6) {
    memcpy(&req->storage,
           addr,
           sizeof(struct sockaddr_in6));
  } else {
    return UV_EINVAL;
  }

  UV_REQ_INIT(req, UV_GETNAMEINFO);
  uv__req_register(loop);

  req->getnameinfo_cb = getnameinfo_cb;
  req->flags = flags;
  req->loop = loop;
  req->retcode = 0;

  if (getnameinfo_cb) {
    uv__work_submit(loop,
                    &req->work_req,
                    UV__WORK_SLOW_IO,
                    uv__getnameinfo_work,
                    uv__getnameinfo_done);
    return 0;
  } else {
    uv__getnameinfo_work(&req->work_req);
    uv__getnameinfo_done(&req->work_req, 0);
    return req->retcode;
  }
}
