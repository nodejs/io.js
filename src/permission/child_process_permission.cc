#include "child_process_permission.h"

#include <string>
#include <vector>

namespace node {

namespace permission {

// Currently, ChildProcess manage a single state
// Once denied, it's always denied
void ChildProcessPermission::Apply(Environment* env,
                                   const std::vector<std::string>& allow,
                                   PermissionScope scope) {
  deny_all_ = true;
}

bool ChildProcessPermission::is_granted(Environment* env,
                                        PermissionScope perm,
                                        const std::string_view& param) const {
  return deny_all_ == false;
}

}  // namespace permission
}  // namespace node
