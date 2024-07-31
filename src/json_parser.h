#ifndef SRC_JSON_PARSER_H_
#define SRC_JSON_PARSER_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include "util.h"
#include "v8.h"

namespace node {
// This is intended to be used to get some top-level fields out of a JSON
// without having to spin up a full Node.js environment that unnecessarily
// complicates things.
class JSONParser {
 public:
  using StringDict = std::unordered_map<std::string, std::string>;
  JSONParser();
  ~JSONParser() = default;
  bool Parse(const std::string& content);
  std::optional<std::string> GetTopLevelStringField(std::string_view field);
  std::optional<bool> GetTopLevelBoolField(std::string_view field);
  std::optional<StringDict> GetTopLevelStringDict(std::string_view field);

 private:
  // We might want a lighter-weight JSON parser for this use case. But for now
  // using V8 is good enough.
  RAIIIsolateWithoutEntering isolate_;

  v8::Global<v8::Context> context_;
  v8::Global<v8::Object> content_;
  bool parsed_ = false;
};
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_JSON_PARSER_H_
