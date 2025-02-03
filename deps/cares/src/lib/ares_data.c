/* MIT License
 *
 * Copyright (c) 2009 Daniel Stenberg
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ares_private.h"
#include <stddef.h>
#include <assert.h>
#include "ares_data.h"

/*
** ares_free_data() - c-ares external API function.
**
** This function must be used by the application to free data memory that
** has been internally allocated by some c-ares function and for which a
** pointer has already been returned to the calling application. The list
** of c-ares functions returning pointers that must be free'ed using this
** function is:
**
**   ares_get_servers()
**   ares_parse_srv_reply()
**   ares_parse_txt_reply()
*/

void ares_free_data(void *dataptr)
{
  while (dataptr != NULL) {
    struct ares_data *ptr;
    void             *next_data = NULL;

#ifdef __INTEL_COMPILER
#  pragma warning(push)
#  pragma warning(disable : 1684)
    /* 1684: conversion from pointer to same-sized integral type */
#endif

    ptr = (void *)((char *)dataptr - offsetof(struct ares_data, data));

#ifdef __INTEL_COMPILER
#  pragma warning(pop)
#endif

    if (ptr->mark != ARES_DATATYPE_MARK) {
      return;
    }

    switch (ptr->type) {
      case ARES_DATATYPE_MX_REPLY:
        next_data = ptr->data.mx_reply.next;
        ares_free(ptr->data.mx_reply.host);
        break;

      case ARES_DATATYPE_SRV_REPLY:
        next_data = ptr->data.srv_reply.next;
        ares_free(ptr->data.srv_reply.host);
        break;

      case ARES_DATATYPE_URI_REPLY:
        next_data = ptr->data.uri_reply.next;
        ares_free(ptr->data.uri_reply.uri);
        break;

      case ARES_DATATYPE_TXT_REPLY:
      case ARES_DATATYPE_TXT_EXT:
        next_data = ptr->data.txt_reply.next;
        ares_free(ptr->data.txt_reply.txt);
        break;

      case ARES_DATATYPE_ADDR_NODE:
        next_data = ptr->data.addr_node.next;
        break;

      case ARES_DATATYPE_ADDR_PORT_NODE:
        next_data = ptr->data.addr_port_node.next;
        break;

      case ARES_DATATYPE_NAPTR_REPLY:
        next_data = ptr->data.naptr_reply.next;
        ares_free(ptr->data.naptr_reply.flags);
        ares_free(ptr->data.naptr_reply.service);
        ares_free(ptr->data.naptr_reply.regexp);
        ares_free(ptr->data.naptr_reply.replacement);
        break;

      case ARES_DATATYPE_SOA_REPLY:
        ares_free(ptr->data.soa_reply.nsname);
        ares_free(ptr->data.soa_reply.hostmaster);
        break;

      case ARES_DATATYPE_CAA_REPLY:
        next_data = ptr->data.caa_reply.next;
        ares_free(ptr->data.caa_reply.property);
        ares_free(ptr->data.caa_reply.value);
        break;

      default:
        return;
    }

    ares_free(ptr);
    dataptr = next_data;
  }
}

/*
** ares_malloc_data() - c-ares internal helper function.
**
** This function allocates memory for a c-ares private ares_data struct
** for the specified ares_datatype, initializes c-ares private fields
** and zero initializes those which later might be used from the public
** API. It returns an interior pointer which can be passed by c-ares
** functions to the calling application, and that must be free'ed using
** c-ares external API function ares_free_data().
*/

void *ares_malloc_data(ares_datatype type)
{
  struct ares_data *ptr;

  ptr = ares_malloc_zero(sizeof(*ptr));
  if (!ptr) {
    return NULL;
  }

  switch (type) {
    case ARES_DATATYPE_MX_REPLY:
    case ARES_DATATYPE_SRV_REPLY:
    case ARES_DATATYPE_URI_REPLY:
    case ARES_DATATYPE_TXT_EXT:
    case ARES_DATATYPE_TXT_REPLY:
    case ARES_DATATYPE_CAA_REPLY:
    case ARES_DATATYPE_ADDR_NODE:
    case ARES_DATATYPE_ADDR_PORT_NODE:
    case ARES_DATATYPE_NAPTR_REPLY:
    case ARES_DATATYPE_SOA_REPLY:
      break;

    default:
      ares_free(ptr);
      return NULL;
  }

  ptr->mark = ARES_DATATYPE_MARK;
  ptr->type = type;

  return &ptr->data;
}
