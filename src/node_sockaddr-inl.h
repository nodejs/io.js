#ifndef SRC_NODE_SOCKADDR_INL_H_
#define SRC_NODE_SOCKADDR_INL_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "node.h"
#include "env-inl.h"
#include "node_internals.h"
#include "node_sockaddr.h"
#include "util-inl.h"
#include "memory_tracker-inl.h"

#include <string>

namespace node {

static constexpr uint32_t kLabelMask = 0xFFFFF;

inline void hash_combine(size_t* seed) { }

template <typename T, typename... Args>
inline void hash_combine(size_t* seed, const T& value, Args... rest) {
    *seed ^= std::hash<T>{}(value) + 0x9e3779b9 + (*seed << 6) + (*seed >> 2);
    hash_combine(seed, rest...);
}

bool SocketAddress::is_numeric_host(const char* hostname) {
  return is_numeric_host(hostname, AF_INET) ||
         is_numeric_host(hostname, AF_INET6);
}

bool SocketAddress::is_numeric_host(const char* hostname, int family) {
  in6_addr dst;
  return inet_pton(family, hostname, &dst) == 1;
}

int SocketAddress::GetPort(const sockaddr* addr) {
  CHECK(addr->sa_family == AF_INET || addr->sa_family == AF_INET6);
  return ntohs(addr->sa_family == AF_INET ?
      reinterpret_cast<const sockaddr_in*>(addr)->sin_port :
      reinterpret_cast<const sockaddr_in6*>(addr)->sin6_port);
}

int SocketAddress::GetPort(const sockaddr_storage* addr) {
  return GetPort(reinterpret_cast<const sockaddr*>(addr));
}

std::string SocketAddress::GetAddress(const sockaddr* addr) {
  CHECK(addr->sa_family == AF_INET || addr->sa_family == AF_INET6);
  char host[INET6_ADDRSTRLEN];
  const void* src = addr->sa_family == AF_INET ?
      static_cast<const void*>(
          &(reinterpret_cast<const sockaddr_in*>(addr)->sin_addr)) :
      static_cast<const void*>(
          &(reinterpret_cast<const sockaddr_in6*>(addr)->sin6_addr));
  uv_inet_ntop(addr->sa_family, src, host, INET6_ADDRSTRLEN);
  return std::string(host);
}

std::string SocketAddress::GetAddress(const sockaddr_storage* addr) {
  return GetAddress(reinterpret_cast<const sockaddr*>(addr));
}

size_t SocketAddress::GetLength(const sockaddr* addr) {
  return addr->sa_family == AF_INET ?
      sizeof(sockaddr_in) : sizeof(sockaddr_in6);
}

size_t SocketAddress::GetLength(const sockaddr_storage* addr) {
  return GetLength(reinterpret_cast<const sockaddr*>(addr));
}

SocketAddress::SocketAddress(const sockaddr* addr) {
  memcpy(&address_, addr, GetLength(addr));
}

SocketAddress::SocketAddress(const SocketAddress& addr) {
  memcpy(&address_, &addr.address_, addr.length());
}

SocketAddress& SocketAddress::operator=(const sockaddr* addr) {
  memcpy(&address_, addr, GetLength(addr));
  return *this;
}

SocketAddress& SocketAddress::operator=(const SocketAddress& addr) {
  memcpy(&address_, &addr.address_, addr.length());
  return *this;
}

const sockaddr& SocketAddress::operator*() const {
  return *data();
}

const sockaddr* SocketAddress::operator->() const {
  return data();
}

size_t SocketAddress::length() const {
  return GetLength(&address_);
}

const sockaddr* SocketAddress::data() const {
  return reinterpret_cast<const sockaddr*>(&address_);
}

const uint8_t* SocketAddress::raw() const {
  return reinterpret_cast<const uint8_t*>(&address_);
}

sockaddr* SocketAddress::storage() {
  return reinterpret_cast<sockaddr*>(&address_);
}

int SocketAddress::family() const {
  return address_.ss_family;
}

std::string SocketAddress::address() const {
  return GetAddress(&address_);
}

int SocketAddress::port() const {
  return GetPort(&address_);
}

uint32_t SocketAddress::flow_label() const {
  if (family() != AF_INET6)
    return 0;
  const sockaddr_in6* in = reinterpret_cast<const sockaddr_in6*>(data());
  return in->sin6_flowinfo;
}

void SocketAddress::set_flow_label(uint32_t label) {
  if (family() != AF_INET6)
    return;
  CHECK_LE(label, kLabelMask);
  sockaddr_in6* in = reinterpret_cast<sockaddr_in6*>(&address_);
  in->sin6_flowinfo = label;
}

std::string SocketAddress::ToString() const {
  if (family() != AF_INET && family() != AF_INET6) return "";
  return (family() == AF_INET6 ?
              std::string("[") + address() + "]:" :
              address() + ":") +
      std::to_string(port());
}

void SocketAddress::Update(uint8_t* data, size_t len) {
  CHECK_LE(len, sizeof(address_));
  memcpy(&address_, data, len);
}

void SocketAddress::Update(const sockaddr* data, size_t len) {
  CHECK_LE(len, sizeof(address_));
  memcpy(&address_, data, len);
}

v8::MaybeLocal<v8::Object> SocketAddress::ToJS(
    Environment* env,
    v8::Local<v8::Object> info) const {
  return AddressToJS(env, data(), info);
}

bool SocketAddress::operator==(const SocketAddress& other) const {
  if (family() != other.family()) return false;
  return memcmp(raw(), other.raw(), length()) == 0;
}

bool SocketAddress::operator!=(const SocketAddress& other) const {
  return !(*this == other);
}

std::partial_ordering SocketAddress::operator<=>(
    const SocketAddress& other) const {
  return compare(other);
}

template <typename T>
SocketAddressLRU<T>::SocketAddressLRU(
    size_t max_size)
    : max_size_(max_size) {}

template <typename T>
typename T::Type* SocketAddressLRU<T>::Peek(
    const SocketAddress& address) const {
  auto it = map_.find(address);
  return it == std::end(map_) ? nullptr : &it->second->second;
}

template <typename T>
void SocketAddressLRU<T>::CheckExpired() {
  auto it = list_.rbegin();
  while (it != list_.rend()) {
    if (T::CheckExpired(it->first, it->second)) {
      map_.erase(it->first);
      list_.pop_back();
      it = list_.rbegin();
      continue;
    } else {
      break;
    }
  }
}

template <typename T>
void SocketAddressLRU<T>::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackFieldWithSize("list", size() * sizeof(Pair));
}

// If an item already exists for the given address, bump up it's
// position in the LRU list and return it. If the item does not
// exist, create it. If an item is created, check the size of the
// cache and adjust if necessary. Whether the item exists or not,
// purge expired items.
template <typename T>
typename T::Type* SocketAddressLRU<T>::Upsert(
    const SocketAddress& address) {

  auto on_exit = OnScopeLeave([&]() { CheckExpired(); });

  auto it = map_.find(address);
  if (it != std::end(map_)) {
    list_.splice(list_.begin(), list_, it->second);
    T::Touch(it->first, &it->second->second);
    return &it->second->second;
  }

  list_.push_front(Pair(address, { }));
  map_[address] = list_.begin();
  T::Touch(list_.begin()->first, &list_.begin()->second);

  // Drop the last item in the list if we are
  // over the size limit...
  if (map_.size() > max_size_) {
    auto last = list_.end();
    map_.erase((--last)->first);
    list_.pop_back();
  }

  return &map_[address]->second;
}

v8::MaybeLocal<v8::Value> SocketAddressBlockList::Rule::ToV8String(
    Environment* env) {
  std::string str = ToString();
  return ToV8Value(env->context(), str);
}
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_NODE_SOCKADDR_INL_H_
