// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/platform/platform.h"

#include <cstdio>
#include <cstring>

#include "include/v8-function.h"
#include "src/base/build_config.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

#ifdef V8_TARGET_OS_LINUX
#include <sys/sysmacros.h>

#include "src/base/platform/platform-linux.h"
#endif

#ifdef V8_OS_WIN
#include <windows.h>
#endif

namespace v8 {
namespace base {

#ifdef V8_TARGET_OS_WIN
// Alignment is constrained on Windows.
constexpr size_t kMaxPageSize = 4096;
#elif V8_HOST_ARCH_PPC64
#if defined(_AIX)
// gcc might complain about overalignment (bug):
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=89357
constexpr size_t kMaxPageSize = 4096;
#else
// Native PPC linux has large (64KB) physical pages.
constexpr size_t kMaxPageSize = 65536;
#endif
#else
constexpr size_t kMaxPageSize = 16384;
#endif

alignas(kMaxPageSize) const char kArray[kMaxPageSize] =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
    "tempor incididunt ut labore et dolore magna aliqua.";

TEST(OS, GetCurrentProcessId) {
#ifdef V8_OS_POSIX
  EXPECT_EQ(static_cast<int>(getpid()), OS::GetCurrentProcessId());
#endif

#ifdef V8_OS_WIN
  EXPECT_EQ(static_cast<int>(::GetCurrentProcessId()),
            OS::GetCurrentProcessId());
#endif
}

TEST(OS, RemapPages) {
  if constexpr (OS::IsRemapPageSupported()) {
    const size_t size = base::OS::AllocatePageSize();
    ASSERT_TRUE(size <= kMaxPageSize);
    const void* data = static_cast<const void*>(kArray);

    // Target mapping.
    void* remapped_data =
        OS::Allocate(nullptr, size, base::OS::AllocatePageSize(),
                     OS::MemoryPermission::kReadWrite);
    ASSERT_TRUE(remapped_data);

    EXPECT_TRUE(OS::RemapPages(data, size, remapped_data,
                               OS::MemoryPermission::kReadExecute));
    EXPECT_EQ(0, memcmp(remapped_data, data, size));

    OS::Free(remapped_data, size);
  }
}

#ifdef V8_TARGET_OS_LINUX
TEST(OS, ParseProcMaps) {
  // Truncated
  std::string line = "00000000-12345678 r--p";
  EXPECT_FALSE(MemoryRegion::FromMapsLine(line.c_str()));

  // Constants below are for 64 bit architectures.
#ifdef V8_TARGET_ARCH_64_BIT
  // File-backed.
  line =
      "7f861d1e3000-7f861d33b000 r-xp 00026000 fe:01 12583839                  "
      " /lib/x86_64-linux-gnu/libc-2.33.so";
  auto region = MemoryRegion::FromMapsLine(line.c_str());
  EXPECT_TRUE(region);

  EXPECT_EQ(region->start, 0x7f861d1e3000u);
  EXPECT_EQ(region->end, 0x7f861d33b000u);
  EXPECT_EQ(std::string(region->permissions), std::string("r-xp"));
  EXPECT_EQ(region->offset, 0x00026000u);
  EXPECT_EQ(region->dev, makedev(0xfe, 0x01));
  EXPECT_EQ(region->inode, 12583839u);
  EXPECT_EQ(region->pathname,
            std::string("/lib/x86_64-linux-gnu/libc-2.33.so"));

  // Large device numbers. (The major device number 0x103 is from a real
  // system, the minor device number 0x104 is synthetic.)
  line =
      "556bea200000-556beaa1c000 r--p 00000000 103:104 22                      "
      " /usr/local/bin/node";
  region = MemoryRegion::FromMapsLine(line.c_str());
  EXPECT_EQ(region->start, 0x556bea200000u);
  EXPECT_EQ(region->end, 0x556beaa1c000u);
  EXPECT_EQ(std::string(region->permissions), std::string("r--p"));
  EXPECT_EQ(region->offset, 0x00000000);
  EXPECT_EQ(region->dev, makedev(0x103, 0x104));
  EXPECT_EQ(region->inode, 22u);
  EXPECT_EQ(region->pathname, std::string("/usr/local/bin/node"));

  // Anonymous, but named.
  line =
      "5611cc7eb000-5611cc80c000 rw-p 00000000 00:00 0                         "
      " [heap]";
  region = MemoryRegion::FromMapsLine(line.c_str());
  EXPECT_TRUE(region);

  EXPECT_EQ(region->start, 0x5611cc7eb000u);
  EXPECT_EQ(region->end, 0x5611cc80c000u);
  EXPECT_EQ(std::string(region->permissions), std::string("rw-p"));
  EXPECT_EQ(region->offset, 0u);
  EXPECT_EQ(region->dev, makedev(0x0, 0x0));
  EXPECT_EQ(region->inode, 0u);
  EXPECT_EQ(region->pathname, std::string("[heap]"));

  // Anonymous, not named.
  line = "5611cc7eb000-5611cc80c000 rw-p 00000000 00:00 0";
  region = MemoryRegion::FromMapsLine(line.c_str());
  EXPECT_TRUE(region);

  EXPECT_EQ(region->start, 0x5611cc7eb000u);
  EXPECT_EQ(region->end, 0x5611cc80c000u);
  EXPECT_EQ(std::string(region->permissions), std::string("rw-p"));
  EXPECT_EQ(region->offset, 0u);
  EXPECT_EQ(region->dev, makedev(0x0, 0x0));
  EXPECT_EQ(region->inode, 0u);
  EXPECT_EQ(region->pathname, std::string(""));
#endif  // V8_TARGET_ARCH_64_BIT
}

TEST(OS, GetSharedLibraryAddresses) {
  FILE* fp = tmpfile();
  ASSERT_TRUE(fp);
  const char* contents =
      R"EOF(12340000-12345000 r-xp 00026000 fe:01 12583839                   /lib/x86_64-linux-gnu/libc-2.33.so
12365000-12376000 rw-p 00000000 00:00 0    [heap]
12430000-12435000 r-xp 00062000 fe:01 12583839 /path/to/SomeApplication.apk
)EOF";
  size_t length = strlen(contents);
  ASSERT_EQ(fwrite(contents, 1, length, fp), length);
  rewind(fp);

  auto shared_library_addresses = GetSharedLibraryAddresses(fp);
  EXPECT_EQ(shared_library_addresses.size(), 2u);

  EXPECT_EQ(shared_library_addresses[0].library_path,
            "/lib/x86_64-linux-gnu/libc-2.33.so");
  EXPECT_EQ(shared_library_addresses[0].start, 0x12340000u - 0x26000);

  EXPECT_EQ(shared_library_addresses[1].library_path,
            "/path/to/SomeApplication.apk");
#if defined(V8_OS_ANDROID)
  EXPECT_EQ(shared_library_addresses[1].start, 0x12430000u);
#else
  EXPECT_EQ(shared_library_addresses[1].start, 0x12430000u - 0x62000);
#endif
}
#endif  // V8_TARGET_OS_LINUX

namespace {

class ThreadLocalStorageTest : public Thread, public ::testing::Test {
 public:
  ThreadLocalStorageTest() : Thread(Options("ThreadLocalStorageTest")) {
    for (size_t i = 0; i < arraysize(keys_); ++i) {
      keys_[i] = Thread::CreateThreadLocalKey();
    }
  }
  ~ThreadLocalStorageTest() override {
    for (size_t i = 0; i < arraysize(keys_); ++i) {
      Thread::DeleteThreadLocalKey(keys_[i]);
    }
  }

  void Run() final {
    for (size_t i = 0; i < arraysize(keys_); i++) {
      CHECK(!Thread::HasThreadLocal(keys_[i]));
    }
    for (size_t i = 0; i < arraysize(keys_); i++) {
      Thread::SetThreadLocal(keys_[i], GetValue(i));
    }
    for (size_t i = 0; i < arraysize(keys_); i++) {
      CHECK(Thread::HasThreadLocal(keys_[i]));
    }
    for (size_t i = 0; i < arraysize(keys_); i++) {
      CHECK_EQ(GetValue(i), Thread::GetThreadLocal(keys_[i]));
      CHECK_EQ(GetValue(i), Thread::GetExistingThreadLocal(keys_[i]));
    }
    for (size_t i = 0; i < arraysize(keys_); i++) {
      Thread::SetThreadLocal(keys_[i], GetValue(arraysize(keys_) - i - 1));
    }
    for (size_t i = 0; i < arraysize(keys_); i++) {
      CHECK(Thread::HasThreadLocal(keys_[i]));
    }
    for (size_t i = 0; i < arraysize(keys_); i++) {
      CHECK_EQ(GetValue(arraysize(keys_) - i - 1),
               Thread::GetThreadLocal(keys_[i]));
      CHECK_EQ(GetValue(arraysize(keys_) - i - 1),
               Thread::GetExistingThreadLocal(keys_[i]));
    }
  }

 private:
  static void* GetValue(size_t x) { return reinterpret_cast<void*>(x + 1); }

  // Older versions of Android have fewer TLS slots (nominally 64, but the
  // system uses "about 5 of them" itself).
  Thread::LocalStorageKey keys_[32];
};

}  // namespace

TEST_F(ThreadLocalStorageTest, DoTest) {
  Run();
  CHECK(Start());
  Join();
}

TEST(StackTest, GetStackStart) { EXPECT_NE(nullptr, Stack::GetStackStart()); }

TEST(StackTest, GetCurrentStackPosition) {
  EXPECT_NE(nullptr, Stack::GetCurrentStackPosition());
}

#if !defined(V8_OS_FUCHSIA)
TEST(StackTest, StackVariableInBounds) {
  void* dummy;
  ASSERT_GT(static_cast<void*>(Stack::GetStackStart()),
            Stack::GetCurrentStackPosition());
  EXPECT_GT(static_cast<void*>(Stack::GetStackStart()),
            Stack::GetRealStackAddressForSlot(&dummy));
  EXPECT_LT(static_cast<void*>(Stack::GetCurrentStackPosition()),
            Stack::GetRealStackAddressForSlot(&dummy));
}
#endif  // !V8_OS_FUCHSIA

using SetDataReadOnlyTest = ::testing::Test;

TEST_F(SetDataReadOnlyTest, SetDataReadOnly) {
  static struct alignas(kMaxPageSize) TestData {
    int x;
    int y;
  } test_data;
  static_assert(alignof(TestData) == kMaxPageSize);
  static_assert(sizeof(TestData) == kMaxPageSize);

  test_data.x = 25;
  test_data.y = 41;

  OS::SetDataReadOnly(&test_data, sizeof(test_data));
  CHECK_EQ(25, test_data.x);
  CHECK_EQ(41, test_data.y);

  ASSERT_DEATH_IF_SUPPORTED(test_data.x = 1, "");
  ASSERT_DEATH_IF_SUPPORTED(test_data.y = 0, "");
}

}  // namespace base

namespace {

#ifdef V8_CC_GNU

static uintptr_t sp_addr = 0;

void GetStackPointerCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
  GET_STACK_POINTER_TO(sp_addr);
  CHECK(i::ValidateCallbackInfo(info));
  info.GetReturnValue().Set(v8::Integer::NewFromUnsigned(
      info.GetIsolate(), static_cast<uint32_t>(sp_addr)));
}

using PlatformTest = v8::TestWithIsolate;
TEST_F(PlatformTest, StackAlignment) {
  Local<ObjectTemplate> global_template = ObjectTemplate::New(isolate());
  global_template->Set(
      isolate(), "get_stack_pointer",
      FunctionTemplate::New(isolate(), GetStackPointerCallback));

  Local<Context> context = Context::New(isolate(), nullptr, global_template);
  Context::Scope context_scope(context);
  TryRunJS(
      "function foo() {"
      "  return get_stack_pointer();"
      "}");

  Local<Object> global_object = context->Global();
  Local<Function> foo = v8::Local<v8::Function>::Cast(
      global_object->Get(isolate()->GetCurrentContext(), NewString("foo"))
          .ToLocalChecked());

  Local<v8::Value> result =
      foo->Call(isolate()->GetCurrentContext(), global_object, 0, nullptr)
          .ToLocalChecked();
  CHECK_EQ(0u, result->Uint32Value(isolate()->GetCurrentContext()).FromJust() %
                   base::OS::ActivationFrameAlignment());
}
#endif  // V8_CC_GNU

}  // namespace

}  // namespace v8
