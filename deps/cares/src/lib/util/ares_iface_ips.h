/* MIT License
 *
 * Copyright (c) 2023 Brad House
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
#ifndef __ARES__IFACE_IPS_H
#define __ARES__IFACE_IPS_H

/*! Flags for interface ip addresses. */
typedef enum {
  ARES_IFACE_IP_V4 = 1 << 0,        /*!< IPv4 address. During enumeration if
                                     *   this flag is set ARES_IFACE_IP_V6
                                     *   is not, will only enumerate v4
                                     *   addresses. */
  ARES_IFACE_IP_V6 = 1 << 1,        /*!< IPv6 address. During enumeration if
                                     *   this flag is set ARES_IFACE_IP_V4
                                     *   is not, will only enumerate v6
                                     *   addresses. */
  ARES_IFACE_IP_LOOPBACK  = 1 << 2, /*!< Loopback adapter */
  ARES_IFACE_IP_OFFLINE   = 1 << 3, /*!< Adapter offline */
  ARES_IFACE_IP_LINKLOCAL = 1 << 4, /*!< Link-local ip address */
  /*! Default, enumerate all ips for online interfaces, including loopback */
  ARES_IFACE_IP_DEFAULT = (ARES_IFACE_IP_V4 | ARES_IFACE_IP_V6 |
                           ARES_IFACE_IP_LOOPBACK | ARES_IFACE_IP_LINKLOCAL)
} ares_iface_ip_flags_t;

struct ares_iface_ips;

/*! Opaque pointer for holding enumerated interface ip addresses */
typedef struct ares_iface_ips ares_iface_ips_t;

/*! Destroy ip address enumeration created by ares_iface_ips().
 *
 *  \param[in]  ips   Initialized IP address enumeration structure
 */
void                          ares_iface_ips_destroy(ares_iface_ips_t *ips);

/*! Enumerate ip addresses on interfaces
 *
 *  \param[out]  ips   Returns initialized ip address structure
 *  \param[in]   flags Flags for enumeration
 *  \param[in]   name  Interface name to enumerate, or NULL to enumerate all
 *  \return ARES_ENOMEM on out of memory, ARES_ENOTIMP if not supported on
 *          the system, ARES_SUCCESS on success
 */
ares_status_t                 ares_iface_ips(ares_iface_ips_t    **ips,
                                             ares_iface_ip_flags_t flags, const char *name);

/*! Count of ips enumerated
 *
 * \param[in]  ips   Initialized IP address enumeration structure
 * \return count
 */
size_t                        ares_iface_ips_cnt(const ares_iface_ips_t *ips);

/*! Retrieve interface name
 *
 * \param[in]  ips   Initialized IP address enumeration structure
 * \param[in]  idx   Index of entry to pull
 * \return interface name
 */
const char *ares_iface_ips_get_name(const ares_iface_ips_t *ips, size_t idx);

/*! Retrieve interface address
 *
 * \param[in]  ips   Initialized IP address enumeration structure
 * \param[in]  idx   Index of entry to pull
 * \return interface address
 */
const struct ares_addr *ares_iface_ips_get_addr(const ares_iface_ips_t *ips,
                                                size_t                  idx);

/*! Retrieve interface address flags
 *
 * \param[in]  ips   Initialized IP address enumeration structure
 * \param[in]  idx   Index of entry to pull
 * \return interface address flags
 */
ares_iface_ip_flags_t   ares_iface_ips_get_flags(const ares_iface_ips_t *ips,
                                                 size_t                  idx);

/*! Retrieve interface address netmask
 *
 * \param[in]  ips   Initialized IP address enumeration structure
 * \param[in]  idx   Index of entry to pull
 * \return interface address netmask
 */
unsigned char           ares_iface_ips_get_netmask(const ares_iface_ips_t *ips,
                                                   size_t                  idx);

/*! Retrieve interface ipv6 link local scope
 *
 * \param[in]  ips   Initialized IP address enumeration structure
 * \param[in]  idx   Index of entry to pull
 * \return interface ipv6 link local scope
 */
unsigned int            ares_iface_ips_get_ll_scope(const ares_iface_ips_t *ips,
                                                    size_t                  idx);


/*! Retrieve the interface index (aka link local scope) from the interface
 *  name.
 *
 * \param[in] name  Interface name
 * \return 0 on failure, index otherwise
 */
unsigned int            ares_os_if_nametoindex(const char *name);

/*! Retrieves the interface name from the index (aka link local scope)
 *
 * \param[in] index  Interface index (> 0)
 * \param[in] name   Buffer to hold name
 * \param[in] name_len Length of provided buffer, must be at least IF_NAMESIZE
 * \return NULL on failure, or pointer to name on success
 */
const char             *ares_os_if_indextoname(unsigned int index, char *name,
                                               size_t name_len);

#endif
