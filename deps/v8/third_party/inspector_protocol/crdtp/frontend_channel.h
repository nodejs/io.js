// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CRDTP_FRONTEND_CHANNEL_H_
#define V8_CRDTP_FRONTEND_CHANNEL_H_

#include <cstdint>
#include <memory>
#include "export.h"
#include "serializable.h"
#include "span.h"

namespace v8_crdtp {
// =============================================================================
// FrontendChannel - For sending notifications and responses to protocol clients
// =============================================================================
class FrontendChannel {
 public:
  virtual ~FrontendChannel() = default;

  // Sends protocol responses and notifications. The |call_id| parameter is
  // seemingly redundant because it's also included in the message, but
  // responses may be sent from an untrusted source to a trusted process (e.g.
  // from Chromium's renderer (blink) to the browser process), which needs
  // to be able to match the response to an earlier request without parsing the
  // message.
  virtual void SendProtocolResponse(int call_id,
                                    std::unique_ptr<Serializable> message) = 0;
  virtual void SendProtocolNotification(
      std::unique_ptr<Serializable> message) = 0;

  // FallThrough indicates that |message| should be handled in another layer.
  // Usually this means the layer responding to the message didn't handle it,
  // but in some cases messages are handled by multiple layers (e.g. both
  // the embedder and the content layer in Chromium).
  virtual void FallThrough(int call_id,
                           span<uint8_t> method,
                           span<uint8_t> message) = 0;

  // Session implementations may queue notifications for performance or
  // other considerations; this is a hook for domain handlers to manually flush.
  virtual void FlushProtocolNotifications() = 0;
};
}  // namespace v8_crdtp

#endif  // V8_CRDTP_FRONTEND_CHANNEL_H_
