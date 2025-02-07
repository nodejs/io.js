#ifndef SRC_INSPECTOR_NODE_JSON_H_
#define SRC_INSPECTOR_NODE_JSON_H_

#include "node/inspector/protocol/Protocol.h"

namespace node {
namespace inspector {

struct JsonUtil {
  // Parse s JSON string into protocol::Value.
  static std::unique_ptr<protocol::Value> ParseJSON(const uint8_t* chars,
                                                    size_t size);
  static std::unique_ptr<protocol::Value> ParseJSON(const uint16_t* chars,
                                                    size_t size);

  static std::unique_ptr<protocol::Value> parseJSON(const std::string_view);
  static std::unique_ptr<protocol::Value> parseJSON(
      v8_inspector::StringView view);
};

}  // namespace inspector
}  // namespace node

#endif  // SRC_INSPECTOR_NODE_JSON_H_
