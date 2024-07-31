#include "permission/worker_permission.h"

#include <string>
#include <vector>

namespace node {

namespace permission {

// Currently, PolicyDenyWorker manage a single state
// Once denied, it's always denied
void WorkerPermission::Apply(Environment* env,
                             const std::vector<std::string>& allow,
                             PermissionScope scope) {
  deny_all_ = true;
}

bool WorkerPermission::is_granted(Environment* env,
                                  PermissionScope perm,
                                  const std::string_view& param) const {
  return deny_all_ == false;
}

}  // namespace permission
}  // namespace node
