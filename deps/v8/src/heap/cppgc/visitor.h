// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_VISITOR_H_
#define V8_HEAP_CPPGC_VISITOR_H_

#include "include/cppgc/visitor.h"
#include "src/heap/cppgc/heap-object-header.h"

namespace cppgc {
namespace internal {

class HeapBase;
class HeapObjectHeader;
class PageBackend;

class VisitorFactory final {
 public:
  static constexpr Visitor::Key CreateKey() { return {}; }
};

// Base visitor that is allowed to create a public cppgc::Visitor object and
// use its internals.
class VisitorBase : public cppgc::Visitor {
 public:
  template <typename T>
  static void TraceRawForTesting(cppgc::Visitor* visitor, const T* t) {
    visitor->TraceImpl(t);
  }

  VisitorBase() : cppgc::Visitor(VisitorFactory::CreateKey()) {}
  ~VisitorBase() override = default;

  VisitorBase(const VisitorBase&) = delete;
  VisitorBase& operator=(const VisitorBase&) = delete;
};

class RootVisitorBase : public RootVisitor {
 public:
  RootVisitorBase() : RootVisitor(VisitorFactory::CreateKey()) {}
  ~RootVisitorBase() override = default;

  RootVisitorBase(const RootVisitorBase&) = delete;
  RootVisitorBase& operator=(const RootVisitorBase&) = delete;
};

// Regular visitor that additionally allows for conservative tracing.
class V8_EXPORT_PRIVATE ConservativeTracingVisitor {
 public:
  ConservativeTracingVisitor(HeapBase&, PageBackend&, cppgc::Visitor&);
  virtual ~ConservativeTracingVisitor() = default;

  ConservativeTracingVisitor(const ConservativeTracingVisitor&) = delete;
  ConservativeTracingVisitor& operator=(const ConservativeTracingVisitor&) =
      delete;

  virtual void TraceConservativelyIfNeeded(const void*);
  void TraceConservativelyIfNeeded(HeapObjectHeader&);
  void TraceConservatively(const HeapObjectHeader&);

 protected:
  using TraceConservativelyCallback = void(ConservativeTracingVisitor*,
                                           const HeapObjectHeader&);
  virtual void VisitFullyConstructedConservatively(HeapObjectHeader&);
  virtual void VisitInConstructionConservatively(HeapObjectHeader&,
                                                 TraceConservativelyCallback) {}

  void TryTracePointerConservatively(ConstAddress address);

  HeapBase& heap_;
  PageBackend& page_backend_;
  cppgc::Visitor& visitor_;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_VISITOR_H_
