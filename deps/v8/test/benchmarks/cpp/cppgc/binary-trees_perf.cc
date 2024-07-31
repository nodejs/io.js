// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "include/cppgc/allocation.h"
#include "include/cppgc/garbage-collected.h"
#include "include/cppgc/heap.h"
#include "include/cppgc/persistent.h"
#include "include/cppgc/visitor.h"
#include "src/base/macros.h"
#include "src/heap/cppgc/object-allocator.h"
#include "test/benchmarks/cpp/cppgc/benchmark_utils.h"
#include "third_party/google_benchmark_chrome/src/include/benchmark/benchmark.h"

namespace {

// Implementation of the binary trees benchmark of the computer language
// benchmarks game. See
//   https://benchmarksgame-team.pages.debian.net/benchmarksgame/performance/binarytrees.html

class BinaryTrees : public cppgc::internal::testing::BenchmarkWithHeap {
 public:
  BinaryTrees() { Iterations(1); }
};

class TreeNode final : public cppgc::GarbageCollected<TreeNode> {
 public:
  void Trace(cppgc::Visitor* visitor) const {
    visitor->Trace(left_);
    visitor->Trace(right_);
  }

  const TreeNode* left() const { return left_; }
  void set_left(TreeNode* node) { left_ = node; }
  const TreeNode* right() const { return right_; }
  void set_right(TreeNode* node) { right_ = node; }

  size_t Check() const {
    return left() ? left()->Check() + right()->Check() + 1 : 1;
  }

 private:
  cppgc::Member<TreeNode> left_;
  cppgc::Member<TreeNode> right_;
};

TreeNode* CreateTree(cppgc::AllocationHandle& alloc_handle, size_t depth) {
  auto* node = cppgc::MakeGarbageCollected<TreeNode>(alloc_handle);
  if (depth > 0) {
    node->set_left(CreateTree(alloc_handle, depth - 1));
    node->set_right(CreateTree(alloc_handle, depth - 1));
  }
  return node;
}

void Loop(cppgc::AllocationHandle& alloc_handle, size_t iterations,
          size_t depth) {
  size_t check = 0;
  for (size_t item = 0; item < iterations; ++item) {
    check += CreateTree(alloc_handle, depth)->Check();
  }
  std::cout << iterations << "\t  trees of depth " << depth
            << "\t check: " << check << std::endl;
}

void Trees(cppgc::AllocationHandle& alloc_handle, size_t max_depth) {
  // Keep the long-lived tree in a Persistent to allow for concurrent GC to
  // immediately find it.
  cppgc::Persistent<TreeNode> long_lived_tree =
      CreateTree(alloc_handle, max_depth);

  constexpr size_t kMinDepth = 4;
  const size_t max_iterations = 16 << max_depth;
  for (size_t depth = kMinDepth; depth <= max_depth; depth += 2) {
    const size_t iterations = max_iterations >> depth;
    Loop(alloc_handle, iterations, depth);
  }

  std::cout << "long lived tree of depth " << max_depth << "\t "
            << "check: " << long_lived_tree->Check() << "\n";
}

void RunBinaryTrees(cppgc::Heap& heap) {
  const size_t max_depth = 21;

  auto& alloc_handle = heap.GetAllocationHandle();

  const size_t stretch_depth = max_depth + 1;
  std::cout << "stretch tree of depth " << stretch_depth << "\t "
            << "check: " << CreateTree(alloc_handle, stretch_depth)->Check()
            << std::endl;

  Trees(alloc_handle, max_depth);
}

}  // namespace

BENCHMARK_F(BinaryTrees, V1)(benchmark::State& st) {
  for (auto _ : st) {
    USE(_);
    RunBinaryTrees(heap());
  }
}
