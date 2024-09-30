#ifndef SRC_PERMISSION_NET_PERMISSION_H_
#define SRC_PERMISSION_NET_PERMISSION_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <bitset>
#include "permission/permission_base.h"
#include "util.h"
#include "uv.h"

namespace node {

namespace permission {

class NetPermission final : public PermissionBase {
 public:
  void Apply(Environment* env,
             const std::vector<std::string>& allow,
             PermissionScope scope) override;
  bool is_granted(Environment* env,
                  PermissionScope perm,
                  const std::string_view& param) const override;

  struct NetMaskNode {
    std::bitset<IPV6_BIT_LEN> netmask;
    std::bitset<IPV6_BIT_LEN> network;
    explicit NetMaskNode(const std::bitset<IPV6_BIT_LEN>& netmask_,
                         const std::bitset<IPV6_BIT_LEN>& network_)
        : netmask(netmask_), network(network_) {}
  };

  struct NetNode {
    std::string_view address;
    std::string_view port;
    std::unique_ptr<NetMaskNode> net_mask_node;
    explicit NetNode(const std::string_view address_,
                     const std::string_view port_,
                     std::unique_ptr<NetMaskNode> net_mask_node_)
        : address(address_),
          port(port_),
          net_mask_node(std::move(net_mask_node_)) {}
  };

 private:
  void GrantAccess(PermissionScope scope, const std::string_view param);
  bool CheckoutResource(const std::vector<std::unique_ptr<NetNode>>& list,
                        const std::string_view param = "") const;
  void Print() const;

  std::vector<std::unique_ptr<NetNode>> granted_udp_;
  bool deny_all_udp_ = true;
  bool allow_all_udp_ = false;
};

}  // namespace permission

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_PERMISSION_NET_PERMISSION_H_
