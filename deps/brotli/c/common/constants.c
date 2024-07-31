/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#include "constants.h"

const BrotliPrefixCodeRange
    _kBrotliPrefixCodeRanges[BROTLI_NUM_BLOCK_LEN_SYMBOLS] = {
        {1, 2},     {5, 2},     {9, 2},   {13, 2},    {17, 3},    {25, 3},
        {33, 3},    {41, 3},    {49, 4},  {65, 4},    {81, 4},    {97, 4},
        {113, 5},   {145, 5},   {177, 5}, {209, 5},   {241, 6},   {305, 6},
        {369, 7},   {497, 8},   {753, 9}, {1265, 10}, {2289, 11}, {4337, 12},
        {8433, 13}, {16625, 24}};
