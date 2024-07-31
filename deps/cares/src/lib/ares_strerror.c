/* MIT License
 *
 * Copyright (c) 1998 Massachusetts Institute of Technology
 * Copyright (c) The c-ares project and its contributors
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

const char *ares_strerror(int code)
{
  ares_status_t status = (ares_status_t)code;
  switch (status) {
    case ARES_SUCCESS:
      return "Successful completion";
    case ARES_ENODATA:
      return "DNS server returned answer with no data";
    case ARES_EFORMERR:
      return "DNS server claims query was misformatted";
    case ARES_ESERVFAIL:
      return "DNS server returned general failure";
    case ARES_ENOTFOUND:
      return "Domain name not found";
    case ARES_ENOTIMP:
      return "DNS server does not implement requested operation";
    case ARES_EREFUSED:
      return "DNS server refused query";
    case ARES_EBADQUERY:
      return "Misformatted DNS query";
    case ARES_EBADNAME:
      return "Misformatted domain name";
    case ARES_EBADFAMILY:
      return "Unsupported address family";
    case ARES_EBADRESP:
      return "Misformatted DNS reply";
    case ARES_ECONNREFUSED:
      return "Could not contact DNS servers";
    case ARES_ETIMEOUT:
      return "Timeout while contacting DNS servers";
    case ARES_EOF:
      return "End of file";
    case ARES_EFILE:
      return "Error reading file";
    case ARES_ENOMEM:
      return "Out of memory";
    case ARES_EDESTRUCTION:
      return "Channel is being destroyed";
    case ARES_EBADSTR:
      return "Misformatted string";
    case ARES_EBADFLAGS:
      return "Illegal flags specified";
    case ARES_ENONAME:
      return "Given hostname is not numeric";
    case ARES_EBADHINTS:
      return "Illegal hints flags specified";
    case ARES_ENOTINITIALIZED:
      return "c-ares library initialization not yet performed";
    case ARES_ELOADIPHLPAPI:
      return "Error loading iphlpapi.dll";
    case ARES_EADDRGETNETWORKPARAMS:
      return "Could not find GetNetworkParams function";
    case ARES_ECANCELLED:
      return "DNS query cancelled";
    case ARES_ESERVICE:
      return "Invalid service name or number";
    case ARES_ENOSERVER:
      return "No DNS servers were configured";
  }

  return "unknown";
}
