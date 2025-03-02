// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_V8_EMBEDDER_HEAP_H_
#define INCLUDE_V8_EMBEDDER_HEAP_H_

#include "v8-traced-handle.h"  // NOLINT(build/include_directory)
#include "v8config.h"          // NOLINT(build/include_directory)

namespace v8 {
namespace internal {
class TracedHandles;
}  // namespace internal

class Isolate;
class Value;

/**
 * Handler for embedder roots on non-unified heap garbage collections.
 */
class V8_EXPORT EmbedderRootsHandler {
 public:
  virtual ~EmbedderRootsHandler() = default;

  EmbedderRootsHandler() = default;

  /**
   * Used in combination with |IsRoot|. Called by V8 when an
   * object that is backed by a handle is reclaimed by a non-tracing garbage
   * collection. It is up to the embedder to reset the original handle.
   *
   * Note that the |handle| is different from the handle that the embedder holds
   * for retaining the object. It is up to the embedder to find the original
   * handle via the object or class id.
   */
  virtual void ResetRoot(const v8::TracedReference<v8::Value>& handle) = 0;

  /**
   * Similar to |ResetRoot()|, but opportunistic. The function is called in
   * parallel for different handles and as such must be thread-safe. In case,
   * |false| is returned, |ResetRoot()| will be recalled for the same handle.
   */
  virtual bool TryResetRoot(const v8::TracedReference<v8::Value>& handle) {
    ResetRoot(handle);
    return true;
  }

 private:
  friend class internal::TracedHandles;
};

}  // namespace v8

#endif  // INCLUDE_V8_EMBEDDER_HEAP_H_
