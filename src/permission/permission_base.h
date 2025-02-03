#ifndef SRC_PERMISSION_PERMISSION_BASE_H_
#define SRC_PERMISSION_PERMISSION_BASE_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <map>
#include <string>
#include <string_view>
#include "v8.h"

namespace node {

class Environment;

namespace permission {

#define FILESYSTEM_PERMISSIONS(V)                                              \
  V(FileSystem, "fs", PermissionsRoot)                                         \
  V(FileSystemRead, "fs.read", FileSystem)                                     \
  V(FileSystemWrite, "fs.write", FileSystem)

#define CHILD_PROCESS_PERMISSIONS(V) V(ChildProcess, "child", PermissionsRoot)

#define WASI_PERMISSIONS(V) V(WASI, "wasi", PermissionsRoot)

#define WORKER_THREADS_PERMISSIONS(V)                                          \
  V(WorkerThreads, "worker", PermissionsRoot)

#define INSPECTOR_PERMISSIONS(V) V(Inspector, "inspector", PermissionsRoot)

#define PERMISSIONS(V)                                                         \
  FILESYSTEM_PERMISSIONS(V)                                                    \
  CHILD_PROCESS_PERMISSIONS(V)                                                 \
  WASI_PERMISSIONS(V)                                                          \
  WORKER_THREADS_PERMISSIONS(V)                                                \
  INSPECTOR_PERMISSIONS(V)

#define V(name, _, __) k##name,
enum class PermissionScope {
  kPermissionsRoot = -1,
  PERMISSIONS(V) kPermissionsCount
};
#undef V

class PermissionBase {
 public:
  virtual void Apply(Environment* env,
                     const std::vector<std::string>& allow,
                     PermissionScope scope) = 0;
  virtual bool is_granted(Environment* env,
                          PermissionScope perm,
                          const std::string_view& param = "") const = 0;
};

}  // namespace permission

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_PERMISSION_PERMISSION_BASE_H_
