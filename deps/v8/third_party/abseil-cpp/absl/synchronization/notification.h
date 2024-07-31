// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// -----------------------------------------------------------------------------
// notification.h
// -----------------------------------------------------------------------------
//
// This header file defines a `Notification` abstraction, which allows threads
// to receive notification of a single occurrence of a single event.
//
// The `Notification` object maintains a private boolean "notified" state that
// transitions to `true` at most once. The `Notification` class provides the
// following primary member functions:
//   * `HasBeenNotified()` to query its state
//   * `WaitForNotification*()` to have threads wait until the "notified" state
//      is `true`.
//   * `Notify()` to set the notification's "notified" state to `true` and
//     notify all waiting threads that the event has occurred.
//     This method may only be called once.
//
// Note that while `Notify()` may only be called once, it is perfectly valid to
// call any of the `WaitForNotification*()` methods multiple times, from
// multiple threads -- even after the notification's "notified" state has been
// set -- in which case those methods will immediately return.
//
// Note that the lifetime of a `Notification` requires careful consideration;
// it might not be safe to destroy a notification after calling `Notify()` since
// it is still legal for other threads to call `WaitForNotification*()` methods
// on the notification. However, observers responding to a "notified" state of
// `true` can safely delete the notification without interfering with the call
// to `Notify()` in the other thread.
//
// Memory ordering: For any threads X and Y, if X calls `Notify()`, then any
// action taken by X before it calls `Notify()` is visible to thread Y after:
//  * Y returns from `WaitForNotification()`, or
//  * Y receives a `true` return value from either `HasBeenNotified()` or
//    `WaitForNotificationWithTimeout()`.

#ifndef ABSL_SYNCHRONIZATION_NOTIFICATION_H_
#define ABSL_SYNCHRONIZATION_NOTIFICATION_H_

#include <atomic>

#include "absl/base/attributes.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// -----------------------------------------------------------------------------
// Notification
// -----------------------------------------------------------------------------
class Notification {
 public:
  // Initializes the "notified" state to unnotified.
  Notification() : notified_yet_(false) {}
  explicit Notification(bool prenotify) : notified_yet_(prenotify) {}
  Notification(const Notification&) = delete;
  Notification& operator=(const Notification&) = delete;
  ~Notification();

  // Notification::HasBeenNotified()
  //
  // Returns the value of the notification's internal "notified" state.
  ABSL_MUST_USE_RESULT bool HasBeenNotified() const {
    return HasBeenNotifiedInternal(&this->notified_yet_);
  }

  // Notification::WaitForNotification()
  //
  // Blocks the calling thread until the notification's "notified" state is
  // `true`. Note that if `Notify()` has been previously called on this
  // notification, this function will immediately return.
  void WaitForNotification() const;

  // Notification::WaitForNotificationWithTimeout()
  //
  // Blocks until either the notification's "notified" state is `true` (which
  // may occur immediately) or the timeout has elapsed, returning the value of
  // its "notified" state in either case.
  bool WaitForNotificationWithTimeout(absl::Duration timeout) const;

  // Notification::WaitForNotificationWithDeadline()
  //
  // Blocks until either the notification's "notified" state is `true` (which
  // may occur immediately) or the deadline has expired, returning the value of
  // its "notified" state in either case.
  bool WaitForNotificationWithDeadline(absl::Time deadline) const;

  // Notification::Notify()
  //
  // Sets the "notified" state of this notification to `true` and wakes waiting
  // threads. Note: do not call `Notify()` multiple times on the same
  // `Notification`; calling `Notify()` more than once on the same notification
  // results in undefined behavior.
  void Notify();

 private:
  static inline bool HasBeenNotifiedInternal(
      const std::atomic<bool>* notified_yet) {
    return notified_yet->load(std::memory_order_acquire);
  }

  mutable Mutex mutex_;
  std::atomic<bool> notified_yet_;  // written under mutex_
};

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_SYNCHRONIZATION_NOTIFICATION_H_
