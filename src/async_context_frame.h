#ifndef SRC_ASYNC_CONTEXT_FRAME_H_
#define SRC_ASYNC_CONTEXT_FRAME_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "base_object.h"
#include "v8.h"

#include <cstdint>

namespace node {
namespace async_context_frame {

class Scope {
 public:
  explicit Scope(v8::Isolate* isolate, v8::Local<v8::Value> object);
  ~Scope();

 private:
  v8::Isolate* isolate_;
  v8::Global<v8::Value> prior_;
};

v8::Local<v8::Value> current(v8::Isolate* isolate);
void set(v8::Isolate* isolate, v8::Local<v8::Value> value);
v8::Local<v8::Value> exchange(v8::Isolate* isolate, v8::Local<v8::Value> value);

}  // namespace async_context_frame
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_ASYNC_CONTEXT_FRAME_H_
