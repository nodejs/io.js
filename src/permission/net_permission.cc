#include "net_permission.h"
#include "debug_utils-inl.h"
#include "json_utils.h"

#include <cctype>
#include <charconv>
#include <string>
#include <string_view>
#include <vector>

namespace node {

namespace permission {

bool ConvertIPToBitset(std::bitset<IPV6_BIT_LEN>* bitset,
                       const std::string& str,
                       int ip_version) {
  int family = ip_version == 6 ? AF_INET6 : AF_INET;
  int char_len = ip_version == 6 ? IPV6_CHAR_LEN : IPV4_CHAR_LEN;
  int position = 0;
  char buf[IPV6_CHAR_LEN];
  // IP(like 127.0.0.1) in buf is [01111111][00000000][00000000][00000001]
  if (uv_inet_pton(family, str.c_str(), &buf) == 0) {
    // Handle char by char
    for (int i = char_len - 1; i >= 0; i--) {
      // Handle byte by byte
      for (int j = 0; j < 8; j++) {
        bitset->set(position++, !!(buf[i] & (1 << j)));
      }
    }
    return true;
  }
  return false;
}

void NetPermission::Apply(Environment* env,
                          const std::vector<std::string>& allow,
                          PermissionScope scope) {
  using std::string_view_literals::operator""sv;
  for (const std::string& res : allow) {
    // Allow multiple item in one flag, like --allow-net-udp=127.0.0.1,localhost
    const std::vector<std::string_view> addresses = SplitString(res, ","sv);
    for (const auto& address : addresses) {
      if (scope == PermissionScope::kNetUDP) {
        // Address is like *, *:*, host, host:*, host:port, *:port,
        // host/netmask:port
        if (address == "*"sv || address == "*:*"sv) {
          deny_all_udp_ = false;
          allow_all_udp_ = true;
          granted_udp_.clear();
          return;
        }
        GrantAccess(scope, address);
      }
    }
  }

  if (UNLIKELY(per_process::enabled_debug_list.enabled(
          DebugCategory::PERMISSION_MODEL))) {
    Print();
  }
}

void NetPermission::GrantAccess(PermissionScope scope,
                                const std::string_view param) {
  std::unique_ptr<PermissionAddressInfo> result = ParseAddress(param);
  std::bitset<IPV6_BIT_LEN> ip;
  std::bitset<IPV6_BIT_LEN> netmask;
  std::unique_ptr<NetMaskNode> net_mask_node;
  int ip_version = IsIP(std::string(result->address));

  if (ip_version > 0 && !result->netmask.empty()) {
    if (IsDigit(result->netmask)) {  // 127.0.0.1/24
      uint16_t netmask_len;
      std::from_chars(result->netmask.data(),
                      result->netmask.data() + result->netmask.size(),
                      netmask_len);
      CHECK_GE(netmask_len, 0);
      CHECK_LE(netmask_len, 0xFFFF);
      int position = ip_version == 6 ? IPV6_BIT_LEN : IPV4_BIT_LEN;
      while (netmask_len--) {
        netmask.set(--position, true);
      }
    } else {  // 127.0.0.1/255.255.255.0
      CHECK(ConvertIPToBitset(
          &netmask, std::string(result->netmask), ip_version));
    }
    CHECK(ConvertIPToBitset(&ip, std::string(result->address), ip_version));
    std::bitset<IPV6_BIT_LEN> network = netmask & ip;
    net_mask_node = std::make_unique<NetMaskNode>(netmask, network);
  }
  auto node = std::make_unique<NetNode>(
      result->address, result->port, std::move(net_mask_node));
  granted_udp_.push_back(std::move(node));
  deny_all_udp_ = false;
}

bool NetPermission::is_granted(Environment* env,
                               PermissionScope perm,
                               const std::string_view& param = "") const {
  switch (perm) {
    case PermissionScope::kNetUDP:
      return !deny_all_udp_ &&
             (allow_all_udp_ || CheckoutResource(granted_udp_, param));
    default:
      return false;
  }
}

bool NetPermission::CheckoutResource(
    const std::vector<std::unique_ptr<NetNode>>& list,
    const std::string_view param) const {
  if (param.empty()) {
    return false;
  }
  using std::string_view_literals::operator""sv;
  const std::vector<std::string_view> address_port = SplitString(param, "/"sv);
  int size = address_port.size();
  CHECK(size == 1 || size == 2);
  std::string address;
  std::string_view port;
  if (size == 2) {
    address = address_port[0];
    port = address_port[1];
  } else {
    address = address_port[0];
    port = "*"sv;
  }
  for (uint32_t i = 0; i < list.size(); i++) {
    auto node = list[i].get();
    int ip_version = IsIP(address);
    if (ip_version > 0 && node->net_mask_node) {
      std::bitset<IPV6_BIT_LEN> ip;
      if (!ConvertIPToBitset(&ip, address, ip_version)) {
        return false;
      }
      std::bitset<IPV6_BIT_LEN> network = node->net_mask_node->netmask & ip;
      if (node->net_mask_node->network == network &&
          (node->port == "*"sv || node->port == port)) {
        return true;
      }
    } else if ((node->address == "*"sv || node->address == address) &&
               (node->port == "*"sv || node->port == port)) {
      return true;
    }
  }
  return false;
}

void NetPermission::Print() const {
  std::cout << "net-udp-net: " << std::endl;
  std::cout << "  deny_all_udp_: " << deny_all_udp_ << std::endl;
  std::cout << "  allow_all_udp_: " << allow_all_udp_ << std::endl;
  JSONWriter writer(std::cout, false);
  writer.json_arraystart("Nodes");
  for (uint32_t i = 0; i < granted_udp_.size(); i++) {
    auto node = granted_udp_[i].get();
    writer.json_start();
    writer.json_keyvalue("address", node->address);
    writer.json_keyvalue("port", node->port);
    if (node->net_mask_node) {
      writer.json_keyvalue("netmask", node->net_mask_node->netmask.to_string());
      writer.json_keyvalue("network", node->net_mask_node->network.to_string());
    }
    writer.json_end();
  }
  writer.json_arrayend();
  std::cout << std::endl;
}

}  // namespace permission
}  // namespace node
