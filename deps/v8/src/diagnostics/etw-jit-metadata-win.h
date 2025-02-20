// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DIAGNOSTICS_ETW_JIT_METADATA_WIN_H_
#define V8_DIAGNOSTICS_ETW_JIT_METADATA_WIN_H_

#include <string>
#include <utility>

#include "src/libplatform/etw/etw-provider-win.h"

namespace v8 {
namespace internal {
namespace ETWJITInterface {

/*******************************************************************************
Helper templates to create tightly packed metadata of the format expected by the
ETW data structures.
*******************************************************************************/

// All "manifest-free" events should go to channel 11 by default
const uint8_t kManifestFreeChannel = 11;

// Number of metadescriptors. Use this to find out the index of the field
// descriptors in the descriptors_array
const uint8_t kMetaDescriptorsCount = 2;

// ETW control code for capturing state
// https://learn.microsoft.com/en-us/windows/win32/api/evntprov/nc-evntprov-penablecallback
constexpr uint32_t kEtwControlDisable = 0;
constexpr uint32_t kEtwControlEnable = 1;
constexpr uint32_t kEtwControlCaptureState = 2;

// Filtering keyword to find JScript stack-walking events
constexpr uint64_t kJScriptRuntimeKeyword = 1;

constexpr uint16_t kSourceLoadEventID = 41;
constexpr uint16_t kMethodLoadEventID = 9;
constexpr uint16_t kSourceDCStartEventID = 39;
constexpr uint16_t kMethodDCStartEventID = 5;

constexpr unsigned char kTraceLevel = TRACE_LEVEL_INFORMATION;

// Structure to treat a string literal, or char[], as a constexpr byte sequence
template <size_t count>
struct str_bytes {
  template <std::size_t... idx>
  constexpr str_bytes(char const (&str)[count], std::index_sequence<idx...>)
      : bytes{str[idx]...}, size(count) {}

  // Concatenate two str_bytes
  template <std::size_t count1, std::size_t count2, std::size_t... idx1,
            std::size_t... idx2>
  constexpr str_bytes(const str_bytes<count1>& s1, std::index_sequence<idx1...>,
                      const str_bytes<count2>& s2, std::index_sequence<idx2...>)
      : bytes{s1.bytes[idx1]..., s2.bytes[idx2]...}, size(count) {}

  char bytes[count];  // NOLINT
  size_t size;
};

// Specialization for 0 (base case when joining fields)
template <>
struct str_bytes<0> {
  constexpr str_bytes() : bytes{}, size(0) {}
  char bytes[1];  // MSVC doesn't like an array of 0 bytes
  size_t size;
};

// Factory function to simplify creating a str_bytes from a string literal
template <size_t count, typename idx = std::make_index_sequence<count>>
constexpr auto MakeStrBytes(char const (&s)[count]) {
  return str_bytes<count>{s, idx{}};
}

// Concatenates two str_bytes into one
template <std::size_t size1, std::size_t size2>
constexpr auto JoinBytes(const str_bytes<size1>& str1,
                         const str_bytes<size2>& str2) {
  auto idx1 = std::make_index_sequence<size1>();
  auto idx2 = std::make_index_sequence<size2>();
  return str_bytes<size1 + size2>{str1, idx1, str2, idx2};
}

// Creates an str_bytes which is the field name suffixed with the field type
template <size_t count>
constexpr auto Field(char const (&s)[count], uint8_t type) {
  auto field_name = MakeStrBytes(s);
  const char type_arr[1] = {static_cast<char>(type)};
  return JoinBytes(field_name, MakeStrBytes(type_arr));
}

// Creates the ETW event metadata header, which consists of a uint16_t
// representing the total size, and a tag byte (always 0x00 currently).
constexpr auto Header(size_t size) {
  size_t total_size = size + 3;  // total_size includes the 2 byte size + tag
  const char header_bytes[3] = {static_cast<char>(total_size & 0xFF),
                                static_cast<char>(total_size >> 8 & 0xFF),
                                '\0'};
  return MakeStrBytes(header_bytes);
}

// The JoinFields implementations below are a set of overloads for constructing
// a str_bytes representing the concatenated fields from a parameter pack.

// Empty case needed for events with no fields.
constexpr auto JoinFields() { return str_bytes<0>{}; }

// Only one field, or base case when multiple fields.
template <typename T1>
constexpr auto JoinFields(T1 field) {
  return field;
}

// Join two or more fields together.
template <typename T1, typename T2, typename... Ts>
constexpr auto JoinFields(T1 field1, T2 field2, Ts... args) {
  auto bytes = JoinBytes(field1, field2);
  return JoinFields(bytes, args...);
}

// Creates a constexpr char[] representing the fields for an ETW event.
// Declare the variable as `constexpr static auto` and provide the event name,
// followed by a series of `Field` invocations for each field.
//
// Example:
//  constexpr static auto event_fields = EventFields("my1stEvent",
//      Field("MyIntVal", kTypeInt32),
//      Field("MyMsg", kTypeAnsiStr),
//      Field("Address", kTypePointer));
template <std::size_t count, typename... Ts>
constexpr auto EventFields(char const (&name)[count], Ts... field_args) {
  auto name_bytes = MakeStrBytes(name);
  auto fields = JoinFields(field_args...);
  auto data = JoinBytes(name_bytes, fields);

  auto header = Header(data.size);
  return JoinBytes(header, data);
}

constexpr auto EventMetadata(uint16_t id, uint64_t keywords) {
  return EVENT_DESCRIPTOR{id,
                          0,  // Version
                          kManifestFreeChannel,
                          kTraceLevel,             // Level
                          EVENT_TRACE_TYPE_START,  // Opcode
                          0,                       // Task
                          keywords};
}

void SetMetaDescriptors(EVENT_DATA_DESCRIPTOR* data_descriptor,
                        UINT16 const UNALIGNED* traits, const void* metadata,
                        size_t size);

// Base case, no fields left to set
inline void SetFieldDescriptors(EVENT_DATA_DESCRIPTOR* data_descriptors) {}

// Need to declare all the base overloads in advance, as ther bodies may become
// a point of reference for any of the overloads, and only overloads that have
// been seen will be candidates.
template <typename... Ts>
void SetFieldDescriptors(EVENT_DATA_DESCRIPTOR* data_descriptors,
                         const std::wstring& value, const Ts&... rest);
template <typename... Ts>
void SetFieldDescriptors(EVENT_DATA_DESCRIPTOR* data_descriptors,
                         const std::string& value, const Ts&... rest);
template <typename... Ts>
void SetFieldDescriptors(EVENT_DATA_DESCRIPTOR* data_descriptors,
                         const char* value, const Ts&... rest);

// One or more fields to set
template <typename T, typename... Ts>
void SetFieldDescriptors(EVENT_DATA_DESCRIPTOR* data_descriptors,
                         const T& value, const Ts&... rest) {
  EventDataDescCreate(data_descriptors, &value, sizeof(value));
  SetFieldDescriptors(++data_descriptors, rest...);
}

// Specialize for strings
template <typename... Ts>
void SetFieldDescriptors(EVENT_DATA_DESCRIPTOR* data_descriptors,
                         const std::wstring& value, const Ts&... rest) {
  EventDataDescCreate(data_descriptors, value.data(),
                      static_cast<ULONG>(value.size() * 2 + 2));
  SetFieldDescriptors(++data_descriptors, rest...);
}

template <typename... Ts>
void SetFieldDescriptors(EVENT_DATA_DESCRIPTOR* data_descriptors,
                         const std::string& value, const Ts&... rest) {
  EventDataDescCreate(data_descriptors, value.data(),
                      static_cast<ULONG>(value.size() + 1));
  SetFieldDescriptors(++data_descriptors, rest...);
}

template <typename... Ts>
void SetFieldDescriptors(EVENT_DATA_DESCRIPTOR* data_descriptors,
                         const char* value, const Ts&... rest) {
  ULONG size = static_cast<ULONG>(strlen(value) + 1);
  EventDataDescCreate(data_descriptors, value, size);
  SetFieldDescriptors(++data_descriptors, rest...);
}

// This function does the actual writing of the event via the Win32 API
inline ULONG LogEvent(uint64_t regHandle,
                      const EVENT_DESCRIPTOR* event_descriptor,
                      EVENT_DATA_DESCRIPTOR* data_descriptor,
                      ULONG desc_count) {
  if (regHandle == 0) return ERROR_SUCCESS;
  return EventWriteTransfer(regHandle, event_descriptor, NULL /* ActivityId */,
                            NULL /* RelatedActivityId */, desc_count,
                            data_descriptor);
}

// This template is called by the provider implementation
template <typename T, typename... Fs>
void LogEventData(const TraceLoggingHProvider provider,
                  const EVENT_DESCRIPTOR* event_descriptor, T* meta,
                  const Fs&... fields) {
  const size_t descriptor_count = sizeof...(fields) + kMetaDescriptorsCount;
  EVENT_DATA_DESCRIPTOR descriptors[sizeof...(fields) + kMetaDescriptorsCount];

  SetMetaDescriptors(descriptors, provider->ProviderMetadataPtr, meta->bytes,
                     meta->size);

  EVENT_DATA_DESCRIPTOR* data_descriptors = descriptors + kMetaDescriptorsCount;
  SetFieldDescriptors(data_descriptors, fields...);

  LogEvent(provider->RegHandle, event_descriptor, descriptors,
           descriptor_count);
}

}  // namespace ETWJITInterface
}  // namespace internal
}  // namespace v8

#endif  // V8_DIAGNOSTICS_ETW_JIT_METADATA_WIN_H_
