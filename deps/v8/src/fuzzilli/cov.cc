// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/fuzzilli/cov.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "src/base/platform/memory.h"

#define SHM_SIZE 0x100000
#define MAX_EDGES ((SHM_SIZE - 4) * 8)

struct shmem_data {
  uint32_t num_edges;
  unsigned char edges[];
};

struct shmem_data* shmem;

uint32_t *edges_start, *edges_stop;
uint32_t builtins_start;
uint32_t builtins_edge_count;

void sanitizer_cov_reset_edgeguards() {
  uint32_t N = 0;
  for (uint32_t* x = edges_start; x < edges_stop && N < MAX_EDGES; x++)
    *x = ++N;
}

extern "C" void __sanitizer_cov_trace_pc_guard_init(uint32_t* start,
                                                    uint32_t* stop) {
  // We should initialize the shared memory region only once. We can initialize
  // it multiple times if it's the same region, which is something that appears
  // to happen on e.g. macOS. If we ever see a different region, we will likely
  // overwrite the previous one, which is probably not intended and as such we
  // fail with an error.
  if (shmem) {
    if (!(edges_start == start && edges_stop == stop)) {
      fprintf(stderr,
              "[COV] Multiple initialization of shmem!"
              " This is probably not intended! Currently only one edge"
              " region is supported\n");
      _exit(-1);
    }
    // Already initialized.
    return;
  }
  // Map the shared memory region
  const char* shm_key = getenv("SHM_ID");
  if (!shm_key) {
    puts("[COV] no shared memory bitmap available, skipping");
    shmem = (struct shmem_data*)v8::base::Malloc(SHM_SIZE);
  } else {
    int fd = shm_open(shm_key, O_RDWR, S_IREAD | S_IWRITE);
    if (fd <= -1) {
      fprintf(stderr, "[COV] Failed to open shared memory region\n");
      _exit(-1);
    }

    shmem = (struct shmem_data*)mmap(0, SHM_SIZE, PROT_READ | PROT_WRITE,
                                     MAP_SHARED, fd, 0);
    if (shmem == MAP_FAILED) {
      fprintf(stderr, "[COV] Failed to mmap shared memory region\n");
      _exit(-1);
    }
  }

  edges_start = start;
  edges_stop = stop;
  sanitizer_cov_reset_edgeguards();

  shmem->num_edges = static_cast<uint32_t>(stop - start);
  builtins_start = 1 + shmem->num_edges;
  printf("[COV] edge counters initialized. Shared memory: %s with %u edges\n",
         shm_key, shmem->num_edges);
}

uint32_t sanitizer_cov_count_discovered_edges() {
  uint32_t on_edges_counter = 0;
  for (uint32_t i = 1; i < builtins_start; ++i) {
    const uint32_t byteIndex = i >> 3;  // Divide by 8 using a shift operation
    const uint32_t bitIndex = i & 7;  // Modulo 8 using a bitwise AND operation

    if (shmem->edges[byteIndex] & (1 << bitIndex)) {
      ++on_edges_counter;
    }
  }
  return on_edges_counter;
}

extern "C" void __sanitizer_cov_trace_pc_guard(uint32_t* guard) {
  // There's a small race condition here: if this function executes in two
  // threads for the same edge at the same time, the first thread might disable
  // the edge (by setting the guard to zero) before the second thread fetches
  // the guard value (and thus the index). However, our instrumentation ignores
  // the first edge (see libcoverage.c) and so the race is unproblematic.
  uint32_t index = *guard;
  shmem->edges[index / 8] |= 1 << (index % 8);
  *guard = 0;
}

void cov_init_builtins_edges(uint32_t num_edges) {
  if (num_edges + shmem->num_edges > MAX_EDGES) {
    printf(
        "[COV] Error: Insufficient amount of edges left for builtins "
        "coverage.\n");
    exit(-1);
  }
  builtins_edge_count = num_edges;
  builtins_start = 1 + shmem->num_edges;
  shmem->num_edges += builtins_edge_count;
  printf("[COV] Additional %d edges for builtins initialized.\n", num_edges);
}

// This function is ran once per REPRL loop. In case of crash the coverage of
// crash will not be stored in shared memory. Therefore, it would be useful, if
// we could store these coverage information into shared memory in real time.
void cov_update_builtins_basic_block_coverage(
    const std::vector<bool>& cov_map) {
  if (cov_map.size() != builtins_edge_count) {
    printf("[COV] Error: Size of builtins cov map changed.\n");
    exit(-1);
  }
  for (uint32_t i = 0; i < cov_map.size(); ++i) {
    if (cov_map[i]) {
      const uint32_t byteIndex = (i + builtins_start) >> 3;
      const uint32_t bitIndex = (i + builtins_start) & 7;

      shmem->edges[byteIndex] |= (1 << bitIndex);
    }
  }
}
