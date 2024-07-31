/* Copyright The libuv project and contributors. All rights reserved.
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
#include <string.h>

union TestAddr {
    struct sockaddr addr;
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
};


TEST_IMPL(ip_name) {
    char dst[INET6_ADDRSTRLEN];
    union TestAddr test_addr;
    struct sockaddr* addr = &test_addr.addr;
    struct sockaddr_in* addr4 = &test_addr.addr4;
    struct sockaddr_in6* addr6 = &test_addr.addr6;

    /* test ip4_name */
    ASSERT_OK(uv_ip4_addr("192.168.0.1", TEST_PORT, addr4));
    ASSERT_OK(uv_ip4_name(addr4, dst, INET_ADDRSTRLEN));
    ASSERT_OK(strcmp("192.168.0.1", dst));

    ASSERT_OK(uv_ip_name(addr, dst, INET_ADDRSTRLEN));
    ASSERT_OK(strcmp("192.168.0.1", dst));

    /* test ip6_name */
    ASSERT_OK(uv_ip6_addr("fe80::2acf:daff:fedd:342a", TEST_PORT, addr6));
    ASSERT_OK(uv_ip6_name(addr6, dst, INET6_ADDRSTRLEN));
    ASSERT_OK(strcmp("fe80::2acf:daff:fedd:342a", dst));

    ASSERT_OK(uv_ip_name(addr, dst, INET6_ADDRSTRLEN));
    ASSERT_OK(strcmp("fe80::2acf:daff:fedd:342a", dst));

    /* test other sa_family */
    addr->sa_family = AF_UNIX;
    /* size is not a concern here */
    ASSERT_EQ(UV_EAFNOSUPPORT, uv_ip_name(addr, dst, INET6_ADDRSTRLEN));

    MAKE_VALGRIND_HAPPY(uv_default_loop());
    return 0;
}
