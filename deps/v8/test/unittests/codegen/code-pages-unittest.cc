// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-function.h"
#include "src/api/api-inl.h"
#include "src/codegen/code-desc.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/handles/handles-inl.h"
#include "src/heap/factory.h"
#include "src/heap/spaces.h"
#include "src/libsampler/sampler.h"
#include "test/unittests/heap/heap-utils.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {

using CodePagesTest = TestWithContext;

namespace internal {
namespace test_code_pages {

// We have three levels of support which have different behaviors to test.
// 1 - Have code range. ARM64 and x64
// 2 - Have code pages. ARM32 only
// 3 - Nothing - This feature does not work on other platforms.
#if defined(V8_TARGET_ARCH_ARM)
static const bool kHaveCodePages = true;
#else
static const bool kHaveCodePages = false;
#endif  // defined(V8_TARGET_ARCH_ARM)

static const char* foo_source = R"(
  function foo%d(a, b) {
    let x = a * b;
    let y = x ^ b;
    let z = y / a;
    return x + y - z;
  };
  %%PrepareFunctionForOptimization(foo%d);
  foo%d(1, 2);
  foo%d(1, 2);
  %%OptimizeFunctionOnNextCall(foo%d);
  foo%d(1, 2);
)";

std::string getFooCode(int n) {
  constexpr size_t kMaxSize = 512;
  char foo_replaced[kMaxSize];
  EXPECT_LE(n, 999999);
  snprintf(foo_replaced, kMaxSize, foo_source, n, n, n, n, n, n);

  return std::string(foo_replaced);
}

namespace {

bool PagesHasExactPage(std::vector<MemoryRange>* pages, Address search_page) {
  void* addr = reinterpret_cast<void*>(search_page);
  auto it =
      std::find_if(pages->begin(), pages->end(),
                   [addr](const MemoryRange& r) { return r.start == addr; });
  return it != pages->end();
}

bool PagesHasExactPage(std::vector<MemoryRange>* pages, Address search_page,
                       size_t size) {
  void* addr = reinterpret_cast<void*>(search_page);
  auto it = std::find_if(pages->begin(), pages->end(),
                         [addr, size](const MemoryRange& r) {
                           return r.start == addr && r.length_in_bytes == size;
                         });
  return it != pages->end();
}

bool PagesContainsRange(std::vector<MemoryRange>* pages, Address search_address,
                        size_t size) {
  uint8_t* addr = reinterpret_cast<uint8_t*>(search_address);
  auto it =
      std::find_if(pages->begin(), pages->end(), [=](const MemoryRange& r) {
        const uint8_t* page_start = reinterpret_cast<const uint8_t*>(r.start);
        const uint8_t* page_end = page_start + r.length_in_bytes;
        return addr >= page_start && (addr + size) <= page_end;
      });
  return it != pages->end();
}

bool PagesContainsAddress(std::vector<MemoryRange>* pages,
                          Address search_address) {
  return PagesContainsRange(pages, search_address, 0);
}

}  // namespace

TEST_F(CodePagesTest, CodeRangeCorrectContents) {
  if (!i_isolate()->RequiresCodeRange()) return;

  std::vector<MemoryRange>* pages = i_isolate()->GetCodePages();

  const base::AddressRegion& code_region = i_isolate()->heap()->code_region();
  EXPECT_TRUE(!code_region.is_empty());
  // We should only have the code range and the embedded code range.
  EXPECT_EQ(2u, pages->size());
  EXPECT_TRUE(
      PagesHasExactPage(pages, code_region.begin(), code_region.size()));
  EXPECT_TRUE(PagesHasExactPage(
      pages, reinterpret_cast<Address>(i_isolate()->CurrentEmbeddedBlobCode()),
      i_isolate()->CurrentEmbeddedBlobCodeSize()));
  if (i_isolate()->is_short_builtin_calls_enabled()) {
    // In this case embedded blob code must be included via code_region.
    EXPECT_TRUE(PagesContainsRange(
        pages, reinterpret_cast<Address>(i_isolate()->embedded_blob_code()),
        i_isolate()->embedded_blob_code_size()));
  } else {
    EXPECT_TRUE(PagesHasExactPage(
        pages, reinterpret_cast<Address>(i_isolate()->embedded_blob_code()),
        i_isolate()->embedded_blob_code_size()));
  }
}

TEST_F(CodePagesTest, CodePagesCorrectContents) {
  if (!kHaveCodePages) return;

  std::vector<MemoryRange>* pages = i_isolate()->GetCodePages();
  // There might be other pages already.
  EXPECT_GE(pages->size(), 1u);

  const base::AddressRegion& code_region = i_isolate()->heap()->code_region();
  EXPECT_TRUE(code_region.is_empty());

  // We should have the embedded code range even when there is no regular code
  // range.
  EXPECT_TRUE(PagesHasExactPage(
      pages, reinterpret_cast<Address>(i_isolate()->embedded_blob_code()),
      i_isolate()->embedded_blob_code_size()));
}

TEST_F(CodePagesTest, OptimizedCodeWithCodeRange) {
  v8_flags.allow_natives_syntax = true;
  if (!i_isolate()->RequiresCodeRange()) return;

  HandleScope scope(i_isolate());

  std::string foo_str = getFooCode(1);
  RunJS(foo_str.c_str());
  v8::Local<v8::Function> local_foo = v8::Local<v8::Function>::Cast(
      context()->Global()->Get(context(), NewString("foo1")).ToLocalChecked());
  Handle<JSFunction> foo =
      Handle<JSFunction>::cast(v8::Utils::OpenHandle(*local_foo));

  Tagged<Code> code = foo->code(i_isolate());
  // We don't produce optimized code when run with --no-turbofan and
  // --no-maglev.
  if (!code->is_optimized_code()) return;
  Tagged<InstructionStream> foo_code = code->instruction_stream();

  EXPECT_TRUE(i_isolate()->heap()->InSpace(foo_code, CODE_SPACE));

  std::vector<MemoryRange>* pages = i_isolate()->GetCodePages();
  EXPECT_TRUE(PagesContainsAddress(pages, foo_code.address()));
}

TEST_F(CodePagesTest, OptimizedCodeWithCodePages) {
  if (!kHaveCodePages) return;
  // We don't want incremental marking to start which could cause the code to
  // not be collected on the CollectGarbage() call.
  ManualGCScope manual_gc_scope(i_isolate());
  v8_flags.allow_natives_syntax = true;

  const void* created_page = nullptr;
  int num_foos_created = 0;

  {
    HandleScope scope(i_isolate());

    size_t num_code_pages = 0;
    size_t initial_num_code_pages = 0;

    // Keep generating new code until a new code page is added to the list.
    for (int n = 0; n < 999999; n++) {
      // Compile and optimize the code and get a reference to it.
      std::string foo_str = getFooCode(n);
      char foo_name[10];
      snprintf(foo_name, sizeof(foo_name), "foo%d", n);
      RunJS(foo_str.c_str());
      v8::Local<v8::Function> local_foo = v8::Local<v8::Function>::Cast(
          context()
              ->Global()
              ->Get(context(), NewString(foo_name))
              .ToLocalChecked());
      Handle<JSFunction> foo =
          Handle<JSFunction>::cast(v8::Utils::OpenHandle(*local_foo));

      // If there is baseline code, check that it's only due to
      // --always-sparkplug (if this check fails, we'll have to re-think this
      // test).
      if (foo->shared()->HasBaselineCode()) {
        EXPECT_TRUE(v8_flags.always_sparkplug);
        return;
      }
      Tagged<Code> code = foo->code(i_isolate());
      // We don't produce optimized code when run with --no-turbofan and
      // --no-maglev.
      if (!code->is_optimized_code()) return;
      Tagged<InstructionStream> foo_code = code->instruction_stream();

      EXPECT_TRUE(i_isolate()->heap()->InSpace(foo_code, CODE_SPACE));

      // Check that the generated code ended up in one of the code pages
      // returned by GetCodePages().
      uint8_t* foo_code_ptr = reinterpret_cast<uint8_t*>(foo_code.address());
      std::vector<MemoryRange>* pages = i_isolate()->GetCodePages();

      // Wait until after we have created the first function to take the initial
      // number of pages so that this test isn't brittle to irrelevant
      // implementation details.
      if (n == 0) {
        initial_num_code_pages = pages->size();
      }
      num_code_pages = pages->size();

      // Check that the code object was allocation on any of the pages returned
      // by GetCodePages().
      auto it = std::find_if(
          pages->begin(), pages->end(), [foo_code_ptr](const MemoryRange& r) {
            const uint8_t* page_start =
                reinterpret_cast<const uint8_t*>(r.start);
            const uint8_t* page_end = page_start + r.length_in_bytes;
            return foo_code_ptr >= page_start && foo_code_ptr < page_end;
          });
      EXPECT_NE(it, pages->end());

      // Store the page that was created just for our functions - we expect it
      // to be removed later.
      if (num_code_pages > initial_num_code_pages) {
        created_page = it->start;
        num_foos_created = n + 1;
        break;
      }
    }
    CHECK_NOT_NULL(created_page);
  }

  // Now delete all our foos and force a GC and check that the page is removed
  // from the list.
  {
    HandleScope scope(i_isolate());
    for (int n = 0; n < num_foos_created; n++) {
      char foo_name[10];
      snprintf(foo_name, sizeof(foo_name), "foo%d", n);
      context()
          ->Global()
          ->Set(context(), NewString(foo_name), v8::Undefined(isolate()))
          .Check();
    }
  }

  InvokeMajorGC();

  std::vector<MemoryRange>* pages = i_isolate()->GetCodePages();
  auto it = std::find_if(
      pages->begin(), pages->end(),
      [created_page](const MemoryRange& r) { return r.start == created_page; });
  EXPECT_EQ(it, pages->end());
}

TEST_F(CodePagesTest, LargeCodeObject) {
  // We don't want incremental marking to start which could cause the code to
  // not be collected on the CollectGarbage() call.
  ManualGCScope manual_gc_scope(i_isolate());
  DisableConservativeStackScanningScopeForTesting no_stack_scanning(
      i_isolate()->heap());

  if (!i_isolate()->RequiresCodeRange() && !kHaveCodePages) return;

  // Create a big function that ends up in CODE_LO_SPACE.
  const int instruction_size = PageMetadata::kPageSize + 1;
  EXPECT_GT(instruction_size, MemoryChunkLayout::MaxRegularCodeObjectSize());
  std::unique_ptr<uint8_t[]> instructions(new uint8_t[instruction_size]);

  CodeDesc desc;
  desc.buffer = instructions.get();
  desc.buffer_size = instruction_size;
  desc.instr_size = instruction_size;
  desc.reloc_size = 0;
  desc.constant_pool_size = 0;
  desc.unwinding_info = nullptr;
  desc.unwinding_info_size = 0;
  desc.origin = nullptr;

  Address stale_code_address;

  {
    HandleScope scope(i_isolate());
    Handle<Code> foo_code =
        Factory::CodeBuilder(i_isolate(), desc, CodeKind::FOR_TESTING).Build();
    Handle<InstructionStream> foo_istream(foo_code->instruction_stream(),
                                          i_isolate());

    EXPECT_TRUE(i_isolate()->heap()->InSpace(*foo_istream, CODE_LO_SPACE));

    std::vector<MemoryRange>* pages = i_isolate()->GetCodePages();

    if (i_isolate()->RequiresCodeRange()) {
      EXPECT_TRUE(PagesContainsAddress(pages, foo_istream->address()));
    } else {
      EXPECT_TRUE(PagesHasExactPage(pages, foo_istream->address()));
    }

    stale_code_address = foo_istream->address();
  }

  // Delete the large code object.
  InvokeMajorGC();
  EXPECT_TRUE(
      !i_isolate()->heap()->InSpaceSlow(stale_code_address, CODE_LO_SPACE));

  // Check that it was removed from CodePages.
  std::vector<MemoryRange>* pages = i_isolate()->GetCodePages();
  EXPECT_TRUE(!PagesHasExactPage(pages, stale_code_address));
}

static constexpr size_t kBufSize = v8::Isolate::kMinCodePagesBufferSize;

class SignalSender : public sampler::Sampler {
 public:
  explicit SignalSender(v8::Isolate* isolate) : sampler::Sampler(isolate) {}

  // Called during the signal/thread suspension.
  void SampleStack(const v8::RegisterState& regs) override {
    MemoryRange* code_pages_copy = code_pages_copy_.load();
    CHECK_NOT_NULL(code_pages_copy);
    size_t num_pages = isolate_->CopyCodePages(kBufSize, code_pages_copy);
    EXPECT_LE(num_pages, kBufSize);
    sample_semaphore_.Signal();
  }

  // Called on the sampling thread to trigger a sample. Blocks until the sample
  // is finished.
  void SampleIntoVector(MemoryRange output_buffer[]) {
    code_pages_copy_.store(output_buffer);
    DoSample();
    sample_semaphore_.Wait();
    code_pages_copy_.store(nullptr);
  }

 private:
  base::Semaphore sample_semaphore_{0};
  std::atomic<MemoryRange*> code_pages_copy_{nullptr};
};

class SamplingThread : public base::Thread {
 public:
  explicit SamplingThread(SignalSender* signal_sender)
      : base::Thread(base::Thread::Options("SamplingThread")),
        signal_sender_(signal_sender) {}

  // Blocks until a sample is taken.
  void TriggerSample() { signal_sender_->SampleIntoVector(code_pages_copy_); }

  void Run() override {
    while (running_.load()) {
      TriggerSample();
    }
  }

  // Called from the main thread. Blocks until a sample is taken. Not
  // thread-safe so do not call while this thread is running.
  static std::vector<MemoryRange> DoSynchronousSample(v8::Isolate* isolate) {
    MemoryRange code_pages_copy[kBufSize];
    size_t num_pages = isolate->CopyCodePages(kBufSize, code_pages_copy);
    EXPECT_LE(num_pages, kBufSize);
    return std::vector<MemoryRange>{code_pages_copy,
                                    &code_pages_copy[num_pages]};
  }

  void Stop() { running_.store(false); }

 private:
  std::atomic_bool running_{true};
  SignalSender* signal_sender_;
  MemoryRange code_pages_copy_[kBufSize];
};

TEST_F(CodePagesTest, LargeCodeObjectWithSignalHandler) {
  // We don't want incremental marking to start which could cause the code to
  // not be collected on the CollectGarbage() call.
  ManualGCScope manual_gc_scope(i_isolate());
  DisableConservativeStackScanningScopeForTesting no_stack_scanning(
      i_isolate()->heap());

  if (!i_isolate()->RequiresCodeRange() && !kHaveCodePages) return;

  // Create a big function that ends up in CODE_LO_SPACE.
  const int instruction_size = PageMetadata::kPageSize + 1;
  EXPECT_GT(instruction_size, MemoryChunkLayout::MaxRegularCodeObjectSize());
  std::unique_ptr<uint8_t[]> instructions(new uint8_t[instruction_size]);

  CodeDesc desc;
  desc.buffer = instructions.get();
  desc.buffer_size = instruction_size;
  desc.instr_size = instruction_size;
  desc.reloc_size = 0;
  desc.constant_pool_size = 0;
  desc.unwinding_info = nullptr;
  desc.unwinding_info_size = 0;
  desc.origin = nullptr;

  Address stale_code_address;

  SignalSender signal_sender(isolate());
  signal_sender.Start();
  // Take an initial sample.
  std::vector<MemoryRange> initial_pages =
      SamplingThread::DoSynchronousSample(isolate());
  SamplingThread sampling_thread(&signal_sender);

  sampling_thread.StartSynchronously();

  {
    HandleScope scope(i_isolate());
    Handle<Code> foo_code =
        Factory::CodeBuilder(i_isolate(), desc, CodeKind::FOR_TESTING).Build();
    Handle<InstructionStream> foo_istream(foo_code->instruction_stream(),
                                          i_isolate());

    EXPECT_TRUE(i_isolate()->heap()->InSpace(*foo_istream, CODE_LO_SPACE));

    // Do a synchronous sample to ensure that we capture the state with the
    // extra code page.
    sampling_thread.Stop();
    sampling_thread.Join();

    // Check that the page was added.
    std::vector<MemoryRange> pages =
        SamplingThread::DoSynchronousSample(isolate());
    if (i_isolate()->RequiresCodeRange()) {
      EXPECT_TRUE(PagesContainsAddress(&pages, foo_istream->address()));
    } else {
      EXPECT_TRUE(PagesHasExactPage(&pages, foo_istream->address()));
    }

    stale_code_address = foo_istream->address();
  }

  // Start async sampling again to detect threading issues.
  sampling_thread.StartSynchronously();

  // Delete the large code object.
  InvokeMajorGC();
  EXPECT_TRUE(
      !i_isolate()->heap()->InSpaceSlow(stale_code_address, CODE_LO_SPACE));

  sampling_thread.Stop();
  sampling_thread.Join();

  std::vector<MemoryRange> pages =
      SamplingThread::DoSynchronousSample(isolate());
  EXPECT_TRUE(!PagesHasExactPage(&pages, stale_code_address));

  signal_sender.Stop();
}

TEST_F(CodePagesTest, Sorted) {
  // We don't want incremental marking to start which could cause the code to
  // not be collected on the CollectGarbage() call.
  ManualGCScope manual_gc_scope(i_isolate());
  DisableConservativeStackScanningScopeForTesting no_stack_scanning(
      i_isolate()->heap());

  if (!i_isolate()->RequiresCodeRange() && !kHaveCodePages) return;

  // Create a big function that ends up in CODE_LO_SPACE.
  const int instruction_size = PageMetadata::kPageSize + 1;
  EXPECT_GT(instruction_size, MemoryChunkLayout::MaxRegularCodeObjectSize());
  std::unique_ptr<uint8_t[]> instructions(new uint8_t[instruction_size]);

  CodeDesc desc;
  desc.buffer = instructions.get();
  desc.buffer_size = instruction_size;
  desc.instr_size = instruction_size;
  desc.reloc_size = 0;
  desc.constant_pool_size = 0;
  desc.unwinding_info = nullptr;
  desc.unwinding_info_size = 0;
  desc.origin = nullptr;

  // Take an initial sample.
  std::vector<MemoryRange> initial_pages =
      SamplingThread::DoSynchronousSample(isolate());
  size_t initial_num_pages = initial_pages.size();

  auto compare = [](const MemoryRange& a, const MemoryRange& b) {
    return a.start < b.start;
  };
  {
    HandleScope outer_scope(i_isolate());
    Handle<InstructionStream> code1, code3;
    Address code2_address;

    code1 =
        handle(Factory::CodeBuilder(i_isolate(), desc, CodeKind::FOR_TESTING)
                   .Build()
                   ->instruction_stream(),
               i_isolate());
    EXPECT_TRUE(i_isolate()->heap()->InSpace(*code1, CODE_LO_SPACE));

    {
      HandleScope scope(i_isolate());

      // Create three large code objects, we'll delete the middle one and check
      // everything is still sorted.
      Handle<InstructionStream> code2 =
          handle(Factory::CodeBuilder(i_isolate(), desc, CodeKind::FOR_TESTING)
                     .Build()
                     ->instruction_stream(),
                 i_isolate());
      EXPECT_TRUE(i_isolate()->heap()->InSpace(*code2, CODE_LO_SPACE));
      code3 =
          handle(Factory::CodeBuilder(i_isolate(), desc, CodeKind::FOR_TESTING)
                     .Build()
                     ->instruction_stream(),
                 i_isolate());
      EXPECT_TRUE(i_isolate()->heap()->InSpace(*code3, CODE_LO_SPACE));

      code2_address = code2->address();
      EXPECT_TRUE(
          i_isolate()->heap()->InSpaceSlow(code1->address(), CODE_LO_SPACE));
      EXPECT_TRUE(
          i_isolate()->heap()->InSpaceSlow(code2->address(), CODE_LO_SPACE));
      EXPECT_TRUE(
          i_isolate()->heap()->InSpaceSlow(code3->address(), CODE_LO_SPACE));

      // Check that the pages were added.
      std::vector<MemoryRange> pages =
          SamplingThread::DoSynchronousSample(isolate());
      if (i_isolate()->RequiresCodeRange()) {
        EXPECT_EQ(pages.size(), initial_num_pages);
      } else {
        EXPECT_EQ(pages.size(), initial_num_pages + 3);
      }

      EXPECT_TRUE(std::is_sorted(pages.begin(), pages.end(), compare));

      code3 = scope.CloseAndEscape(code3);
    }
    EXPECT_TRUE(
        i_isolate()->heap()->InSpaceSlow(code1->address(), CODE_LO_SPACE));
    EXPECT_TRUE(i_isolate()->heap()->InSpaceSlow(code2_address, CODE_LO_SPACE));
    EXPECT_TRUE(
        i_isolate()->heap()->InSpaceSlow(code3->address(), CODE_LO_SPACE));
    // Delete code2.
    InvokeMajorGC();
    EXPECT_TRUE(
        i_isolate()->heap()->InSpaceSlow(code1->address(), CODE_LO_SPACE));
    EXPECT_TRUE(
        !i_isolate()->heap()->InSpaceSlow(code2_address, CODE_LO_SPACE));
    EXPECT_TRUE(
        i_isolate()->heap()->InSpaceSlow(code3->address(), CODE_LO_SPACE));

    std::vector<MemoryRange> pages =
        SamplingThread::DoSynchronousSample(isolate());
    if (i_isolate()->RequiresCodeRange()) {
      EXPECT_EQ(pages.size(), initial_num_pages);
    } else {
      EXPECT_EQ(pages.size(), initial_num_pages + 2);
    }
    EXPECT_TRUE(std::is_sorted(pages.begin(), pages.end(), compare));
  }
}

}  // namespace test_code_pages
}  // namespace internal
}  // namespace v8
