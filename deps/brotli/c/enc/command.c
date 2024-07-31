/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#include "command.h"

#include <brotli/types.h>

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

const uint32_t kBrotliInsBase[BROTLI_NUM_INS_COPY_CODES] = {
    0,  1,  2,  3,  4,   5,   6,   8,   10,   14,   18,   26,
    34, 50, 66, 98, 130, 194, 322, 578, 1090, 2114, 6210, 22594};
const uint32_t kBrotliInsExtra[BROTLI_NUM_INS_COPY_CODES] = {
    0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 9, 10, 12, 14, 24};
const uint32_t kBrotliCopyBase[BROTLI_NUM_INS_COPY_CODES] = {
    2,  3,  4,  5,  6,  7,   8,   9,   10,  12,  14,   18,
    22, 30, 38, 54, 70, 102, 134, 198, 326, 582, 1094, 2118};
const uint32_t kBrotliCopyExtra[BROTLI_NUM_INS_COPY_CODES] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 9, 10, 24};

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
