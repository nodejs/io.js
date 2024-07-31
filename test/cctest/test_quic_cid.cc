#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#include <env-inl.h>
#include <gtest/gtest.h>
#include <ngtcp2/ngtcp2.h>
#include <quic/cid.h>
#include <util-inl.h>
#include <string>
#include <unordered_map>

using node::quic::CID;

TEST(CID, Basic) {
  auto& random = CID::Factory::random();
  {
    auto cid = random.Generate();
    CHECK_EQ(cid.length(), CID::kMaxLength);
    CHECK(cid);
    CHECK_EQ(cid, cid);
  }
  {
    auto cid = random.Generate(5);
    CHECK_EQ(cid.length(), 5);
    CHECK(cid);
  }
  {
    auto cid1 = random.Generate();
    auto cid2 = random.Generate();
    CHECK_NE(cid1, cid2);
  }
  {
    auto cid1 = random.Generate(5);
    auto cid2 = random.Generate();
    CHECK_NE(cid1, cid2);
  }
  {
    auto cid1 = random.Generate();
    auto cid2 = random.Generate(5);
    CHECK_NE(cid1, cid2);
  }
  {
    auto cid = CID::kInvalid;
    // They are copy constructible...
    auto cid2 = cid;
    CHECK(!cid);
    CHECK_EQ(cid.length(), 0);
    CHECK_EQ(cid, cid2);
  }
  {
    auto cid1 = random.Generate();
    auto cid2 = random.Generate();
    CID::Map<std::string> map;
    map[cid1] = "hello";
    map[cid2] = "there";
    CHECK_EQ(map[cid1], "hello");
    CHECK_EQ(map[cid2], "there");
    CHECK_NE(map[cid2], "hello");
    CHECK_NE(map[cid1], "there");
  }
  {
    ngtcp2_cid cid_;
    uint8_t data[] = {1, 2, 3, 4, 5};
    ngtcp2_cid_init(&cid_, data, 5);
    auto cid = CID(cid_);
    // This variation of the constructor copies the cid_, so if we
    // modify the original data it doesn't change in the CID.
    cid_.data[0] = 9;
    CHECK_EQ(cid.length(), 5);
    CHECK_EQ(cid.ToString(), "0102030405");
  }
  {
    ngtcp2_cid cid_;
    uint8_t data[] = {1, 2, 3, 4, 5};
    ngtcp2_cid_init(&cid_, data, 5);
    auto cid = CID(&cid_);
    // This variation of the constructor wraps the cid_, so if we
    // modify the original data it does change in the CID.
    cid_.data[0] = 9;
    CHECK_EQ(cid.length(), 5);
    CHECK_EQ(cid.ToString(), "0902030405");
  }
  {
    // Generate a bunch to ensure that the pool is regenerated.
    for (int n = 0; n < 1000; n++) {
      random.Generate();
    }
  }
  {
    ngtcp2_cid cid_;
    // Generate a bunch to ensure that the pool is regenerated.
    for (int n = 0; n < 1000; n++) {
      random.GenerateInto(&cid_, 10);
      CHECK_EQ(cid_.datalen, 10);
    }
  }
}
#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
