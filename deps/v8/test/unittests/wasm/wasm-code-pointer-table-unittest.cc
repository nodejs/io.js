// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-code-pointer-table-inl.h"
#include "test/unittests/test-utils.h"

namespace v8::internal::wasm {

namespace {

template <typename FunctionType>
class BackgroundThread final : public v8::base::Thread {
 public:
  explicit BackgroundThread(FunctionType function)
      : v8::base::Thread(base::Thread::Options("BackgroundThread")),
        function_(function),
        should_stop_(false) {}

  void Stop() {
    should_stop_.store(true);
    Join();
  }

  void Run() override {
    while (!should_stop_.load()) {
      function_();
    }
  }

 private:
  FunctionType function_;
  std::atomic<bool> should_stop_;
};

template <typename FunctionType>
BackgroundThread(FunctionType) -> BackgroundThread<FunctionType>;

}  // anonymous namespace

class WasmCodePointerTableTest : public TestWithPlatform {
 public:
  WasmCodePointerTableTest()
      : code_pointer_table_(GetProcessWideWasmCodePointerTable()) {}

 protected:
  void SetUp() override {}
  void TearDown() override {
    for (auto handle : handles_) {
      code_pointer_table_->FreeEntry(handle);
    }
    handles_.clear();
  }

  void CreateHoleySegments() {
    std::vector<WasmCodePointer> to_free_handles;

    for (size_t i = 0; i < 3 * WasmCodePointerTable::kEntriesPerSegment + 1337;
         i++) {
      handles_.push_back(code_pointer_table_->AllocateUninitializedEntry());
    }

    for (size_t i = 0; i < 3 * WasmCodePointerTable::kEntriesPerSegment; i++) {
      to_free_handles.push_back(
          code_pointer_table_->AllocateUninitializedEntry());
    }

    for (size_t i = 0; i < 3 * WasmCodePointerTable::kEntriesPerSegment + 1337;
         i++) {
      handles_.push_back(code_pointer_table_->AllocateUninitializedEntry());
    }

    for (size_t i = 0; i < 3 * WasmCodePointerTable::kEntriesPerSegment; i++) {
      to_free_handles.push_back(
          code_pointer_table_->AllocateUninitializedEntry());
    }

    for (size_t i = 0; i < 3 * WasmCodePointerTable::kEntriesPerSegment + 1337;
         i++) {
      handles_.push_back(code_pointer_table_->AllocateUninitializedEntry());
    }

    for (auto to_free_handle : to_free_handles) {
      code_pointer_table_->FreeEntry(to_free_handle);
    }
  }

  WasmCodePointerTable* code_pointer_table_;
  std::vector<WasmCodePointer> handles_;
};

TEST_F(WasmCodePointerTableTest, ConcurrentSweep) {
  BackgroundThread sweep_thread1(
      [this]() { code_pointer_table_->SweepSegments(); });
  BackgroundThread sweep_thread2(
      [this]() { code_pointer_table_->SweepSegments(); });

  CreateHoleySegments();
  sweep_thread1.StartSynchronously();
  sweep_thread2.StartSynchronously();
  CreateHoleySegments();
  sweep_thread1.Stop();
  sweep_thread2.Stop();
}

}  // namespace v8::internal::wasm
