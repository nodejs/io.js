#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#include <env-inl.h>
#include <gtest/gtest.h>
#include <quic/data.h>
#include <util-inl.h>
#include <string>

using node::quic::QuicError;

TEST(QuicError, NoError) {
  QuicError err;
  CHECK_EQ(err.code(), QuicError::QUIC_NO_ERROR);
  CHECK_EQ(err.type(), QuicError::Type::TRANSPORT);
  CHECK_EQ(err.reason(), "");
  CHECK_EQ(err, QuicError::TRANSPORT_NO_ERROR);
  CHECK(!err);

  QuicError err2("a reason");
  CHECK_EQ(err2.code(), QuicError::QUIC_NO_ERROR);
  CHECK_EQ(err2.type(), QuicError::Type::TRANSPORT);
  CHECK_EQ(err2.reason(), "a reason");

  // Equality check ignores the reason
  CHECK_EQ(err2, QuicError::TRANSPORT_NO_ERROR);

  auto err3 = QuicError::ForTransport(QuicError::QUIC_NO_ERROR);
  CHECK_EQ(err3.code(), QuicError::QUIC_NO_ERROR);
  CHECK_EQ(err3.type(), QuicError::Type::TRANSPORT);
  CHECK_EQ(err3.reason(), "");
  CHECK_EQ(err3, QuicError::TRANSPORT_NO_ERROR);

  // QuicError's are copy assignable
  auto err4 = err3;
  CHECK_EQ(err4, err3);

  // QuicError's are movable
  auto err5 = std::move(err4);
  CHECK_EQ(err5, err3);

  // Equality check ignores the reason
  CHECK(err5 == err2);
  CHECK(err5 != QuicError::APPLICATION_NO_ERROR);

  const ngtcp2_ccerr& ccerr = err5;
  CHECK_EQ(ccerr.error_code, NGTCP2_NO_ERROR);
  CHECK_EQ(ccerr.frame_type, 0);
  CHECK_EQ(ccerr.type, NGTCP2_CCERR_TYPE_TRANSPORT);
  CHECK_EQ(ccerr.reasonlen, 0);

  const ngtcp2_ccerr* ccerr2 = err5;
  CHECK_EQ(ccerr2->error_code, NGTCP2_NO_ERROR);
  CHECK_EQ(ccerr2->frame_type, 0);
  CHECK_EQ(ccerr2->type, NGTCP2_CCERR_TYPE_TRANSPORT);
  CHECK_EQ(ccerr2->reasonlen, 0);

  QuicError err6(ccerr);
  QuicError err7(ccerr2);
  CHECK_EQ(err6, err7);

  CHECK_EQ(err.ToString(), "QuicError(TRANSPORT) 0");
  CHECK_EQ(err2.ToString(), "QuicError(TRANSPORT) 0: a reason");

  ngtcp2_ccerr ccerr3;
  ngtcp2_ccerr_default(&ccerr3);
  ccerr3.frame_type = 1;
  QuicError err8(ccerr3);
  CHECK_EQ(err8.frame_type(), 1);
}

TEST(QuicError, ApplicationNoError) {
  CHECK_EQ(QuicError::APPLICATION_NO_ERROR.code(),
           QuicError::QUIC_APP_NO_ERROR);
  CHECK_EQ(QuicError::APPLICATION_NO_ERROR.type(),
           QuicError::Type::APPLICATION);
  CHECK_EQ(QuicError::APPLICATION_NO_ERROR.reason(), "");

  auto err =
      QuicError::ForApplication(QuicError::QUIC_APP_NO_ERROR, "a reason");
  CHECK_EQ(err.code(), QuicError::QUIC_APP_NO_ERROR);
  CHECK_EQ(err.type(), QuicError::Type::APPLICATION);
  CHECK_EQ(err.reason(), "a reason");
  CHECK_EQ(err.ToString(), "QuicError(APPLICATION) 65280: a reason");
}

TEST(QuicError, VersionNegotiation) {
  CHECK_EQ(QuicError::VERSION_NEGOTIATION.code(), 0);
  CHECK_EQ(QuicError::VERSION_NEGOTIATION.type(),
           QuicError::Type::VERSION_NEGOTIATION);
  CHECK_EQ(QuicError::VERSION_NEGOTIATION.reason(), "");

  auto err = QuicError::ForVersionNegotiation("a reason");
  CHECK_EQ(err.code(), 0);
  CHECK_EQ(err.type(), QuicError::Type::VERSION_NEGOTIATION);
  CHECK_EQ(err.reason(), "a reason");
  CHECK_EQ(err.ToString(), "QuicError(VERSION_NEGOTIATION) 0: a reason");
}

TEST(QuicError, IdleClose) {
  CHECK_EQ(QuicError::IDLE_CLOSE.code(), 0);
  CHECK_EQ(QuicError::IDLE_CLOSE.type(), QuicError::Type::IDLE_CLOSE);
  CHECK_EQ(QuicError::IDLE_CLOSE.reason(), "");

  auto err = QuicError::ForIdleClose("a reason");
  CHECK_EQ(err.code(), 0);
  CHECK_EQ(err.type(), QuicError::Type::IDLE_CLOSE);
  CHECK_EQ(err.reason(), "a reason");
  CHECK_EQ(err.ToString(), "QuicError(IDLE_CLOSE) 0: a reason");

  CHECK_EQ(QuicError::IDLE_CLOSE, err);
}

TEST(QuicError, InternalError) {
  auto err = QuicError::ForNgtcp2Error(NGTCP2_ERR_INTERNAL, "a reason");
  CHECK_EQ(err.code(), NGTCP2_INTERNAL_ERROR);
  CHECK_EQ(err.type(), QuicError::Type::TRANSPORT);
  CHECK_EQ(err.reason(), "a reason");
  CHECK_EQ(err.ToString(), "QuicError(TRANSPORT) 1: a reason");

  printf("%s\n", QuicError::INTERNAL_ERROR.ToString().c_str());
  CHECK_EQ(err, QuicError::INTERNAL_ERROR);
}

TEST(QuicError, TlsAlert) {
  auto err = QuicError::ForTlsAlert(1, "a reason");
  CHECK_EQ(err.code(), 257);
  CHECK_EQ(err.type(), QuicError::Type::TRANSPORT);
  CHECK_EQ(err.reason(), "a reason");
  CHECK(err.is_crypto());
  CHECK_EQ(err.crypto_error(), 1);
}

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
