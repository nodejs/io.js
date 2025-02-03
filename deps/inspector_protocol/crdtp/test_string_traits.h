// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRDTP_TEST_STRING_TRAITS_H_

#include "protocol_core.h"

namespace crdtp {

// Either real string traits or dummy string traits are going to be used
// depending on whether this is built standalone or with embedder.
template <>
struct ProtocolTypeTraits<std::string> {
  static bool Deserialize(DeserializerState* state, std::string* value);
  static void Serialize(const std::string& value, std::vector<uint8_t>* bytes);
};

}  // namespace crdtp

#endif  // CRDTP_TEST_STRING_TRAITS_H_
