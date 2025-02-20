// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-module-sourcemap.h"

#include <algorithm>

#include "include/v8-context.h"
#include "include/v8-json.h"
#include "include/v8-local-handle.h"
#include "include/v8-object.h"
#include "include/v8-primitive.h"
#include "src/base/vlq-base64.h"
#include "src/wasm/std-object-sizes.h"

namespace v8 {

class String;

namespace internal {
namespace wasm {

WasmModuleSourceMap::WasmModuleSourceMap(v8::Isolate* v8_isolate,
                                         v8::Local<v8::String> src_map_str) {
  v8::HandleScope scope(v8_isolate);
  v8::Local<v8::Context> context = v8::Context::New(v8_isolate);

  v8::Local<v8::Value> src_map_value;
  if (!v8::JSON::Parse(context, src_map_str).ToLocal(&src_map_value)) return;
  v8::Local<v8::Object> src_map_obj =
      v8::Local<v8::Object>::Cast(src_map_value);

  v8::Local<v8::Value> version_value, sources_value, mappings_value;
  bool has_valid_version =
      src_map_obj
          ->Get(context, v8::String::NewFromUtf8Literal(v8_isolate, "version"))
          .ToLocal(&version_value) &&
      version_value->IsUint32();
  uint32_t version = 0;
  if (!has_valid_version || !version_value->Uint32Value(context).To(&version) ||
      version != 3u)
    return;

  bool has_valid_sources =
      src_map_obj
          ->Get(context, v8::String::NewFromUtf8Literal(v8_isolate, "sources"))
          .ToLocal(&sources_value) &&
      sources_value->IsArray();
  if (!has_valid_sources) return;

  v8::Local<v8::Object> sources_arr =
      v8::Local<v8::Object>::Cast(sources_value);
  v8::Local<v8::Value> sources_len_value;
  if (!sources_arr
           ->Get(context, v8::String::NewFromUtf8Literal(v8_isolate, "length"))
           .ToLocal(&sources_len_value))
    return;
  uint32_t sources_len = 0;
  if (!sources_len_value->Uint32Value(context).To(&sources_len)) return;

  for (uint32_t i = 0; i < sources_len; ++i) {
    v8::Local<v8::Value> file_name_value;
    if (!sources_arr->Get(context, i).ToLocal(&file_name_value) ||
        !file_name_value->IsString())
      return;
    v8::Local<v8::String> file_name =
        v8::Local<v8::String>::Cast(file_name_value);
    size_t file_name_sz = file_name->Utf8LengthV2(v8_isolate) + 1;
    std::unique_ptr<char[]> file_name_buf(new char[file_name_sz]);
    file_name->WriteUtf8V2(v8_isolate, file_name_buf.get(), file_name_sz,
                           v8::String::WriteFlags::kNullTerminate);
    filenames.emplace_back(file_name_buf.get());
  }

  bool has_valid_mappings =
      src_map_obj
          ->Get(context, v8::String::NewFromUtf8Literal(v8_isolate, "mappings"))
          .ToLocal(&mappings_value) &&
      mappings_value->IsString();
  if (!has_valid_mappings) return;

  v8::Local<v8::String> mappings = v8::Local<v8::String>::Cast(mappings_value);
  size_t mappings_sz = mappings->Utf8LengthV2(v8_isolate) + 1;
  std::unique_ptr<char[]> mappings_buf(new char[mappings_sz]);
  mappings->WriteUtf8V2(v8_isolate, mappings_buf.get(), mappings_sz,
                        v8::String::WriteFlags::kNullTerminate);

  valid_ = DecodeMapping(mappings_buf.get());
}

size_t WasmModuleSourceMap::GetSourceLine(size_t wasm_offset) const {
  std::vector<std::size_t>::const_iterator up =
      std::upper_bound(offsets.begin(), offsets.end(), wasm_offset);
  CHECK_NE(offsets.begin(), up);
  size_t source_idx = up - offsets.begin() - 1;
  return source_row[source_idx];
}

std::string WasmModuleSourceMap::GetFilename(size_t wasm_offset) const {
  std::vector<size_t>::const_iterator up =
      std::upper_bound(offsets.begin(), offsets.end(), wasm_offset);
  CHECK_NE(offsets.begin(), up);
  size_t offset_idx = up - offsets.begin() - 1;
  size_t filename_idx = file_idxs[offset_idx];
  return filenames[filename_idx];
}

bool WasmModuleSourceMap::HasSource(size_t start, size_t end) const {
  return start <= *(offsets.end() - 1) && end > *offsets.begin();
}

bool WasmModuleSourceMap::HasValidEntry(size_t start, size_t addr) const {
  std::vector<size_t>::const_iterator up =
      std::upper_bound(offsets.begin(), offsets.end(), addr);
  if (up == offsets.begin()) return false;
  size_t offset_idx = up - offsets.begin() - 1;
  size_t entry_offset = offsets[offset_idx];
  if (entry_offset < start) return false;
  return true;
}

bool WasmModuleSourceMap::DecodeMapping(const std::string& s) {
  size_t pos = 0, gen_col = 0, file_idx = 0, ori_line = 0;
  int32_t qnt = 0;

  while (pos < s.size()) {
    // Skip redundant commas.
    if (s[pos] == ',') {
      ++pos;
      continue;
    }
    if ((qnt = base::VLQBase64Decode(s.c_str(), s.size(), &pos)) ==
        std::numeric_limits<int32_t>::min())
      return false;
    gen_col += qnt;
    if ((qnt = base::VLQBase64Decode(s.c_str(), s.size(), &pos)) ==
        std::numeric_limits<int32_t>::min())
      return false;
    file_idx += qnt;
    if ((qnt = base::VLQBase64Decode(s.c_str(), s.size(), &pos)) ==
        std::numeric_limits<int32_t>::min())
      return false;
    ori_line += qnt;
    // Column number in source file is always 0 in source map generated by
    // Emscripten. We just decode this value without further usage of it.
    if ((qnt = base::VLQBase64Decode(s.c_str(), s.size(), &pos)) ==
        std::numeric_limits<int32_t>::min())
      return false;

    if (pos < s.size() && s[pos] != ',') return false;
    pos++;

    file_idxs.push_back(file_idx);
    source_row.push_back(ori_line);
    offsets.push_back(gen_col);
  }
  return true;
}

size_t WasmModuleSourceMap::EstimateCurrentMemoryConsumption() const {
  UPDATE_WHEN_CLASS_CHANGES(WasmModuleSourceMap, 104);
  size_t result = sizeof(WasmModuleSourceMap);
  result += ContentSize(offsets);
  result += ContentSize(filenames);
  for (const std::string& s : filenames) {
    result += s.length();
  }
  result += ContentSize(file_idxs);
  result += ContentSize(source_row);
  return result;
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
