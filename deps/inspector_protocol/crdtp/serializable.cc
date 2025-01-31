// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "serializable.h"

#include <utility>

namespace crdtp {
// =============================================================================
// Serializable - An object to be emitted as a sequence of bytes.
// =============================================================================

std::vector<uint8_t> Serializable::Serialize() const {
  std::vector<uint8_t> out;
  AppendSerialized(&out);
  return out;
}

namespace {
class PreSerialized : public Serializable {
 public:
  explicit PreSerialized(std::vector<uint8_t> bytes)
      : bytes_(std::move(bytes)) {}

  void AppendSerialized(std::vector<uint8_t>* out) const override {
    out->insert(out->end(), bytes_.begin(), bytes_.end());
  }

 private:
  std::vector<uint8_t> bytes_;
};
}  // namespace

// static
std::unique_ptr<Serializable> Serializable::From(std::vector<uint8_t> bytes) {
  return std::make_unique<PreSerialized>(std::move(bytes));
}
}  // namespace crdtp
