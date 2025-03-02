// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/benchmarks/cpp/benchmark-utils.h"

#include "include/cppgc/platform.h"
#include "include/libplatform/libplatform.h"
#include "include/v8-array-buffer.h"
#include "include/v8-cppgc.h"
#include "include/v8-initialization.h"
namespace v8::benchmarking {

// static
v8::Platform* BenchmarkWithIsolate::platform_;

// static
v8::Isolate* BenchmarkWithIsolate::v8_isolate_;

// static
v8::ArrayBuffer::Allocator* BenchmarkWithIsolate::v8_ab_allocator_;

// static
void BenchmarkWithIsolate::InitializeProcess() {
  v8::V8::SetFlagsFromString("--allow-natives-syntax");
  platform_ = v8::platform::NewDefaultPlatform().release();
  v8::V8::InitializePlatform(platform_);
  v8::V8::Initialize();
  cppgc::InitializeProcess(platform_->GetPageAllocator());
  v8_ab_allocator_ = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
  auto heap = v8::CppHeap::Create(platform_, v8::CppHeapCreateParams({}));
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = v8_ab_allocator_;
  create_params.cpp_heap = heap.release();
  v8_isolate_ = v8::Isolate::New(create_params);
  v8_isolate_->Enter();
}

// static
void BenchmarkWithIsolate::ShutdownProcess() {
  v8_isolate_->Exit();
  v8_isolate_->Dispose();
  cppgc::ShutdownProcess();
  v8::V8::Dispose();
  v8::V8::DisposePlatform();
  delete v8_ab_allocator_;
}

}  // namespace v8::benchmarking
