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

#include "uv.h"
#include "internal.h"
#include "req-inl.h"
#include "idna.h"

/* EAI_* constants. */
#include <winsock2.h>

/* Needed for ConvertInterfaceIndexToLuid and ConvertInterfaceLuidToNameA */
#include <iphlpapi.h>

int uv__getaddrinfo_translate_error(int sys_err) {
  switch (sys_err) {
    case 0:                       return 0;
    case WSATRY_AGAIN:            return UV_EAI_AGAIN;
    case WSAEINVAL:               return UV_EAI_BADFLAGS;
    case WSANO_RECOVERY:          return UV_EAI_FAIL;
    case WSAEAFNOSUPPORT:         return UV_EAI_FAMILY;
    case WSA_NOT_ENOUGH_MEMORY:   return UV_EAI_MEMORY;
    case WSAHOST_NOT_FOUND:       return UV_EAI_NONAME;
    case WSATYPE_NOT_FOUND:       return UV_EAI_SERVICE;
    case WSAESOCKTNOSUPPORT:      return UV_EAI_SOCKTYPE;
    default:                      return uv_translate_sys_error(sys_err);
  }
}


/*
 * MinGW is missing this
 */
#if !defined(_MSC_VER) && !defined(__MINGW64_VERSION_MAJOR)
  typedef struct addrinfoW {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    size_t ai_addrlen;
    WCHAR* ai_canonname;
    struct sockaddr* ai_addr;
    struct addrinfoW* ai_next;
  } ADDRINFOW, *PADDRINFOW;

  DECLSPEC_IMPORT int WSAAPI GetAddrInfoW(const WCHAR* node,
                                          const WCHAR* service,
                                          const ADDRINFOW* hints,
                                          PADDRINFOW* result);

  DECLSPEC_IMPORT void WSAAPI FreeAddrInfoW(PADDRINFOW pAddrInfo);
#endif


/* Adjust size value to be multiple of 4. Use to keep pointer aligned.
 * Do we need different versions of this for different architectures? */
#define ALIGNED_SIZE(X)     ((((X) + 3) >> 2) << 2)

#ifndef NDIS_IF_MAX_STRING_SIZE
#define NDIS_IF_MAX_STRING_SIZE IF_MAX_STRING_SIZE
#endif

static void uv__getaddrinfo_work(struct uv__work* w) {
  uv_getaddrinfo_t* req;
  struct addrinfoW* hints;
  int err;

  req = container_of(w, uv_getaddrinfo_t, work_req);
  hints = req->addrinfow;
  req->addrinfow = NULL;
  err = GetAddrInfoW(req->node, req->service, hints, &req->addrinfow);
  req->retcode = uv__getaddrinfo_translate_error(err);
}


/*
 * Called from uv_run when complete. Call user specified callback
 * then free returned addrinfo
 * Returned addrinfo strings are converted from UTF-16 to UTF-8.
 *
 * To minimize allocation we calculate total size required,
 * and copy all structs and referenced strings into the one block.
 * Each size calculation is adjusted to avoid unaligned pointers.
 */
static void uv__getaddrinfo_done(struct uv__work* w, int status) {
  uv_getaddrinfo_t* req;
  size_t addrinfo_len = 0;
  ssize_t name_len = 0;
  size_t addrinfo_struct_len = ALIGNED_SIZE(sizeof(struct addrinfo));
  struct addrinfoW* addrinfow_ptr;
  struct addrinfo* addrinfo_ptr;
  char* alloc_ptr = NULL;
  char* cur_ptr = NULL;
  int r;

  req = container_of(w, uv_getaddrinfo_t, work_req);

  /* release input parameter memory */
  uv__free(req->alloc);
  req->alloc = NULL;

  if (status == UV_ECANCELED) {
    assert(req->retcode == 0);
    req->retcode = UV_EAI_CANCELED;
    goto complete;
  }

  if (req->retcode == 0) {
    /* Convert addrinfoW to addrinfo. First calculate required length. */
    addrinfow_ptr = req->addrinfow;
    while (addrinfow_ptr != NULL) {
      addrinfo_len += addrinfo_struct_len +
          ALIGNED_SIZE(addrinfow_ptr->ai_addrlen);
      if (addrinfow_ptr->ai_canonname != NULL) {
        name_len = uv_utf16_length_as_wtf8(addrinfow_ptr->ai_canonname, -1);
        if (name_len < 0) {
          req->retcode = name_len;
          goto complete;
        }
        addrinfo_len += ALIGNED_SIZE(name_len + 1);
      }
      addrinfow_ptr = addrinfow_ptr->ai_next;
    }

    /* allocate memory for addrinfo results */
    alloc_ptr = (char*)uv__malloc(addrinfo_len);

    /* do conversions */
    if (alloc_ptr != NULL) {
      cur_ptr = alloc_ptr;
      addrinfow_ptr = req->addrinfow;

      while (addrinfow_ptr != NULL) {
        /* copy addrinfo struct data */
        assert(cur_ptr + addrinfo_struct_len <= alloc_ptr + addrinfo_len);
        addrinfo_ptr = (struct addrinfo*)cur_ptr;
        addrinfo_ptr->ai_family = addrinfow_ptr->ai_family;
        addrinfo_ptr->ai_socktype = addrinfow_ptr->ai_socktype;
        addrinfo_ptr->ai_protocol = addrinfow_ptr->ai_protocol;
        addrinfo_ptr->ai_flags = addrinfow_ptr->ai_flags;
        addrinfo_ptr->ai_addrlen = addrinfow_ptr->ai_addrlen;
        addrinfo_ptr->ai_canonname = NULL;
        addrinfo_ptr->ai_addr = NULL;
        addrinfo_ptr->ai_next = NULL;

        cur_ptr += addrinfo_struct_len;

        /* copy sockaddr */
        if (addrinfo_ptr->ai_addrlen > 0) {
          assert(cur_ptr + addrinfo_ptr->ai_addrlen <=
                 alloc_ptr + addrinfo_len);
          memcpy(cur_ptr, addrinfow_ptr->ai_addr, addrinfo_ptr->ai_addrlen);
          addrinfo_ptr->ai_addr = (struct sockaddr*)cur_ptr;
          cur_ptr += ALIGNED_SIZE(addrinfo_ptr->ai_addrlen);
        }

        /* convert canonical name to UTF-8 */
        if (addrinfow_ptr->ai_canonname != NULL) {
          name_len = alloc_ptr + addrinfo_len - cur_ptr;
          r = uv__copy_utf16_to_utf8(addrinfow_ptr->ai_canonname,
                                     -1,
                                     cur_ptr,
                                     (size_t*)&name_len);
          assert(r == 0);
          addrinfo_ptr->ai_canonname = cur_ptr;
          cur_ptr += ALIGNED_SIZE(name_len + 1);
        }
        assert(cur_ptr <= alloc_ptr + addrinfo_len);

        /* set next ptr */
        addrinfow_ptr = addrinfow_ptr->ai_next;
        if (addrinfow_ptr != NULL) {
          addrinfo_ptr->ai_next = (struct addrinfo*)cur_ptr;
        }
      }
      req->addrinfo = (struct addrinfo*)alloc_ptr;
    } else {
      req->retcode = UV_EAI_MEMORY;
    }
  }

  /* return memory to system */
  if (req->addrinfow != NULL) {
    FreeAddrInfoW(req->addrinfow);
    req->addrinfow = NULL;
  }

complete:
  uv__req_unregister(req->loop, req);

  /* finally do callback with converted result */
  if (req->getaddrinfo_cb)
    req->getaddrinfo_cb(req, req->retcode, req->addrinfo);
}


void uv_freeaddrinfo(struct addrinfo* ai) {
  char* alloc_ptr = (char*)ai;

  /* release copied result memory */
  uv__free(alloc_ptr);
}


/*
 * Entry point for getaddrinfo
 * we convert the UTF-8 strings to UNICODE
 * and save the UNICODE string pointers in the req
 * We also copy hints so that caller does not need to keep memory until the
 * callback.
 * return 0 if a callback will be made
 * return error code if validation fails
 *
 * To minimize allocation we calculate total size required,
 * and copy all structs and referenced strings into the one block.
 * Each size calculation is adjusted to avoid unaligned pointers.
 */
int uv_getaddrinfo(uv_loop_t* loop,
                   uv_getaddrinfo_t* req,
                   uv_getaddrinfo_cb getaddrinfo_cb,
                   const char* node,
                   const char* service,
                   const struct addrinfo* hints) {
  char hostname_ascii[256];
  size_t nodesize = 0;
  size_t servicesize = 0;
  size_t hintssize = 0;
  char* alloc_ptr = NULL;
  ssize_t rc;

  if (req == NULL || (node == NULL && service == NULL)) {
    return UV_EINVAL;
  }

  UV_REQ_INIT(req, UV_GETADDRINFO);
  req->getaddrinfo_cb = getaddrinfo_cb;
  req->addrinfo = NULL;
  req->loop = loop;
  req->retcode = 0;

  /* calculate required memory size for all input values */
  if (node != NULL) {
    rc = uv__idna_toascii(node,
                          node + strlen(node),
                          hostname_ascii,
                          hostname_ascii + sizeof(hostname_ascii));
    if (rc < 0)
      return rc;
    nodesize = strlen(hostname_ascii) + 1;
    node = hostname_ascii;
  }

  if (service != NULL) {
    rc = uv_wtf8_length_as_utf16(service);
    if (rc < 0)
       return rc;
    servicesize = rc;
  }
  if (hints != NULL) {
    hintssize = ALIGNED_SIZE(sizeof(struct addrinfoW));
  }

  /* allocate memory for inputs, and partition it as needed */
  alloc_ptr = uv__malloc(ALIGNED_SIZE(nodesize * sizeof(WCHAR)) +
                         ALIGNED_SIZE(servicesize * sizeof(WCHAR)) +
                         hintssize);
  if (!alloc_ptr)
    return UV_ENOMEM;

  /* save alloc_ptr now so we can free if error */
  req->alloc = (void*) alloc_ptr;

  /* Convert node string to UTF16 into allocated memory and save pointer in the
   * request. The node here has been converted to ascii. */
  if (node != NULL) {
    req->node = (WCHAR*) alloc_ptr;
    uv_wtf8_to_utf16(node, (WCHAR*) alloc_ptr, nodesize);
    alloc_ptr += ALIGNED_SIZE(nodesize * sizeof(WCHAR));
  } else {
    req->node = NULL;
  }

  /* Convert service string to UTF16 into allocated memory and save pointer in
   * the req. */
  if (service != NULL) {
    req->service = (WCHAR*) alloc_ptr;
    uv_wtf8_to_utf16(service, (WCHAR*) alloc_ptr, servicesize);
    alloc_ptr += ALIGNED_SIZE(servicesize * sizeof(WCHAR));
  } else {
    req->service = NULL;
  }

  /* copy hints to allocated memory and save pointer in req */
  if (hints != NULL) {
    req->addrinfow = (struct addrinfoW*) alloc_ptr;
    req->addrinfow->ai_family = hints->ai_family;
    req->addrinfow->ai_socktype = hints->ai_socktype;
    req->addrinfow->ai_protocol = hints->ai_protocol;
    req->addrinfow->ai_flags = hints->ai_flags;
    req->addrinfow->ai_addrlen = 0;
    req->addrinfow->ai_canonname = NULL;
    req->addrinfow->ai_addr = NULL;
    req->addrinfow->ai_next = NULL;
  } else {
    req->addrinfow = NULL;
  }

  uv__req_register(loop, req);

  if (getaddrinfo_cb) {
    uv__work_submit(loop,
                    &req->work_req,
                    UV__WORK_SLOW_IO,
                    uv__getaddrinfo_work,
                    uv__getaddrinfo_done);
    return 0;
  } else {
    uv__getaddrinfo_work(&req->work_req);
    uv__getaddrinfo_done(&req->work_req, 0);
    return req->retcode;
  }
}

int uv_if_indextoname(unsigned int ifindex, char* buffer, size_t* size) {
  NET_LUID luid;
  wchar_t wname[NDIS_IF_MAX_STRING_SIZE + 1]; /* Add one for the NUL. */
  int r;

  if (buffer == NULL || size == NULL || *size == 0)
    return UV_EINVAL;

  r = ConvertInterfaceIndexToLuid(ifindex, &luid);

  if (r != 0)
    return uv_translate_sys_error(r);

  r = ConvertInterfaceLuidToNameW(&luid, wname, ARRAY_SIZE(wname));

  if (r != 0)
    return uv_translate_sys_error(r);

  return uv__copy_utf16_to_utf8(wname, -1, buffer, size);
}

int uv_if_indextoiid(unsigned int ifindex, char* buffer, size_t* size) {
  int r;

  if (buffer == NULL || size == NULL || *size == 0)
    return UV_EINVAL;

  r = snprintf(buffer, *size, "%d", ifindex);

  if (r < 0)
    return uv_translate_sys_error(r);

  if (r >= (int) *size) {
    *size = r + 1;
    return UV_ENOBUFS;
  }

  *size = r;
  return 0;
}
