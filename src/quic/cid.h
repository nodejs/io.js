#pragma once

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#include <memory_tracker.h>
#include <ngtcp2/ngtcp2.h>
#include <string>
#include "defs.h"

namespace node::quic {

// CIDS are used to identify endpoints participating in a QUIC session.
// Once created, CID instances are immutable.
//
// CIDs contain between 1 to 20 bytes. Most typically they are selected
// randomly but there is a spec for creating "routable" CIDs that encode
// a specific structure that is meaningful only to the side that creates
// the CID. For most purposes, CIDs should be treated as opaque tokens.
//
// Each peer in a QUIC session generates one or more CIDs that the *other*
// peer will use to identify the session. When a QUIC client initiates a
// brand new session, it will initially generates a CID of its own (its
// source CID) and a random placeholder CID for the server (the original
// destination CID). When the server receives the initial packet, it will
// generate its own source CID and use the clients source CID as the
// server's destination CID.
//
//       Client                 Server
// -------------------------------------------
//     Source CID   <====> Destination CID
//  Destination CID <====>   Source CID
//
// While the connection is being established, it is possible for either
// peer to generate additional CIDs that are also associated with the
// connection.
class CID final : public MemoryRetainer {
 public:
  static constexpr size_t kMinLength = NGTCP2_MIN_CIDLEN;
  static constexpr size_t kMaxLength = NGTCP2_MAX_CIDLEN;

  // Copy the given ngtcp2_cid.
  explicit CID(const ngtcp2_cid& cid);

  // Copy the given buffer as a CID. The len must be within
  // kMinLength and kMaxLength.
  explicit CID(const uint8_t* data, size_t len);

  // Wrap the given ngtcp2_cid. The CID does not take ownership
  // of the underlying ngtcp2_cid.
  explicit CID(const ngtcp2_cid* cid);

  CID(const CID& other);
  CID& operator=(const CID& other);
  CID(CID&&) = delete;

  struct Hash final {
    size_t operator()(const CID& cid) const;
  };

  bool operator==(const CID& other) const noexcept;
  bool operator!=(const CID& other) const noexcept;

  operator const uint8_t*() const;
  operator const ngtcp2_cid&() const;
  operator const ngtcp2_cid*() const;

  // True if the CID length is at least kMinLength;
  operator bool() const;
  size_t length() const;

  std::string ToString() const;

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(CID)
  SET_SELF_SIZE(CID)

  template <typename T>
  using Map = std::unordered_map<CID, T, CID::Hash>;

  // A CID::Factory, as the name suggests, is used to create new CIDs.
  // Per https://datatracker.ietf.org/doc/draft-ietf-quic-load-balancers/, QUIC
  // implementations MAY use the Connection ID associated with a QUIC session
  // as a routing mechanism, with each CID instance securely encoding the
  // routing information. By default, our implementation creates CIDs randomly
  // but will allow user code to provide their own CID::Factory implementation.
  class Factory;

  static CID kInvalid;

  // The default constructor creates an empty, zero-length CID.
  // Zero-length CIDs are not usable. We use them as a placeholder
  // for a missing or empty CID value. This is public only because
  // it is required for the CID::Map implementation. It should not
  // be used. Use kInvalid instead.
  CID();

 private:
  ngtcp2_cid cid_;
  const ngtcp2_cid* ptr_;

  friend struct Hash;
};

class CID::Factory {
 public:
  virtual ~Factory() = default;

  // Generate a new CID. The length_hint must be between CID::kMinLength
  // and CID::kMaxLength. The implementation can choose to ignore the length.
  virtual CID Generate(size_t length_hint = CID::kMaxLength) const = 0;

  // Generate a new CID into the given ngtcp2_cid. This variation of
  // Generate should be used far less commonly.
  virtual CID GenerateInto(ngtcp2_cid* cid,
                           size_t length_hint = CID::kMaxLength) const = 0;

  // The default random CID generator instance.
  static const Factory& random();

  // TODO(@jasnell): This will soon also include additional implementations
  // of CID::Factory that implement the QUIC Load Balancers spec.
};

}  // namespace node::quic

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
