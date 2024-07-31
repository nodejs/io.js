#include "json_parser.h"
#include "node_errors.h"
#include "node_v8_platform-inl.h"
#include "util-inl.h"

namespace node {
using v8::Array;
using v8::Context;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::String;
using v8::Value;

JSONParser::JSONParser() {}

bool JSONParser::Parse(const std::string& content) {
  DCHECK(!parsed_);

  Isolate* isolate = isolate_.get();
  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);

  Local<Context> context = Context::New(isolate);
  Context::Scope context_scope(context);

  // It's not a real script, so don't print the source line.
  errors::PrinterTryCatch bootstrapCatch(
      isolate, errors::PrinterTryCatch::kDontPrintSourceLine);
  Local<Value> json_string_value;
  Local<Value> result_value;
  if (!ToV8Value(context, content).ToLocal(&json_string_value) ||
      !json_string_value->IsString() ||
      !v8::JSON::Parse(context, json_string_value.As<String>())
           .ToLocal(&result_value) ||
      !result_value->IsObject()) {
    return false;
  }

  context_.Reset(isolate, context);
  content_.Reset(isolate, result_value.As<Object>());
  parsed_ = true;

  return true;
}

std::optional<std::string> JSONParser::GetTopLevelStringField(
    std::string_view field) {
  Isolate* isolate = isolate_.get();
  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);

  Local<Context> context = context_.Get(isolate);
  Context::Scope context_scope(context);

  Local<Object> content_object = content_.Get(isolate);

  Local<Value> value;
  // It's not a real script, so don't print the source line.
  errors::PrinterTryCatch bootstrapCatch(
      isolate, errors::PrinterTryCatch::kDontPrintSourceLine);
  Local<Value> field_local;
  if (!ToV8Value(context, field, isolate).ToLocal(&field_local)) {
    return {};
  }
  if (!content_object->Get(context, field_local).ToLocal(&value) ||
      !value->IsString()) {
    return {};
  }
  Utf8Value utf8_value(isolate, value);
  return utf8_value.ToString();
}

std::optional<bool> JSONParser::GetTopLevelBoolField(std::string_view field) {
  Isolate* isolate = isolate_.get();
  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);

  Local<Context> context = context_.Get(isolate);
  Context::Scope context_scope(context);

  Local<Object> content_object = content_.Get(isolate);
  Local<Value> value;
  bool has_field;
  // It's not a real script, so don't print the source line.
  errors::PrinterTryCatch bootstrapCatch(
      isolate, errors::PrinterTryCatch::kDontPrintSourceLine);
  Local<Value> field_local;
  if (!ToV8Value(context, field, isolate).ToLocal(&field_local)) {
    return {};
  }
  if (!content_object->Has(context, field_local).To(&has_field)) {
    return {};
  }
  if (!has_field) {
    return false;
  }
  if (!content_object->Get(context, field_local).ToLocal(&value) ||
      !value->IsBoolean()) {
    return {};
  }
  return value->BooleanValue(isolate);
}

std::optional<JSONParser::StringDict> JSONParser::GetTopLevelStringDict(
    std::string_view field) {
  Isolate* isolate = isolate_.get();
  v8::HandleScope handle_scope(isolate);
  Local<Context> context = context_.Get(isolate);
  Local<Object> content_object = content_.Get(isolate);
  Local<Value> value;
  bool has_field;
  // It's not a real script, so don't print the source line.
  errors::PrinterTryCatch bootstrapCatch(
      isolate, errors::PrinterTryCatch::kDontPrintSourceLine);
  Local<Value> field_local;
  if (!ToV8Value(context, field, isolate).ToLocal(&field_local)) {
    return std::nullopt;
  }
  if (!content_object->Has(context, field_local).To(&has_field)) {
    return std::nullopt;
  }
  if (!has_field) {
    return StringDict();
  }
  if (!content_object->Get(context, field_local).ToLocal(&value) ||
      !value->IsObject()) {
    return std::nullopt;
  }
  Local<Object> dict = value.As<Object>();
  Local<Array> keys;
  if (!dict->GetOwnPropertyNames(context).ToLocal(&keys)) {
    return std::nullopt;
  }
  std::unordered_map<std::string, std::string> result;
  uint32_t length = keys->Length();
  for (uint32_t i = 0; i < length; ++i) {
    Local<Value> key;
    Local<Value> value;
    if (!keys->Get(context, i).ToLocal(&key) || !key->IsString())
      return StringDict();
    if (!dict->Get(context, key).ToLocal(&value) || !value->IsString())
      return StringDict();

    Utf8Value key_utf8(isolate, key);
    Utf8Value value_utf8(isolate, value);
    result.emplace(*key_utf8, *value_utf8);
  }
  return result;
}

}  // namespace node
