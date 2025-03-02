// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "protocol_core.h"

#include <algorithm>
#include <cassert>
#include <string>

namespace v8_crdtp {

DeserializerState::DeserializerState(std::vector<uint8_t> bytes)
    : storage_(new std::vector<uint8_t>(std::move(bytes))),
      tokenizer_(span<uint8_t>(storage_->data(), storage_->size())) {}

DeserializerState::DeserializerState(Storage storage, span<uint8_t> span)
    : storage_(std::move(storage)), tokenizer_(span) {}

void DeserializerState::RegisterError(Error error) {
  assert(Error::OK != error);
  if (tokenizer_.Status().ok())
    status_ = Status{error, tokenizer_.Status().pos};
}

void DeserializerState::RegisterFieldPath(span<char> name) {
  field_path_.push_back(name);
}

std::string DeserializerState::ErrorMessage(span<char> message_name) const {
  std::string msg = "Failed to deserialize ";
  msg.append(message_name.begin(), message_name.end());
  for (int field = static_cast<int>(field_path_.size()) - 1; field >= 0;
       --field) {
    msg.append(".");
    msg.append(field_path_[field].begin(), field_path_[field].end());
  }
  Status s = status();
  if (!s.ok())
    msg += " - " + s.ToASCIIString();
  return msg;
}

Status DeserializerState::status() const {
  if (!tokenizer_.Status().ok())
    return tokenizer_.Status();
  return status_;
}

namespace {
constexpr int32_t GetMandatoryFieldMask(
    const DeserializerDescriptor::Field* fields,
    size_t count) {
  int32_t mask = 0;
  for (size_t i = 0; i < count; ++i) {
    if (!fields[i].is_optional)
      mask |= (1 << i);
  }
  return mask;
}
}  // namespace

DeserializerDescriptor::DeserializerDescriptor(const Field* fields,
                                               size_t field_count)
    : fields_(fields),
      field_count_(field_count),
      mandatory_field_mask_(GetMandatoryFieldMask(fields, field_count)) {}

bool DeserializerDescriptor::Deserialize(DeserializerState* state,
                                         void* obj) const {
  auto* tokenizer = state->tokenizer();

  // As a special compatibility quirk, allow empty objects if
  // no mandatory fields are required.
  if (tokenizer->TokenTag() == cbor::CBORTokenTag::DONE &&
      !mandatory_field_mask_) {
    return true;
  }
  if (tokenizer->TokenTag() == cbor::CBORTokenTag::ENVELOPE)
    tokenizer->EnterEnvelope();
  if (tokenizer->TokenTag() != cbor::CBORTokenTag::MAP_START) {
    state->RegisterError(Error::CBOR_MAP_START_EXPECTED);
    return false;
  }
  tokenizer->Next();
  int32_t seen_mandatory_fields = 0;
  for (; tokenizer->TokenTag() != cbor::CBORTokenTag::STOP; tokenizer->Next()) {
    if (tokenizer->TokenTag() != cbor::CBORTokenTag::STRING8) {
      state->RegisterError(Error::CBOR_INVALID_MAP_KEY);
      return false;
    }
    span<uint8_t> u_key = tokenizer->GetString8();
    span<char> key(reinterpret_cast<const char*>(u_key.data()), u_key.size());
    tokenizer->Next();
    if (!DeserializeField(state, key, &seen_mandatory_fields, obj))
      return false;
  }
  // Only compute mandatory fields once per type.
  int32_t missing_fields = seen_mandatory_fields ^ mandatory_field_mask_;
  if (missing_fields) {
    int32_t idx = 0;
    while ((missing_fields & 1) == 0) {
      missing_fields >>= 1;
      ++idx;
    }
    state->RegisterError(Error::BINDINGS_MANDATORY_FIELD_MISSING);
    state->RegisterFieldPath(fields_[idx].name);
    return false;
  }
  return true;
}

bool DeserializerDescriptor::DeserializeField(DeserializerState* state,
                                              span<char> name,
                                              int* seen_mandatory_fields,
                                              void* obj) const {
  // TODO(caseq): consider checking if the sought field is the one
  // after the last deserialized.
  const auto* begin = fields_;
  const auto* end = fields_ + field_count_;
  auto entry = std::lower_bound(
      begin, end, name, [](const Field& field_desc, span<char> field_name) {
        return SpanLessThan(field_desc.name, field_name);
      });
  // Unknown field is not an error -- we may be working against an
  // implementation of a later version of the protocol.
  // TODO(caseq): support unknown arrays and maps not enclosed by an envelope.
  if (entry == end || !SpanEquals(entry->name, name))
    return true;
  if (!entry->deserializer(state, obj)) {
    state->RegisterFieldPath(name);
    return false;
  }
  if (!entry->is_optional)
    *seen_mandatory_fields |= 1 << (entry - begin);
  return true;
}

bool ProtocolTypeTraits<bool>::Deserialize(DeserializerState* state,
                                           bool* value) {
  const auto tag = state->tokenizer()->TokenTag();
  if (tag == cbor::CBORTokenTag::TRUE_VALUE) {
    *value = true;
    return true;
  }
  if (tag == cbor::CBORTokenTag::FALSE_VALUE) {
    *value = false;
    return true;
  }
  state->RegisterError(Error::BINDINGS_BOOL_VALUE_EXPECTED);
  return false;
}

void ProtocolTypeTraits<bool>::Serialize(bool value,
                                         std::vector<uint8_t>* bytes) {
  bytes->push_back(value ? cbor::EncodeTrue() : cbor::EncodeFalse());
}

bool ProtocolTypeTraits<int32_t>::Deserialize(DeserializerState* state,
                                              int32_t* value) {
  if (state->tokenizer()->TokenTag() != cbor::CBORTokenTag::INT32) {
    state->RegisterError(Error::BINDINGS_INT32_VALUE_EXPECTED);
    return false;
  }
  *value = state->tokenizer()->GetInt32();
  return true;
}

void ProtocolTypeTraits<int32_t>::Serialize(int32_t value,
                                            std::vector<uint8_t>* bytes) {
  cbor::EncodeInt32(value, bytes);
}

ContainerSerializer::ContainerSerializer(std::vector<uint8_t>* bytes,
                                         uint8_t tag)
    : bytes_(bytes) {
  envelope_.EncodeStart(bytes_);
  bytes_->push_back(tag);
}

void ContainerSerializer::EncodeStop() {
  bytes_->push_back(cbor::EncodeStop());
  envelope_.EncodeStop(bytes_);
}

ObjectSerializer::ObjectSerializer()
    : serializer_(&owned_bytes_, cbor::EncodeIndefiniteLengthMapStart()) {}

ObjectSerializer::~ObjectSerializer() = default;

std::unique_ptr<Serializable> ObjectSerializer::Finish() {
  serializer_.EncodeStop();
  return Serializable::From(std::move(owned_bytes_));
}

bool ProtocolTypeTraits<double>::Deserialize(DeserializerState* state,
                                             double* value) {
  // Double values that round-trip through JSON may end up getting represented
  // as an int32 (SIGNED, UNSIGNED) on the wire in CBOR. Therefore, we also
  // accept an INT32 here.
  if (state->tokenizer()->TokenTag() == cbor::CBORTokenTag::INT32) {
    *value = state->tokenizer()->GetInt32();
    return true;
  }
  if (state->tokenizer()->TokenTag() != cbor::CBORTokenTag::DOUBLE) {
    state->RegisterError(Error::BINDINGS_DOUBLE_VALUE_EXPECTED);
    return false;
  }
  *value = state->tokenizer()->GetDouble();
  return true;
}

void ProtocolTypeTraits<double>::Serialize(double value,
                                           std::vector<uint8_t>* bytes) {
  cbor::EncodeDouble(value, bytes);
}

class IncomingDeferredMessage : public DeferredMessage {
 public:
  // Creates the state from the part of another message.
  // Note storage is opaque and is mostly to retain ownership.
  // It may be null in case caller owns the memory and will dispose
  // of the message synchronously.
  IncomingDeferredMessage(DeserializerState::Storage storage,
                          span<uint8_t> span)
      : storage_(storage), span_(span) {}

 private:
  DeserializerState MakeDeserializer() const override {
    return DeserializerState(storage_, span_);
  }
  void AppendSerialized(std::vector<uint8_t>* out) const override {
    out->insert(out->end(), span_.begin(), span_.end());
  }

  DeserializerState::Storage storage_;
  span<uint8_t> span_;
};

class OutgoingDeferredMessage : public DeferredMessage {
 public:
  OutgoingDeferredMessage() = default;
  explicit OutgoingDeferredMessage(std::unique_ptr<Serializable> serializable)
      : serializable_(std::move(serializable)) {
    assert(!!serializable_);
  }

 private:
  DeserializerState MakeDeserializer() const override {
    return DeserializerState(serializable_->Serialize());
  }
  void AppendSerialized(std::vector<uint8_t>* out) const override {
    serializable_->AppendSerialized(out);
  }

  std::unique_ptr<Serializable> serializable_;
};

// static
std::unique_ptr<DeferredMessage> DeferredMessage::FromSerializable(
    std::unique_ptr<Serializable> serializeable) {
  return std::make_unique<OutgoingDeferredMessage>(std::move(serializeable));
}

// static
std::unique_ptr<DeferredMessage> DeferredMessage::FromSpan(
    span<uint8_t> bytes) {
  return std::make_unique<IncomingDeferredMessage>(nullptr, bytes);
}

bool ProtocolTypeTraits<std::unique_ptr<DeferredMessage>>::Deserialize(
    DeserializerState* state,
    std::unique_ptr<DeferredMessage>* value) {
  if (state->tokenizer()->TokenTag() != cbor::CBORTokenTag::ENVELOPE) {
    state->RegisterError(Error::CBOR_INVALID_ENVELOPE);
    return false;
  }
  *value = std::make_unique<IncomingDeferredMessage>(
      state->storage(), state->tokenizer()->GetEnvelope());
  return true;
}

void ProtocolTypeTraits<DeferredMessage>::Serialize(
    const DeferredMessage& value,
    std::vector<uint8_t>* bytes) {
  value.AppendSerialized(bytes);
}

}  // namespace v8_crdtp
