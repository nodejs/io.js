#ifndef SRC_TIMER_WRAP_H_
#define SRC_TIMER_WRAP_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "memory_tracker.h"
#include "env.h"
#include "uv.h"

#include <functional>

namespace node {

// Utility class that makes working with libuv timers a bit easier.
class TimerWrap final : public MemoryRetainer {
 public:
  using TimerCb = std::function<void()>;

  template <typename... Args>
  explicit inline TimerWrap(Environment* env, Args&&... args);

  TimerWrap(const TimerWrap&) = delete;

  inline Environment* env() const { return env_; }

  // Stop calling the timer callback.
  void Stop();
  // Render the timer unusable and delete this object.
  void Close();

  // Starts / Restarts the Timer
  void Update(uint64_t interval, uint64_t repeat = 0);

  void Ref();
  void Unref();

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(TimerWrap)
  SET_SELF_SIZE(TimerWrap)

 private:
  static void TimerClosedCb(uv_handle_t* handle);
  static void OnTimeout(uv_timer_t* timer);
  ~TimerWrap() = default;

  Environment* env_;
  TimerCb fn_;
  uv_timer_t timer_;

  friend std::unique_ptr<TimerWrap>::deleter_type;
};

class TimerWrapHandle : public MemoryRetainer  {
 public:
  template <typename... Args>
  explicit inline TimerWrapHandle(Environment* env, Args&&... args);

  TimerWrapHandle(const TimerWrapHandle&) = delete;

  ~TimerWrapHandle() { Close(); }

  void Update(uint64_t interval, uint64_t repeat = 0);

  inline operator bool() const { return timer_ != nullptr; }

  void Ref();
  void Unref();

  void Stop();
  void Close();

  void MemoryInfo(node::MemoryTracker* tracker) const override;

  SET_MEMORY_INFO_NAME(TimerWrapHandle)
  SET_SELF_SIZE(TimerWrapHandle)

 private:
  static void CleanupHook(void* data);

  TimerWrap* timer_;
};

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_TIMER_WRAP_H_
