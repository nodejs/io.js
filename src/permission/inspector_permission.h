#ifndef SRC_PERMISSION_INSPECTOR_PERMISSION_H_
#define SRC_PERMISSION_INSPECTOR_PERMISSION_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <string>
#include "permission/permission_base.h"

namespace node {

namespace permission {

class InspectorPermission final : public PermissionBase {
 public:
  void Apply(Environment* env,
             const std::vector<std::string>& allow,
             PermissionScope scope) override;
  bool is_granted(Environment* env,
                  PermissionScope perm,
                  const std::string_view& param = "") const override;

 private:
  bool deny_all_;
};

}  // namespace permission

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_PERMISSION_INSPECTOR_PERMISSION_H_
