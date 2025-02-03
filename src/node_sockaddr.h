#ifndef SRC_NODE_SOCKADDR_H_
#define SRC_NODE_SOCKADDR_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "env.h"
#include "memory_tracker.h"
#include "base_object.h"
#include "node.h"
#include "node_worker.h"
#include "uv.h"
#include "v8.h"

#include <compare>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>

namespace node {

class Environment;

class SocketAddress : public MemoryRetainer {
 public:
  struct Hash {
    size_t operator()(const SocketAddress& addr) const;
  };

  inline bool operator==(const SocketAddress& other) const;
  inline bool operator!=(const SocketAddress& other) const;

  inline std::partial_ordering operator<=>(const SocketAddress& other) const;

  inline static bool is_numeric_host(const char* hostname);
  inline static bool is_numeric_host(const char* hostname, int family);

  // Returns true if converting {family, host, port} to *addr succeeded.
  static bool ToSockAddr(
      int32_t family,
      const char* host,
      uint32_t port,
      sockaddr_storage* addr);

  // Returns true if converting {family, host, port} to *addr succeeded.
  static bool New(
      int32_t family,
      const char* host,
      uint32_t port,
      SocketAddress* addr);

  static bool New(
      const char* host,
      uint32_t port,
      SocketAddress* addr);

  // Returns the port for an IPv4 or IPv6 address.
  inline static int GetPort(const sockaddr* addr);
  inline static int GetPort(const sockaddr_storage* addr);

  // Returns the numeric host as a string for an IPv4 or IPv6 address.
  inline static std::string GetAddress(const sockaddr* addr);
  inline static std::string GetAddress(const sockaddr_storage* addr);

  // Returns the struct length for an IPv4, IPv6 or UNIX domain.
  inline static size_t GetLength(const sockaddr* addr);
  inline static size_t GetLength(const sockaddr_storage* addr);

  SocketAddress() = default;

  inline explicit SocketAddress(const sockaddr* addr);
  inline SocketAddress(const SocketAddress& addr);
  inline SocketAddress& operator=(const sockaddr* other);
  inline SocketAddress& operator=(const SocketAddress& other);

  inline const sockaddr& operator*() const;
  inline const sockaddr* operator->() const;

  inline const sockaddr* data() const;
  inline const uint8_t* raw() const;
  inline sockaddr* storage();
  inline size_t length() const;

  inline int family() const;
  inline std::string address() const;
  inline int port() const;

  // Returns true if the given other SocketAddress is a match
  // for this one. The addresses are a match if:
  // 1. They are the same family and match identically
  // 2. They are different family but match semantically (
  //     for instance, an IPv4 address in IPv6 notation)
  bool is_match(const SocketAddress& other) const;

  // Compares this SocketAddress to the given other SocketAddress.
  std::partial_ordering compare(const SocketAddress& other) const;

  // Returns true if this SocketAddress is within the subnet
  // identified by the given network address and CIDR prefix.
  bool is_in_network(const SocketAddress& network, int prefix) const;

  // If the SocketAddress is an IPv6 address, returns the
  // current value of the IPv6 flow label, if set. Otherwise
  // returns 0.
  inline uint32_t flow_label() const;

  // If the SocketAddress is an IPv6 address, sets the
  // current value of the IPv6 flow label. If not an
  // IPv6 address, set_flow_label is a non-op. It
  // is important to note that the flow label,
  // while represented as an uint32_t, the flow
  // label is strictly limited to 20 bits, and
  // this will assert if any value larger than
  // 20-bits is specified.
  inline void set_flow_label(uint32_t label = 0);

  inline void Update(uint8_t* data, size_t len);
  inline void Update(const sockaddr* data, size_t len);

  static SocketAddress FromSockName(const uv_udp_t& handle);
  static SocketAddress FromSockName(const uv_tcp_t& handle);
  static SocketAddress FromPeerName(const uv_udp_t& handle);
  static SocketAddress FromPeerName(const uv_tcp_t& handle);

  inline v8::MaybeLocal<v8::Object> ToJS(
      Environment* env,
      v8::Local<v8::Object> obj = v8::Local<v8::Object>()) const;

  inline std::string ToString() const;

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(SocketAddress)
  SET_SELF_SIZE(SocketAddress)

  template <typename T>
  using Map = std::unordered_map<SocketAddress, T, Hash>;

 private:
  sockaddr_storage address_;
};

class SocketAddressBase : public BaseObject {
 public:
  static bool HasInstance(Environment* env, v8::Local<v8::Value> value);
  static v8::Local<v8::FunctionTemplate> GetConstructorTemplate(
      Environment* env);
  static void Initialize(Environment* env, v8::Local<v8::Object> target);
  static BaseObjectPtr<SocketAddressBase> Create(
      Environment* env,
      std::shared_ptr<SocketAddress> address);

  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Detail(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void LegacyDetail(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetFlowLabel(const v8::FunctionCallbackInfo<v8::Value>& args);

  SocketAddressBase(
    Environment* env,
    v8::Local<v8::Object> wrap,
    std::shared_ptr<SocketAddress> address);

  inline const std::shared_ptr<SocketAddress>& address() const {
    return address_;
  }

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(SocketAddressBase)
  SET_SELF_SIZE(SocketAddressBase)

  BaseObject::TransferMode GetTransferMode() const override {
    return TransferMode::kCloneable;
  }
  std::unique_ptr<worker::TransferData> CloneForMessaging() const override;

  class TransferData : public worker::TransferData {
   public:
    inline explicit TransferData(const SocketAddressBase* wrap)
        : address_(wrap->address_) {}

    inline explicit TransferData(std::shared_ptr<SocketAddress> address)
        : address_(std::move(address)) {}

    BaseObjectPtr<BaseObject> Deserialize(
        Environment* env,
        v8::Local<v8::Context> context,
        std::unique_ptr<worker::TransferData> self) override;

    void MemoryInfo(MemoryTracker* tracker) const override;
    SET_MEMORY_INFO_NAME(SocketAddressBase::TransferData)
    SET_SELF_SIZE(TransferData)

   private:
    std::shared_ptr<SocketAddress> address_;
  };

 private:
  std::shared_ptr<SocketAddress> address_;
};

template <typename T>
class SocketAddressLRU : public MemoryRetainer {
 public:
  using Type = typename T::Type;

  inline explicit SocketAddressLRU(size_t max_size);

  // If the item already exists, returns a reference to
  // the existing item, adjusting items position in the
  // LRU. If the item does not exist, emplaces the item
  // and returns the new item.
  Type* Upsert(const SocketAddress& address);

  // Returns a reference to the item if it exists, or
  // nullptr. The position in the LRU is not modified.
  Type* Peek(const SocketAddress& address) const;

  size_t size() const { return map_.size(); }
  size_t max_size() const { return max_size_; }

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(SocketAddressLRU)
  SET_SELF_SIZE(SocketAddressLRU)

 private:
  using Pair = std::pair<SocketAddress, Type>;
  using Iterator = typename std::list<Pair>::iterator;

  void CheckExpired();

  std::list<Pair> list_;
  SocketAddress::Map<Iterator> map_;
  size_t max_size_;
};

// A BlockList is used to evaluate whether a given
// SocketAddress should be accepted for inbound or
// outbound network activity.
class SocketAddressBlockList : public MemoryRetainer {
 public:
  explicit SocketAddressBlockList(
      std::shared_ptr<SocketAddressBlockList> parent = {});
  ~SocketAddressBlockList() = default;

  void AddSocketAddress(const std::shared_ptr<SocketAddress>& address);

  void RemoveSocketAddress(const std::shared_ptr<SocketAddress>& address);

  void AddSocketAddressRange(
      const std::shared_ptr<SocketAddress>& start,
      const std::shared_ptr<SocketAddress>& end);

  void AddSocketAddressMask(
      const std::shared_ptr<SocketAddress>& address,
      int prefix);

  bool Apply(const std::shared_ptr<SocketAddress>& address);

  size_t size() const { return rules_.size(); }

  v8::MaybeLocal<v8::Array> ListRules(Environment* env);

  struct Rule : public MemoryRetainer {
    virtual bool Apply(const std::shared_ptr<SocketAddress>& address) = 0;
    inline v8::MaybeLocal<v8::Value> ToV8String(Environment* env);
    virtual std::string ToString() = 0;
  };

  struct SocketAddressRule final : Rule {
    std::shared_ptr<SocketAddress> address;

    explicit SocketAddressRule(const std::shared_ptr<SocketAddress>& address);

    bool Apply(const std::shared_ptr<SocketAddress>& address) override;
    std::string ToString() override;

    void MemoryInfo(node::MemoryTracker* tracker) const override;
    SET_MEMORY_INFO_NAME(SocketAddressRule)
    SET_SELF_SIZE(SocketAddressRule)
  };

  struct SocketAddressRangeRule final : Rule {
    std::shared_ptr<SocketAddress> start;
    std::shared_ptr<SocketAddress> end;

    SocketAddressRangeRule(
        const std::shared_ptr<SocketAddress>& start,
        const std::shared_ptr<SocketAddress>& end);

    bool Apply(const std::shared_ptr<SocketAddress>& address) override;
    std::string ToString() override;

    void MemoryInfo(node::MemoryTracker* tracker) const override;
    SET_MEMORY_INFO_NAME(SocketAddressRangeRule)
    SET_SELF_SIZE(SocketAddressRangeRule)
  };

  struct SocketAddressMaskRule final : Rule {
    std::shared_ptr<SocketAddress> network;
    int prefix;

    SocketAddressMaskRule(
        const std::shared_ptr<SocketAddress>& address,
        int prefix);

    bool Apply(const std::shared_ptr<SocketAddress>& address) override;
    std::string ToString() override;

    void MemoryInfo(node::MemoryTracker* tracker) const override;
    SET_MEMORY_INFO_NAME(SocketAddressMaskRule)
    SET_SELF_SIZE(SocketAddressMaskRule)
  };

  void MemoryInfo(node::MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(SocketAddressBlockList)
  SET_SELF_SIZE(SocketAddressBlockList)

 private:
  bool ListRules(Environment* env, v8::LocalVector<v8::Value>* vec);

  std::shared_ptr<SocketAddressBlockList> parent_;
  std::list<std::unique_ptr<Rule>> rules_;
  SocketAddress::Map<std::list<std::unique_ptr<Rule>>::iterator> address_rules_;

  Mutex mutex_;
};

class SocketAddressBlockListWrap : public BaseObject {
 public:
  static bool HasInstance(Environment* env, v8::Local<v8::Value> value);
  static v8::Local<v8::FunctionTemplate> GetConstructorTemplate(
      Environment* env);
  static void Initialize(v8::Local<v8::Object> target,
                         v8::Local<v8::Value> unused,
                         v8::Local<v8::Context> context,
                         void* priv);

  static BaseObjectPtr<SocketAddressBlockListWrap> New(Environment* env);
  static BaseObjectPtr<SocketAddressBlockListWrap> New(
      Environment* env,
      std::shared_ptr<SocketAddressBlockList> blocklist);

  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void AddAddress(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void AddRange(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void AddSubnet(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Check(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetRules(const v8::FunctionCallbackInfo<v8::Value>& args);

  SocketAddressBlockListWrap(
      Environment* env,
      v8::Local<v8::Object> wrap,
      std::shared_ptr<SocketAddressBlockList> blocklist =
          std::make_shared<SocketAddressBlockList>());

  void MemoryInfo(node::MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(SocketAddressBlockListWrap)
  SET_SELF_SIZE(SocketAddressBlockListWrap)

  BaseObject::TransferMode GetTransferMode() const override {
    return TransferMode::kCloneable;
  }
  std::unique_ptr<worker::TransferData> CloneForMessaging() const override;

  class TransferData : public worker::TransferData {
   public:
    inline explicit TransferData(const SocketAddressBlockListWrap* wrap)
        : blocklist_(wrap->blocklist_) {}

    inline explicit TransferData(
        std::shared_ptr<SocketAddressBlockList> blocklist)
        : blocklist_(std::move(blocklist)) {}

    BaseObjectPtr<BaseObject> Deserialize(
        Environment* env,
        v8::Local<v8::Context> context,
        std::unique_ptr<worker::TransferData> self) override;

    void MemoryInfo(MemoryTracker* tracker) const override;
    SET_MEMORY_INFO_NAME(SocketAddressBlockListWrap::TransferData)
    SET_SELF_SIZE(TransferData)

   private:
    std::shared_ptr<SocketAddressBlockList> blocklist_;
  };

 private:
  std::shared_ptr<SocketAddressBlockList> blocklist_;
};

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_SOCKADDR_H_
