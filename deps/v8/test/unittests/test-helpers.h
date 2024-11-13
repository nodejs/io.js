// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UNITTESTS_TEST_HELPERS_H_
#define V8_UNITTESTS_TEST_HELPERS_H_

#include <memory>

#include "include/v8-primitive.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

class SharedFunctionInfo;
class Utf16CharacterStream;

namespace test {

class ScriptResource : public v8::String::ExternalOneByteStringResource {
 public:
  ScriptResource(const char* data, size_t length, uint16_t parameter_count)
      : data_(data), length_(length), parameter_count_(parameter_count) {}
  ~ScriptResource() override = default;
  ScriptResource(const ScriptResource&) = delete;
  ScriptResource& operator=(const ScriptResource&) = delete;

  const char* data() const override { return data_; }
  size_t length() const override { return length_; }
  uint16_t parameter_count() const { return parameter_count_; }

 private:
  const char* data_;
  size_t length_;
  uint16_t parameter_count_;
};

test::ScriptResource* CreateSource(test::ScriptResource* maybe_resource);
Handle<SharedFunctionInfo> CreateSharedFunctionInfo(
    Isolate* isolate, ScriptResource* maybe_resource);
std::unique_ptr<Utf16CharacterStream> SourceCharacterStreamForShared(
    Isolate* isolate, DirectHandle<SharedFunctionInfo> shared);

}  // namespace test
}  // namespace internal
}  // namespace v8

#endif  // V8_UNITTESTS_TEST_HELPERS_H_
