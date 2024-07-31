
#ifndef SRC_NODE_SNAPSHOT_BUILDER_H_
#define SRC_NODE_SNAPSHOT_BUILDER_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <cstdint>
#include <optional>
#include <string_view>
#include "node_exit_code.h"
#include "node_mutex.h"
#include "v8.h"

namespace node {

class ExternalReferenceRegistry;
struct SnapshotData;

std::optional<SnapshotConfig> ReadSnapshotConfig(const char* path);

class NODE_EXTERN_PRIVATE SnapshotBuilder {
 public:
  static ExitCode GenerateAsSource(const char* out_path,
                                   const std::vector<std::string>& args,
                                   const std::vector<std::string>& exec_args,
                                   const SnapshotConfig& config,
                                   bool use_array_literals = false);

  // Generate the snapshot into out. builder_script_content should match
  // config.builder_script_path. This is passed separately
  // in case the script is already read for other purposes.
  static ExitCode Generate(
      SnapshotData* out,
      const std::vector<std::string>& args,
      const std::vector<std::string>& exec_args,
      std::optional<std::string_view> builder_script_content,
      const SnapshotConfig& config);

  // If nullptr is returned, the binary is not built with embedded
  // snapshot.
  static const SnapshotData* GetEmbeddedSnapshotData();
  static void InitializeIsolateParams(const SnapshotData* data,
                                      v8::Isolate::CreateParams* params);

  static const std::vector<intptr_t>& CollectExternalReferences();

  static ExitCode CreateSnapshot(SnapshotData* out,
                                 CommonEnvironmentSetup* setup);

 private:
  static std::unique_ptr<ExternalReferenceRegistry> registry_;
};
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_SNAPSHOT_BUILDER_H_
