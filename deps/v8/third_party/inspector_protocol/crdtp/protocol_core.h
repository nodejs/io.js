// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CRDTP_PROTOCOL_CORE_H_
#define V8_CRDTP_PROTOCOL_CORE_H_

#include <sys/types.h>

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "cbor.h"
#include "serializable.h"
#include "span.h"
#include "status.h"

namespace v8_crdtp {

class DeserializerState {
 public:
  using Storage = std::shared_ptr<const std::vector<uint8_t>>;

  // Creates a state from the raw bytes received from the peer.
  explicit DeserializerState(std::vector<uint8_t> bytes);
  // Creates the state from the part of another message.
  DeserializerState(Storage storage, span<uint8_t> span);
  DeserializerState(const DeserializerState& r) = delete;
  DeserializerState(DeserializerState&& r) = default;

  // Registers |error|, unless the tokenizer's status is already an error.
  void RegisterError(Error error);
  // Registers |name| as a segment of the field path.
  void RegisterFieldPath(span<char> name);

  // Produces an error message considering |tokenizer.Status()|,
  // status_, and field_path_.
  std::string ErrorMessage(span<char> message_name) const;
  Status status() const;
  const Storage& storage() const { return storage_; }
  cbor::CBORTokenizer* tokenizer() { return &tokenizer_; }

 private:
  const Storage storage_;
  cbor::CBORTokenizer tokenizer_;
  Status status_;
  std::vector<span<char>> field_path_;
};

template <typename T, typename = void>
struct ProtocolTypeTraits {};

template <>
struct ProtocolTypeTraits<bool> {
  static bool Deserialize(DeserializerState* state, bool* value);
  static void Serialize(bool value, std::vector<uint8_t>* bytes);
};

template <>
struct ProtocolTypeTraits<int32_t> {
  static bool Deserialize(DeserializerState* state, int* value);
  static void Serialize(int value, std::vector<uint8_t>* bytes);
};

template <>
struct ProtocolTypeTraits<double> {
  static bool Deserialize(DeserializerState* state, double* value);
  static void Serialize(double value, std::vector<uint8_t>* bytes);
};

class ContainerSerializer {
 public:
  ContainerSerializer(std::vector<uint8_t>* bytes, uint8_t tag);

  template <typename T>
  void AddField(span<char> field_name, const T& value) {
    cbor::EncodeString8(
        span<uint8_t>(reinterpret_cast<const uint8_t*>(field_name.data()),
                      field_name.size()),
        bytes_);
    ProtocolTypeTraits<T>::Serialize(value, bytes_);
  }
  template <typename T>
  void AddField(span<char> field_name, const std::optional<T>& value) {
    if (!value.has_value()) {
      return;
    }
    AddField(field_name, value.value());
  }

  template <typename T>
  void AddField(span<char> field_name, const std::unique_ptr<T>& value) {
    if (!value) {
      return;
    }
    AddField(field_name, *value);
  }

  void EncodeStop();

 private:
  std::vector<uint8_t>* const bytes_;
  cbor::EnvelopeEncoder envelope_;
};

class ObjectSerializer {
 public:
  ObjectSerializer();
  ~ObjectSerializer();

  template <typename T>
  void AddField(span<char> name, const T& field) {
    serializer_.AddField(name, field);
  }
  std::unique_ptr<Serializable> Finish();

 private:
  std::vector<uint8_t> owned_bytes_;
  ContainerSerializer serializer_;
};

class DeserializerDescriptor {
 public:
  struct Field {
    span<char> name;
    bool is_optional;
    bool (*deserializer)(DeserializerState* state, void* obj);
  };

  DeserializerDescriptor(const Field* fields, size_t field_count);

  bool Deserialize(DeserializerState* state, void* obj) const;

 private:
  bool DeserializeField(DeserializerState* state,
                        span<char> name,
                        int* seen_mandatory_fields,
                        void* obj) const;

  const Field* const fields_;
  const size_t field_count_;
  const int mandatory_field_mask_;
};

template <typename T>
struct ProtocolTypeTraits<std::vector<T>> {
  static bool Deserialize(DeserializerState* state, std::vector<T>* value) {
    auto* tokenizer = state->tokenizer();
    if (tokenizer->TokenTag() == cbor::CBORTokenTag::ENVELOPE)
      tokenizer->EnterEnvelope();
    if (tokenizer->TokenTag() != cbor::CBORTokenTag::ARRAY_START) {
      state->RegisterError(Error::CBOR_ARRAY_START_EXPECTED);
      return false;
    }
    assert(value->empty());
    tokenizer->Next();
    for (; tokenizer->TokenTag() != cbor::CBORTokenTag::STOP;
         tokenizer->Next()) {
      value->emplace_back();
      if (!ProtocolTypeTraits<T>::Deserialize(state, &value->back()))
        return false;
    }
    return true;
  }

  static void Serialize(const std::vector<T>& value,
                        std::vector<uint8_t>* bytes) {
    ContainerSerializer container_serializer(
        bytes, cbor::EncodeIndefiniteLengthArrayStart());
    for (const auto& item : value)
      ProtocolTypeTraits<T>::Serialize(item, bytes);
    container_serializer.EncodeStop();
  }
};

template <typename T>
struct ProtocolTypeTraits<std::unique_ptr<std::vector<T>>> {
  static bool Deserialize(DeserializerState* state,
                          std::unique_ptr<std::vector<T>>* value) {
    auto res = std::make_unique<std::vector<T>>();
    if (!ProtocolTypeTraits<std::vector<T>>::Deserialize(state, res.get()))
      return false;
    *value = std::move(res);
    return true;
  }
  static void Serialize(const std::unique_ptr<std::vector<T>>& value,
                        std::vector<uint8_t>* bytes) {
    ProtocolTypeTraits<std::vector<T>>::Serialize(*value, bytes);
  }
};

class DeferredMessage : public Serializable {
 public:
  static std::unique_ptr<DeferredMessage> FromSerializable(
      std::unique_ptr<Serializable> serializeable);
  static std::unique_ptr<DeferredMessage> FromSpan(span<uint8_t> bytes);

  ~DeferredMessage() override = default;
  virtual DeserializerState MakeDeserializer() const = 0;

 protected:
  DeferredMessage() = default;
};

template <>
struct ProtocolTypeTraits<std::unique_ptr<DeferredMessage>> {
  static bool Deserialize(DeserializerState* state,
                          std::unique_ptr<DeferredMessage>* value);
};

template <>
struct ProtocolTypeTraits<DeferredMessage> {
  static void Serialize(const DeferredMessage& value,
                        std::vector<uint8_t>* bytes);
};

template <typename T>
struct ProtocolTypeTraits<std::optional<T>> {
  static bool Deserialize(DeserializerState* state, std::optional<T>* value) {
    T res;
    if (!ProtocolTypeTraits<T>::Deserialize(state, &res))
      return false;
    *value = std::move(res);
    return true;
  }

  static void Serialize(const std::optional<T>& value,
                        std::vector<uint8_t>* bytes) {
    ProtocolTypeTraits<T>::Serialize(value.value(), bytes);
  }
};

template <typename T>
class DeserializableProtocolObject {
 public:
  static StatusOr<std::unique_ptr<T>> ReadFrom(
      const DeferredMessage& deferred_message) {
    auto state = deferred_message.MakeDeserializer();
    if (auto res = Deserialize(&state))
      return StatusOr<std::unique_ptr<T>>(std::move(res));
    return StatusOr<std::unique_ptr<T>>(state.status());
  }

  static StatusOr<std::unique_ptr<T>> ReadFrom(std::vector<uint8_t> bytes) {
    auto state = DeserializerState(std::move(bytes));
    if (auto res = Deserialize(&state))
      return StatusOr<std::unique_ptr<T>>(std::move(res));
    return StatusOr<std::unique_ptr<T>>(state.status());
  }

  // Short-hand for legacy clients. This would swallow any errors, consider
  // using ReadFrom.
  static std::unique_ptr<T> FromBinary(const uint8_t* bytes, size_t size) {
    std::unique_ptr<T> value(new T());
    auto deserializer = DeferredMessage::FromSpan(span<uint8_t>(bytes, size))
                            ->MakeDeserializer();
    std::ignore = Deserialize(&deserializer, value.get());
    return value;
  }

  [[nodiscard]] static bool Deserialize(DeserializerState* state, T* value) {
    return T::deserializer_descriptor().Deserialize(state, value);
  }

 protected:
  // This is for the sake of the macros used by derived classes thay may be in
  // a different namespace;
  using ProtocolType = T;
  using DeserializerDescriptorType = DeserializerDescriptor;
  template <typename U>
  using DeserializableBase = DeserializableProtocolObject<U>;

  DeserializableProtocolObject() = default;
  ~DeserializableProtocolObject() = default;

 private:
  friend struct ProtocolTypeTraits<std::unique_ptr<T>>;

  static std::unique_ptr<T> Deserialize(DeserializerState* state) {
    std::unique_ptr<T> value(new T());
    if (Deserialize(state, value.get()))
      return value;
    return nullptr;
  }
};

template <typename T>
class ProtocolObject : public Serializable,
                       public DeserializableProtocolObject<T> {
 public:
  std::unique_ptr<T> Clone() const {
    std::vector<uint8_t> serialized;
    AppendSerialized(&serialized);
    return T::ReadFrom(std::move(serialized)).value();
  }

 protected:
  using ProtocolType = T;

  ProtocolObject() = default;
};

template <typename T>
struct ProtocolTypeTraits<
    T,
    typename std::enable_if<
        std::is_base_of<ProtocolObject<T>, T>::value>::type> {
  static bool Deserialize(DeserializerState* state, T* value) {
    return T::Deserialize(state, value);
  }

  static void Serialize(const T& value, std::vector<uint8_t>* bytes) {
    value.AppendSerialized(bytes);
  }
};

template <typename T>
struct ProtocolTypeTraits<
    std::unique_ptr<T>,
    typename std::enable_if<
        std::is_base_of<ProtocolObject<T>, T>::value>::type> {
  static bool Deserialize(DeserializerState* state, std::unique_ptr<T>* value) {
    std::unique_ptr<T> res = T::Deserialize(state);
    if (!res)
      return false;
    *value = std::move(res);
    return true;
  }

  static void Serialize(const std::unique_ptr<T>& value,
                        std::vector<uint8_t>* bytes) {
    ProtocolTypeTraits<T>::Serialize(*value, bytes);
  }
};

template <typename T, typename F>
bool ConvertProtocolValue(const F& from, T* to) {
  std::vector<uint8_t> bytes;
  ProtocolTypeTraits<F>::Serialize(from, &bytes);
  auto deserializer =
      DeferredMessage::FromSpan(span<uint8_t>(bytes.data(), bytes.size()))
          ->MakeDeserializer();
  return ProtocolTypeTraits<T>::Deserialize(&deserializer, to);
}

#define DECLARE_DESERIALIZATION_SUPPORT()  \
  friend DeserializableBase<ProtocolType>; \
  static const DeserializerDescriptorType& deserializer_descriptor()

#define DECLARE_SERIALIZATION_SUPPORT()                              \
 public:                                                             \
  void AppendSerialized(std::vector<uint8_t>* bytes) const override; \
                                                                     \
 private:                                                            \
  friend DeserializableBase<ProtocolType>;                           \
  static const DeserializerDescriptorType& deserializer_descriptor()

#define V8_CRDTP_DESERIALIZE_FILED_IMPL(name, field, is_optional)  \
  {                                                                \
    MakeSpan(name), is_optional,                                   \
        [](DeserializerState* __state, void* __obj) -> bool {      \
          return ProtocolTypeTraits<decltype(field)>::Deserialize( \
              __state, &static_cast<ProtocolType*>(__obj)->field); \
        }                                                          \
  }

// clang-format off
#define V8_CRDTP_BEGIN_DESERIALIZER(type)                                      \
  const type::DeserializerDescriptorType& type::deserializer_descriptor() { \
    using namespace v8_crdtp;                                                  \
    static const DeserializerDescriptorType::Field fields[] = {

#define V8_CRDTP_END_DESERIALIZER()                    \
    };                                              \
    static const DeserializerDescriptorType s_desc( \
        fields, sizeof fields / sizeof fields[0]);  \
    return s_desc;                                  \
  }

#define V8_CRDTP_DESERIALIZE_FIELD(name, field) \
  V8_CRDTP_DESERIALIZE_FILED_IMPL(name, field, false)
#define V8_CRDTP_DESERIALIZE_FIELD_OPT(name, field) \
  V8_CRDTP_DESERIALIZE_FILED_IMPL(name, field, true)

#define V8_CRDTP_BEGIN_SERIALIZER(type)                               \
  void type::AppendSerialized(std::vector<uint8_t>* bytes) const { \
    using namespace v8_crdtp;                                         \
    ContainerSerializer __serializer(bytes,                        \
                                     cbor::EncodeIndefiniteLengthMapStart());

#define V8_CRDTP_SERIALIZE_FIELD(name, field) \
    __serializer.AddField(MakeSpan(name), field)

#define V8_CRDTP_END_SERIALIZER() \
    __serializer.EncodeStop();   \
  } class __cddtp_dummy_name
// clang-format on

}  // namespace v8_crdtp

#endif  // V8_CRDTP_PROTOCOL_CORE_H_
