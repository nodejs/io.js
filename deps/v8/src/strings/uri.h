// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_STRINGS_URI_H_
#define V8_STRINGS_URI_H_

#include "src/handles/maybe-handles.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

class Uri : public AllStatic {
 public:
  // ES6 section 18.2.6.2 decodeURI (encodedURI)
  static MaybeDirectHandle<String> DecodeUri(Isolate* isolate,
                                             DirectHandle<String> uri) {
    return Decode(isolate, uri, true);
  }

  // ES6 section 18.2.6.3 decodeURIComponent (encodedURIComponent)
  static MaybeDirectHandle<String> DecodeUriComponent(
      Isolate* isolate, DirectHandle<String> component) {
    return Decode(isolate, component, false);
  }

  // ES6 section 18.2.6.4 encodeURI (uri)
  static MaybeDirectHandle<String> EncodeUri(Isolate* isolate,
                                             DirectHandle<String> uri) {
    return Encode(isolate, uri, true);
  }

  // ES6 section 18.2.6.5 encodeURIComponent (uriComponent)
  static MaybeDirectHandle<String> EncodeUriComponent(
      Isolate* isolate, DirectHandle<String> component) {
    return Encode(isolate, component, false);
  }

  // ES6 section B.2.1.1 escape (string)
  static MaybeDirectHandle<String> Escape(Isolate* isolate,
                                          Handle<String> string);

  // ES6 section B.2.1.2 unescape (string)
  static MaybeDirectHandle<String> Unescape(Isolate* isolate,
                                            Handle<String> string);

 private:
  static MaybeDirectHandle<String> Decode(Isolate* isolate,
                                          DirectHandle<String> uri,
                                          bool is_uri);
  static MaybeDirectHandle<String> Encode(Isolate* isolate,
                                          DirectHandle<String> uri,
                                          bool is_uri);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_STRINGS_URI_H_
