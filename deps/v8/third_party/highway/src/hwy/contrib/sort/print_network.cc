// Copyright 2021 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>

#include <vector>

#include "hwy/base.h"

// Based on A.7 in "Entwurf und Implementierung vektorisierter
// Sortieralgorithmen" and code by Mark Blacher.
void PrintMergeNetwork(int rows, int cols) {
  printf("\n%d x %d:\n", rows, cols);
  // Powers of two
  HWY_ASSERT(rows != 0 && (rows & (rows - 1)) == 0);
  HWY_ASSERT(cols != 0 && (cols & (cols - 1)) == 0);
  HWY_ASSERT(rows >= 4);
  HWY_ASSERT(cols >= 2);   // otherwise no cross-column merging required
  HWY_ASSERT(cols <= 16);  // SortTraits lacks Reverse32

  // Log(rows) times: sort half of the vectors with reversed groups of the
  // other half. Group size halves until we are sorting adjacent vectors.
  int group_size = rows;
  int num_groups = 1;
  for (; group_size >= 2; group_size /= 2, num_groups *= 2) {
    // All vectors except those being reversed. Allows us to group the
    // ReverseKeys and Sort2 operations, which is easier to read and may help
    // in-order machines with high-latency ReverseKeys.
    std::vector<int> all_vi;
    for (int group = 0; group < num_groups; ++group) {
      for (int i = 0; i < group_size / 2; ++i) {
        all_vi.push_back(group * group_size + i);
      }
    }
    for (int vi : all_vi) {
      const int vr = vi ^ (group_size - 1);
      printf("v%x = st.ReverseKeys%d(d, v%x);\n", vr, cols, vr);
    }
    for (int vi : all_vi) {
      const int vr = vi ^ (group_size - 1);
      printf("st.Sort2(d, v%x, v%x);\n", vi, vr);
    }
    printf("\n");
  }

  // Now merge across columns in all vectors.
  if (cols > 2) {
    for (int i = 0; i < rows; ++i) {
      printf("v%x = st.SortPairsReverse%d(d, v%x);\n", i, cols, i);
    }
    printf("\n");
  }
  if (cols >= 16) {
    for (int i = 0; i < rows; ++i) {
      printf("v%x = st.SortPairsDistance4(d, v%x);\n", i, i);
    }
    printf("\n");
  }
  if (cols >= 8) {
    for (int i = 0; i < rows; ++i) {
      printf("v%x = st.SortPairsDistance2(d, v%x);\n", i, i);
    }
    printf("\n");
  }
  for (int i = 0; i < rows; ++i) {
    printf("v%x = st.SortPairsDistance1(d, v%x);\n", i, i);
  }
  printf("\n");
}

int main(int argc, char** argv) {
  PrintMergeNetwork(8, 2);
  PrintMergeNetwork(8, 4);
  PrintMergeNetwork(16, 4);
  PrintMergeNetwork(16, 8);
  PrintMergeNetwork(16, 16);
  return 0;
}
