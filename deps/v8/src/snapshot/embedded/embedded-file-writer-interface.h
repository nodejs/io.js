// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_EMBEDDED_EMBEDDED_FILE_WRITER_INTERFACE_H_
#define V8_SNAPSHOT_EMBEDDED_EMBEDDED_FILE_WRITER_INTERFACE_H_

#include <string>

#include "v8config.h"  // NOLINT(build/include_directory)

namespace v8 {
namespace internal {

class Builtins;

#if defined(V8_OS_WIN64)
namespace win64_unwindinfo {
class BuiltinUnwindInfo;
}
#endif  // V8_OS_WIN64

static constexpr char kDefaultEmbeddedVariant[] = "Default";

struct LabelInfo {
  int offset;
  std::string name;
};

// Detailed source-code information about builtins can only be obtained by
// registration on the isolate during compilation.
class EmbeddedFileWriterInterface {
 public:
  // We maintain a database of filenames to synthetic IDs.
  virtual int LookupOrAddExternallyCompiledFilename(const char* filename) = 0;
  virtual const char* GetExternallyCompiledFilename(int index) const = 0;
  virtual int GetExternallyCompiledFilenameCount() const = 0;

  // The isolate will call the method below just prior to replacing the
  // compiled builtin InstructionStream objects with trampolines.
  virtual void PrepareBuiltinSourcePositionMap(Builtins* builtins) = 0;

#if defined(V8_OS_WIN64)
  virtual void SetBuiltinUnwindData(
      Builtin builtin,
      const win64_unwindinfo::BuiltinUnwindInfo& unwinding_info) = 0;
#endif  // V8_OS_WIN64
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_EMBEDDED_EMBEDDED_FILE_WRITER_INTERFACE_H_
