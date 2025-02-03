#include "crdtp/json.h"
#include "gtest/gtest.h"
#include "inspector/node_json.h"
#include "node/inspector/protocol/Protocol.h"

namespace node {
namespace inspector {
namespace protocol {
namespace {

TEST(InspectorProtocol, Utf8StringSerDes) {
  constexpr const char* kKey = "unicode_key";
  constexpr const char* kValue = "CJK 汉字 🍱 🧑‍🧑‍🧒‍🧒";
  std::unique_ptr<DictionaryValue> val = DictionaryValue::create();
  val->setString(kKey, kValue);

  std::vector<uint8_t> cbor = val->Serialize();
  std::string json;
  crdtp::Status status =
      crdtp::json::ConvertCBORToJSON(crdtp::SpanFrom(cbor), &json);
  CHECK(status.ok());

  std::unique_ptr<DictionaryValue> parsed =
      DictionaryValue::cast(JsonUtil::parseJSON(json));
  std::string parsed_value;
  CHECK(parsed->getString(kKey, &parsed_value));
  CHECK_EQ(parsed_value, std::string(kValue));
}

}  // namespace
}  // namespace protocol
}  // namespace inspector
}  // namespace node
