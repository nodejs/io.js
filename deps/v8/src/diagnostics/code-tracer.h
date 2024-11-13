// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DIAGNOSTICS_CODE_TRACER_H_
#define V8_DIAGNOSTICS_CODE_TRACER_H_

#include <optional>

#include "src/base/platform/platform.h"
#include "src/base/platform/wrappers.h"
#include "src/base/strings.h"
#include "src/base/vector.h"
#include "src/flags/flags.h"
#include "src/utils/allocation.h"
#include "src/utils/ostreams.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

class CodeTracer final : public Malloced {
 public:
  explicit CodeTracer(int isolate_id) : file_(nullptr), scope_depth_(0) {
    if (!ShouldRedirect()) {
      file_ = stdout;
      return;
    }

    if (v8_flags.redirect_code_traces_to != nullptr) {
      base::StrNCpy(filename_, v8_flags.redirect_code_traces_to,
                    filename_.length());
    } else if (isolate_id >= 0) {
      base::SNPrintF(filename_, "code-%d-%d.asm",
                     base::OS::GetCurrentProcessId(), isolate_id);
    } else {
      base::SNPrintF(filename_, "code-%d.asm", base::OS::GetCurrentProcessId());
    }

    WriteChars(filename_.begin(), "", 0, false);
  }

  class V8_NODISCARD Scope {
   public:
    explicit Scope(CodeTracer* tracer) : tracer_(tracer) { tracer->OpenFile(); }
    ~Scope() { tracer_->CloseFile(); }

    FILE* file() const { return tracer_->file(); }

   private:
    CodeTracer* tracer_;
  };

  class V8_NODISCARD StreamScope : public Scope {
   public:
    explicit StreamScope(CodeTracer* tracer) : Scope(tracer) {
      FILE* file = this->file();
      if (file == stdout) {
        stdout_stream_.emplace();
      } else {
        file_stream_.emplace(file);
      }
    }

    std::ostream& stream() {
      if (stdout_stream_.has_value()) return stdout_stream_.value();
      return file_stream_.value();
    }

   private:
    // Exactly one of these two will be initialized.
    std::optional<StdoutStream> stdout_stream_;
    std::optional<OFStream> file_stream_;
  };

  void OpenFile() {
    if (!ShouldRedirect()) {
      return;
    }

    if (file_ == nullptr) {
      file_ = base::OS::FOpen(filename_.begin(), "ab");
      CHECK_WITH_MSG(file_ != nullptr,
                     "could not open file. If on Android, try passing "
                     "--redirect-code-traces-to=/sdcard/Download/<file-name>");
    }

    scope_depth_++;
  }

  void CloseFile() {
    if (!ShouldRedirect()) {
      return;
    }

    if (--scope_depth_ == 0) {
      DCHECK_NOT_NULL(file_);
      base::Fclose(file_);
      file_ = nullptr;
    }
  }

  FILE* file() const { return file_; }

 private:
  static bool ShouldRedirect() { return v8_flags.redirect_code_traces; }

  base::EmbeddedVector<char, 128> filename_;
  FILE* file_;
  int scope_depth_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_DIAGNOSTICS_CODE_TRACER_H_
