// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_string_traits.h"

namespace crdtp {

// Test-only. Real-life bindings use UTF8/16 conversions as needed.
bool ProtocolTypeTraits<std::string>::Deserialize(DeserializerState* state,
                                                  std::string* value) {
  if (state->tokenizer()->TokenTag() == cbor::CBORTokenTag::STRING8) {
    auto cbor_span = state->tokenizer()->GetString8();
    value->assign(reinterpret_cast<const char*>(cbor_span.data()),
                  cbor_span.size());
    return true;
  }
  state->RegisterError(Error::BINDINGS_STRING8_VALUE_EXPECTED);
  return false;
}

// static
void ProtocolTypeTraits<std::string>::Serialize(const std::string& value,
                                                std::vector<uint8_t>* bytes) {
  cbor::EncodeString8(
      span<uint8_t>(reinterpret_cast<const uint8_t*>(value.data()),
                    value.size()),
      bytes);
}

}  // namespace crdtp
