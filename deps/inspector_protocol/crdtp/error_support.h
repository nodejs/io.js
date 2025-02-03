// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRDTP_ERROR_SUPPORT_H_
#define CRDTP_ERROR_SUPPORT_H_

#include <cstdint>
#include <string>
#include <vector>
#include "export.h"
#include "span.h"

namespace crdtp {
// =============================================================================
// ErrorSupport - For tracking errors in tree structures.
// =============================================================================

// This abstraction is used when converting between Values and inspector
// objects, e.g. in lib/ValueConversions_{h,cc}.template. As the processing
// enters and exits a branch, we call Push / Pop. Within the branch,
// we either set the name or an index (in case we're processing the element of a
// list/vector). Only once an error is seen, the path which is now on the
// stack is materialized and prefixes the error message. E.g.,
// "foo.bar.2: some error". After error collection, ::Errors() is used to
// access the message.
class CRDTP_EXPORT ErrorSupport {
 public:
  // Push / Pop operations for the path segments; after Push, either SetName or
  // SetIndex must be called exactly once.
  void Push();
  void Pop();

  // Sets the name of the current segment on the stack; e.g. a field name.
  // |name| must be a C++ string literal in 7 bit US-ASCII.
  void SetName(const char* name);
  // Sets the index of the current segment on the stack; e.g. an array index.
  void SetIndex(size_t index);

  // Materializes the error internally. |error| must be a C++ string literal
  // in 7 bit US-ASCII.
  void AddError(const char* error);

  // Returns the semicolon-separated list of errors as in 7 bit ASCII.
  span<uint8_t> Errors() const;

 private:
  enum SegmentType { EMPTY, NAME, INDEX };
  struct Segment {
    SegmentType type = EMPTY;
    union {
      const char* name;
      size_t index;
    };
  };
  std::vector<Segment> stack_;
  std::string errors_;
};

}  // namespace crdtp

#endif  // CRDTP_ERROR_SUPPORT_H_
