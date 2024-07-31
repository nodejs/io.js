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

#include "../src/strscpy.h"
#include "../src/strscpy.c"

TEST_IMPL(strscpy) {
  char d[4];

  ASSERT_OK(uv__strscpy(d, "", 0));
  ASSERT_OK(uv__strscpy(d, "x", 0));

  memset(d, 0, sizeof(d));
  ASSERT_EQ(1, uv__strscpy(d, "x", sizeof(d)));
  ASSERT_OK(memcmp(d, "x\0\0", sizeof(d)));

  memset(d, 0, sizeof(d));
  ASSERT_EQ(2, uv__strscpy(d, "xy", sizeof(d)));
  ASSERT_OK(memcmp(d, "xy\0", sizeof(d)));

  ASSERT_EQ(3, uv__strscpy(d, "xyz", sizeof(d)));
  ASSERT_OK(memcmp(d, "xyz", sizeof(d)));

  ASSERT_EQ(UV_E2BIG, uv__strscpy(d, "xyzz", sizeof(d)));
  ASSERT_OK(memcmp(d, "xyz", sizeof(d)));

  ASSERT_EQ(UV_E2BIG, uv__strscpy(d, "xyzzy", sizeof(d)));
  ASSERT_OK(memcmp(d, "xyz", sizeof(d)));

  return 0;
}
