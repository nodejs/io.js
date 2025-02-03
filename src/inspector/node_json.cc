#include "node_json.h"

#include "crdtp/json.h"

namespace node {
namespace inspector {

using crdtp::ParserHandler;
using crdtp::span;
using crdtp::Status;
using protocol::Binary;
using protocol::BinaryValue;
using protocol::DictionaryValue;
using protocol::FundamentalValue;
using protocol::ListValue;
using protocol::String;
using protocol::StringUtil;
using protocol::StringValue;
using protocol::Value;

namespace {

// Uses the parsing events received from driver of `ParserHandler`
// (e.g. crdtp::json::ParseJSON) into a protocol::Value instance.
class ValueParserHandler : public ParserHandler {
 public:
  // Provides the parsed protocol::Value.
  std::unique_ptr<Value> ReleaseRoot() { return std::move(root_); }

  // The first parsing error encountered; `status().ok()` is the default.
  Status status() const { return status_; }

 private:
  // Implementation of `ParserHandler`.
  void HandleMapBegin() override {
    if (!status_.ok()) return;
    std::unique_ptr<DictionaryValue> dict = DictionaryValue::create();
    DictionaryValue* dict_ptr = dict.get();
    AddValueToParent(std::move(dict));
    stack_.emplace_back(dict_ptr);
  }

  void HandleMapEnd() override {
    if (!status_.ok()) return;
    DCHECK(!stack_.empty());
    DCHECK(stack_.back().is_dict);
    stack_.pop_back();
  }

  void HandleArrayBegin() override {
    if (!status_.ok()) return;
    std::unique_ptr<ListValue> list = ListValue::create();
    ListValue* list_ptr = list.get();
    AddValueToParent(std::move(list));
    stack_.emplace_back(list_ptr);
  }

  void HandleArrayEnd() override {
    if (!status_.ok()) return;
    DCHECK(!stack_.empty());
    DCHECK(!stack_.back().is_dict);
    stack_.pop_back();
  }

  void HandleString8(span<uint8_t> chars) override {
    AddStringToParent(StringUtil::fromUTF8(chars.data(), chars.size()));
  }

  void HandleString16(span<uint16_t> chars) override {
    AddStringToParent(StringUtil::fromUTF16(chars.data(), chars.size()));
  }

  void HandleBinary(span<uint8_t> bytes) override {
    AddValueToParent(
        BinaryValue::create(Binary::fromSpan(bytes.data(), bytes.size())));
  }

  void HandleDouble(double value) override {
    AddValueToParent(FundamentalValue::create(value));
  }

  void HandleInt32(int32_t value) override {
    AddValueToParent(FundamentalValue::create(value));
  }

  void HandleBool(bool value) override {
    AddValueToParent(FundamentalValue::create(value));
  }

  void HandleNull() override { AddValueToParent(Value::null()); }

  void HandleError(Status error) override { status_ = error; }

  // Adding strings and values to the parent value.
  // Strings are handled separately because they can be keys for
  // dictionary values.
  void AddStringToParent(String str) {
    if (!status_.ok()) return;
    if (!root_) {
      DCHECK(!key_is_pending_);
      root_ = StringValue::create(str);
    } else if (stack_.back().is_dict) {
      // If we already have a pending key, then this is the value of the
      // key/value pair. Otherwise, it's the new pending key.
      if (key_is_pending_) {
        stack_.back().dict->setString(pending_key_, str);
        key_is_pending_ = false;
      } else {
        pending_key_ = std::move(str);
        key_is_pending_ = true;
      }
    } else {  // Top of the stack is a list.
      DCHECK(!key_is_pending_);
      stack_.back().list->pushValue(StringValue::create(str));
    }
  }

  void AddValueToParent(std::unique_ptr<Value> value) {
    if (!status_.ok()) return;
    if (!root_) {
      DCHECK(!key_is_pending_);
      root_ = std::move(value);
    } else if (stack_.back().is_dict) {
      DCHECK(key_is_pending_);
      stack_.back().dict->setValue(pending_key_, std::move(value));
      key_is_pending_ = false;
    } else {  // Top of the stack is a list.
      DCHECK(!key_is_pending_);
      stack_.back().list->pushValue(std::move(value));
    }
  }

  // `status_.ok()` is the default; if we receive an error event
  // we keep the first one and stop modifying any other state.
  Status status_;

  // The root of the parsed protocol::Value tree.
  std::unique_ptr<Value> root_;

  // If root_ is a list or a dictionary, this stack keeps track of
  // the container we're currently parsing as well as its ancestors.
  struct ContainerState {
    explicit ContainerState(DictionaryValue* dict)
        : is_dict(true), dict(dict) {}
    explicit ContainerState(ListValue* list) : is_dict(false), list(list) {}

    bool is_dict;
    union {
      DictionaryValue* dict;
      ListValue* list;
    };
  };
  std::vector<ContainerState> stack_;

  // For maps, keys and values are alternating events, so we keep the
  // key around and process it when the value arrives.
  bool key_is_pending_ = false;
  String pending_key_;
};
}  // anonymous namespace

std::unique_ptr<Value> JsonUtil::ParseJSON(const uint8_t* chars, size_t size) {
  ValueParserHandler handler;
  crdtp::json::ParseJSON(span<uint8_t>(chars, size), &handler);
  if (handler.status().ok()) return handler.ReleaseRoot();
  return nullptr;
}

std::unique_ptr<Value> JsonUtil::ParseJSON(const uint16_t* chars, size_t size) {
  ValueParserHandler handler;
  crdtp::json::ParseJSON(span<uint16_t>(chars, size), &handler);
  if (handler.status().ok()) return handler.ReleaseRoot();
  return nullptr;
}

std::unique_ptr<Value> JsonUtil::parseJSON(const std::string_view string) {
  if (string.empty()) return nullptr;

  return ParseJSON(reinterpret_cast<const uint8_t*>(string.data()),
                   string.size());
}

std::unique_ptr<Value> JsonUtil::parseJSON(v8_inspector::StringView string) {
  if (string.length() == 0) return nullptr;
  if (string.is8Bit()) return ParseJSON(string.characters8(), string.length());
  return ParseJSON(string.characters16(), string.length());
}

}  // namespace inspector
}  // namespace node
