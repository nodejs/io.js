#ifndef SRC_NODE_CONFIG_FILE_H_
#define SRC_NODE_CONFIG_FILE_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <map>
#include <string>
#include <variant>
#include "simdjson.h"
#include "util-inl.h"

namespace node {

// When trying to parse the configuration file, we can have three possible
// results:
//  - Valid: The file was successfully parsed and the content is valid.
//  - FileError: There was an error reading the file.
//  - InvalidContent: The file was read, but the content is invalid.
enum ParseResult { Valid, FileError, InvalidContent };

// ConfigReader is the class that parses the configuration JSON file.
// It reads the file provided by --experimental-config-file and
// extracts the flags.
class ConfigReader {
 public:
  ParseResult ParseConfig(const std::string_view& config_path);

  std::optional<std::string_view> GetDataFromArgs(
      const std::vector<std::string>& args);

  std::string AssignNodeOptions();

  size_t GetFlagsSize();

 private:
  std::vector<std::string> flags_;
};

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_CONFIG_FILE_H_
