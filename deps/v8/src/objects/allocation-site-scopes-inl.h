// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_ALLOCATION_SITE_SCOPES_INL_H_
#define V8_OBJECTS_ALLOCATION_SITE_SCOPES_INL_H_

#include "src/objects/allocation-site-scopes.h"

#include "src/objects/allocation-site-inl.h"

namespace v8 {
namespace internal {

void AllocationSiteContext::InitializeTraversal(Handle<AllocationSite> site) {
  top_ = site;
  // {current_} is updated in place to not create unnecessary Handles, hence
  // we initially need a separate handle.
  current_ = Handle<AllocationSite>::New(*top_, isolate());
}

Handle<AllocationSite> AllocationSiteUsageContext::EnterNewScope() {
  if (top().is_null()) {
    InitializeTraversal(top_site_);
  } else {
    // Advance current site
    Tagged<Object> nested_site = current()->nested_site();
    // Something is wrong if we advance to the end of the list here.
    update_current_site(Cast<AllocationSite>(nested_site));
  }
  return Handle<AllocationSite>(*current(), isolate());
}

void AllocationSiteUsageContext::ExitScope(
    DirectHandle<AllocationSite> scope_site, Handle<JSObject> object) {
  // This assert ensures that we are pointing at the right sub-object in a
  // recursive walk of a nested literal.
  DCHECK(object.is_null() || *object == scope_site->boilerplate());
}

bool AllocationSiteUsageContext::ShouldCreateMemento(
    DirectHandle<JSObject> object) {
  if (activated_ && AllocationSite::CanTrack(object->map()->instance_type())) {
    if (v8_flags.allocation_site_pretenuring ||
        AllocationSite::ShouldTrack(object->GetElementsKind())) {
      if (v8_flags.trace_creation_allocation_sites) {
        PrintF("*** Creating Memento for %s %p\n",
               IsJSArray(*object) ? "JSArray" : "JSObject",
               reinterpret_cast<void*>(object->ptr()));
      }
      return true;
    }
  }
  return false;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_ALLOCATION_SITE_SCOPES_INL_H_
