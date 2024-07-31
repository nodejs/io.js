#ifndef SRC_NODE_SEA_H_
#define SRC_NODE_SEA_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <cinttypes>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "node_exit_code.h"

namespace node {
class Environment;
namespace sea {
// A special number that will appear at the beginning of the single executable
// preparation blobs ready to be injected into the binary. We use this to check
// that the data given to us are intended for building single executable
// applications.
const uint32_t kMagic = 0x143da20;

enum class SeaFlags : uint32_t {
  kDefault = 0,
  kDisableExperimentalSeaWarning = 1 << 0,
  kUseSnapshot = 1 << 1,
  kUseCodeCache = 1 << 2,
  kIncludeAssets = 1 << 3,
};

struct SeaResource {
  SeaFlags flags = SeaFlags::kDefault;
  std::string_view code_path;
  std::string_view main_code_or_snapshot;
  std::optional<std::string_view> code_cache;
  std::unordered_map<std::string_view, std::string_view> assets;

  bool use_snapshot() const;
  bool use_code_cache() const;

  static constexpr size_t kHeaderSize = sizeof(kMagic) + sizeof(SeaFlags);
};

bool IsSingleExecutable();
SeaResource FindSingleExecutableResource();
std::tuple<int, char**> FixupArgsForSEA(int argc, char** argv);
node::ExitCode BuildSingleExecutableBlob(
    const std::string& config_path,
    const std::vector<std::string>& args,
    const std::vector<std::string>& exec_args);

// Try loading the Environment as a single-executable application.
// Returns true if it is loaded as a single-executable application.
// Otherwise returns false and the caller is expected to call LoadEnvironment()
// differently.
bool MaybeLoadSingleExecutableApplication(Environment* env);
}  // namespace sea
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_SEA_H_
