#ifndef SRC_REQ_WRAP_H_
#define SRC_REQ_WRAP_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "async_wrap.h"
#include "util.h"
#include "v8.h"

namespace node {

class Environment;

class ReqWrapBase {
 public:
  explicit inline ReqWrapBase(Environment* env);

  virtual ~ReqWrapBase() = default;

  virtual void Cancel() = 0;
  virtual AsyncWrap* GetAsyncWrap() = 0;

 private:
  friend int GenDebugSymbols();
  friend class Environment;

  ListNode<ReqWrapBase> req_wrap_queue_;
};

template <typename T>
class ReqWrap : public AsyncWrap, public ReqWrapBase {
 public:
  inline ReqWrap(Environment* env,
                 v8::Local<v8::Object> object,
                 AsyncWrap::ProviderType provider);
  inline ~ReqWrap() override;
  // Call this after the req has been dispatched, if that did not already
  // happen by using Dispatch().
  inline void Dispatched();
  // Call this after a request has finished, if re-using this object is planned.
  inline void Reset();
  T* req() { return &req_; }
  inline void Cancel() final;
  inline AsyncWrap* GetAsyncWrap() override;

  static ReqWrap* from_req(T* req);

  template <typename LibuvFunction, typename... Args>
  inline int Dispatch(LibuvFunction fn, Args... args);

  inline bool IsDispatched();

 private:
  friend int GenDebugSymbols();

  // Adding `friend struct MakeLibuvRequestCallback` is not enough anymore
  // for some reason. Consider this private.
 public:
  typedef void (*callback_t)();
  callback_t original_callback_ = nullptr;

 protected:
  // req_wrap_queue_ needs to be at a fixed offset from the start of the class
  // because it is used by ContainerOf to calculate the address of the embedding
  // ReqWrap. ContainerOf compiles down to simple, fixed pointer arithmetic. It
  // is also used by src/node_postmortem_metadata.cc to calculate offsets and
  // generate debug symbols for ReqWrap, which assumes that the position of
  // members in memory are predictable. sizeof(req_) depends on the type of T,
  // so req_wrap_queue_ would no longer be at a fixed offset if it came after
  // req_. For more information please refer to
  // `doc/contributing/node-postmortem-support.md`
  T req_;
};

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_REQ_WRAP_H_
