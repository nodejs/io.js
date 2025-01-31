/* ====================================================================
 * Copyright (c) 2011 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com). */

#ifndef DEPS_NCRYPTO_DH_PRIMES_H_
#define DEPS_NCRYPTO_DH_PRIMES_H_

#include <openssl/dh.h>

#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/mem.h>

extern "C" int bn_set_words(BIGNUM* bn, const BN_ULONG* words, size_t num);

// Backporting primes that may not be supported in earlier boringssl versions.
// Intentionally keeping the existing C-style formatting.

#define OPENSSL_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

#if defined(OPENSSL_64_BIT)
#define TOBN(hi, lo) ((BN_ULONG)(hi) << 32 | (lo))
#elif defined(OPENSSL_32_BIT)
#define TOBN(hi, lo) (lo), (hi)
#else
#error "Must define either OPENSSL_32_BIT or OPENSSL_64_BIT"
#endif

static BIGNUM* get_params(BIGNUM* ret,
                          const BN_ULONG* words,
                          size_t num_words) {
  BIGNUM* alloc = nullptr;
  if (ret == nullptr) {
    alloc = BN_new();
    if (alloc == nullptr) {
      return nullptr;
    }
    ret = alloc;
  }

  if (!bn_set_words(ret, words, num_words)) {
    BN_free(alloc);
    return nullptr;
  }

  return ret;
}

BIGNUM* BN_get_rfc3526_prime_2048(BIGNUM* ret) {
  static const BN_ULONG kWords[] = {
      TOBN(0xffffffff, 0xffffffff), TOBN(0x15728e5a, 0x8aacaa68),
      TOBN(0x15d22618, 0x98fa0510), TOBN(0x3995497c, 0xea956ae5),
      TOBN(0xde2bcbf6, 0x95581718), TOBN(0xb5c55df0, 0x6f4c52c9),
      TOBN(0x9b2783a2, 0xec07a28f), TOBN(0xe39e772c, 0x180e8603),
      TOBN(0x32905e46, 0x2e36ce3b), TOBN(0xf1746c08, 0xca18217c),
      TOBN(0x670c354e, 0x4abc9804), TOBN(0x9ed52907, 0x7096966d),
      TOBN(0x1c62f356, 0x208552bb), TOBN(0x83655d23, 0xdca3ad96),
      TOBN(0x69163fa8, 0xfd24cf5f), TOBN(0x98da4836, 0x1c55d39a),
      TOBN(0xc2007cb8, 0xa163bf05), TOBN(0x49286651, 0xece45b3d),
      TOBN(0xae9f2411, 0x7c4b1fe6), TOBN(0xee386bfb, 0x5a899fa5),
      TOBN(0x0bff5cb6, 0xf406b7ed), TOBN(0xf44c42e9, 0xa637ed6b),
      TOBN(0xe485b576, 0x625e7ec6), TOBN(0x4fe1356d, 0x6d51c245),
      TOBN(0x302b0a6d, 0xf25f1437), TOBN(0xef9519b3, 0xcd3a431b),
      TOBN(0x514a0879, 0x8e3404dd), TOBN(0x020bbea6, 0x3b139b22),
      TOBN(0x29024e08, 0x8a67cc74), TOBN(0xc4c6628b, 0x80dc1cd1),
      TOBN(0xc90fdaa2, 0x2168c234), TOBN(0xffffffff, 0xffffffff),
  };
  return get_params(ret, kWords, OPENSSL_ARRAY_SIZE(kWords));
}

BIGNUM* BN_get_rfc3526_prime_3072(BIGNUM* ret) {
  static const BN_ULONG kWords[] = {
      TOBN(0xffffffff, 0xffffffff), TOBN(0x4b82d120, 0xa93ad2ca),
      TOBN(0x43db5bfc, 0xe0fd108e), TOBN(0x08e24fa0, 0x74e5ab31),
      TOBN(0x770988c0, 0xbad946e2), TOBN(0xbbe11757, 0x7a615d6c),
      TOBN(0x521f2b18, 0x177b200c), TOBN(0xd8760273, 0x3ec86a64),
      TOBN(0xf12ffa06, 0xd98a0864), TOBN(0xcee3d226, 0x1ad2ee6b),
      TOBN(0x1e8c94e0, 0x4a25619d), TOBN(0xabf5ae8c, 0xdb0933d7),
      TOBN(0xb3970f85, 0xa6e1e4c7), TOBN(0x8aea7157, 0x5d060c7d),
      TOBN(0xecfb8504, 0x58dbef0a), TOBN(0xa85521ab, 0xdf1cba64),
      TOBN(0xad33170d, 0x04507a33), TOBN(0x15728e5a, 0x8aaac42d),
      TOBN(0x15d22618, 0x98fa0510), TOBN(0x3995497c, 0xea956ae5),
      TOBN(0xde2bcbf6, 0x95581718), TOBN(0xb5c55df0, 0x6f4c52c9),
      TOBN(0x9b2783a2, 0xec07a28f), TOBN(0xe39e772c, 0x180e8603),
      TOBN(0x32905e46, 0x2e36ce3b), TOBN(0xf1746c08, 0xca18217c),
      TOBN(0x670c354e, 0x4abc9804), TOBN(0x9ed52907, 0x7096966d),
      TOBN(0x1c62f356, 0x208552bb), TOBN(0x83655d23, 0xdca3ad96),
      TOBN(0x69163fa8, 0xfd24cf5f), TOBN(0x98da4836, 0x1c55d39a),
      TOBN(0xc2007cb8, 0xa163bf05), TOBN(0x49286651, 0xece45b3d),
      TOBN(0xae9f2411, 0x7c4b1fe6), TOBN(0xee386bfb, 0x5a899fa5),
      TOBN(0x0bff5cb6, 0xf406b7ed), TOBN(0xf44c42e9, 0xa637ed6b),
      TOBN(0xe485b576, 0x625e7ec6), TOBN(0x4fe1356d, 0x6d51c245),
      TOBN(0x302b0a6d, 0xf25f1437), TOBN(0xef9519b3, 0xcd3a431b),
      TOBN(0x514a0879, 0x8e3404dd), TOBN(0x020bbea6, 0x3b139b22),
      TOBN(0x29024e08, 0x8a67cc74), TOBN(0xc4c6628b, 0x80dc1cd1),
      TOBN(0xc90fdaa2, 0x2168c234), TOBN(0xffffffff, 0xffffffff),
  };
  return get_params(ret, kWords, OPENSSL_ARRAY_SIZE(kWords));
}

BIGNUM* BN_get_rfc3526_prime_4096(BIGNUM* ret) {
  static const BN_ULONG kWords[] = {
      TOBN(0xffffffff, 0xffffffff), TOBN(0x4df435c9, 0x34063199),
      TOBN(0x86ffb7dc, 0x90a6c08f), TOBN(0x93b4ea98, 0x8d8fddc1),
      TOBN(0xd0069127, 0xd5b05aa9), TOBN(0xb81bdd76, 0x2170481c),
      TOBN(0x1f612970, 0xcee2d7af), TOBN(0x233ba186, 0x515be7ed),
      TOBN(0x99b2964f, 0xa090c3a2), TOBN(0x287c5947, 0x4e6bc05d),
      TOBN(0x2e8efc14, 0x1fbecaa6), TOBN(0xdbbbc2db, 0x04de8ef9),
      TOBN(0x2583e9ca, 0x2ad44ce8), TOBN(0x1a946834, 0xb6150bda),
      TOBN(0x99c32718, 0x6af4e23c), TOBN(0x88719a10, 0xbdba5b26),
      TOBN(0x1a723c12, 0xa787e6d7), TOBN(0x4b82d120, 0xa9210801),
      TOBN(0x43db5bfc, 0xe0fd108e), TOBN(0x08e24fa0, 0x74e5ab31),
      TOBN(0x770988c0, 0xbad946e2), TOBN(0xbbe11757, 0x7a615d6c),
      TOBN(0x521f2b18, 0x177b200c), TOBN(0xd8760273, 0x3ec86a64),
      TOBN(0xf12ffa06, 0xd98a0864), TOBN(0xcee3d226, 0x1ad2ee6b),
      TOBN(0x1e8c94e0, 0x4a25619d), TOBN(0xabf5ae8c, 0xdb0933d7),
      TOBN(0xb3970f85, 0xa6e1e4c7), TOBN(0x8aea7157, 0x5d060c7d),
      TOBN(0xecfb8504, 0x58dbef0a), TOBN(0xa85521ab, 0xdf1cba64),
      TOBN(0xad33170d, 0x04507a33), TOBN(0x15728e5a, 0x8aaac42d),
      TOBN(0x15d22618, 0x98fa0510), TOBN(0x3995497c, 0xea956ae5),
      TOBN(0xde2bcbf6, 0x95581718), TOBN(0xb5c55df0, 0x6f4c52c9),
      TOBN(0x9b2783a2, 0xec07a28f), TOBN(0xe39e772c, 0x180e8603),
      TOBN(0x32905e46, 0x2e36ce3b), TOBN(0xf1746c08, 0xca18217c),
      TOBN(0x670c354e, 0x4abc9804), TOBN(0x9ed52907, 0x7096966d),
      TOBN(0x1c62f356, 0x208552bb), TOBN(0x83655d23, 0xdca3ad96),
      TOBN(0x69163fa8, 0xfd24cf5f), TOBN(0x98da4836, 0x1c55d39a),
      TOBN(0xc2007cb8, 0xa163bf05), TOBN(0x49286651, 0xece45b3d),
      TOBN(0xae9f2411, 0x7c4b1fe6), TOBN(0xee386bfb, 0x5a899fa5),
      TOBN(0x0bff5cb6, 0xf406b7ed), TOBN(0xf44c42e9, 0xa637ed6b),
      TOBN(0xe485b576, 0x625e7ec6), TOBN(0x4fe1356d, 0x6d51c245),
      TOBN(0x302b0a6d, 0xf25f1437), TOBN(0xef9519b3, 0xcd3a431b),
      TOBN(0x514a0879, 0x8e3404dd), TOBN(0x020bbea6, 0x3b139b22),
      TOBN(0x29024e08, 0x8a67cc74), TOBN(0xc4c6628b, 0x80dc1cd1),
      TOBN(0xc90fdaa2, 0x2168c234), TOBN(0xffffffff, 0xffffffff),
  };
  return get_params(ret, kWords, OPENSSL_ARRAY_SIZE(kWords));
}

BIGNUM* BN_get_rfc3526_prime_6144(BIGNUM* ret) {
  static const BN_ULONG kWords[] = {
      TOBN(0xffffffff, 0xffffffff), TOBN(0xe694f91e, 0x6dcc4024),
      TOBN(0x12bf2d5b, 0x0b7474d6), TOBN(0x043e8f66, 0x3f4860ee),
      TOBN(0x387fe8d7, 0x6e3c0468), TOBN(0xda56c9ec, 0x2ef29632),
      TOBN(0xeb19ccb1, 0xa313d55c), TOBN(0xf550aa3d, 0x8a1fbff0),
      TOBN(0x06a1d58b, 0xb7c5da76), TOBN(0xa79715ee, 0xf29be328),
      TOBN(0x14cc5ed2, 0x0f8037e0), TOBN(0xcc8f6d7e, 0xbf48e1d8),
      TOBN(0x4bd407b2, 0x2b4154aa), TOBN(0x0f1d45b7, 0xff585ac5),
      TOBN(0x23a97a7e, 0x36cc88be), TOBN(0x59e7c97f, 0xbec7e8f3),
      TOBN(0xb5a84031, 0x900b1c9e), TOBN(0xd55e702f, 0x46980c82),
      TOBN(0xf482d7ce, 0x6e74fef6), TOBN(0xf032ea15, 0xd1721d03),
      TOBN(0x5983ca01, 0xc64b92ec), TOBN(0x6fb8f401, 0x378cd2bf),
      TOBN(0x33205151, 0x2bd7af42), TOBN(0xdb7f1447, 0xe6cc254b),
      TOBN(0x44ce6cba, 0xced4bb1b), TOBN(0xda3edbeb, 0xcf9b14ed),
      TOBN(0x179727b0, 0x865a8918), TOBN(0xb06a53ed, 0x9027d831),
      TOBN(0xe5db382f, 0x413001ae), TOBN(0xf8ff9406, 0xad9e530e),
      TOBN(0xc9751e76, 0x3dba37bd), TOBN(0xc1d4dcb2, 0x602646de),
      TOBN(0x36c3fab4, 0xd27c7026), TOBN(0x4df435c9, 0x34028492),
      TOBN(0x86ffb7dc, 0x90a6c08f), TOBN(0x93b4ea98, 0x8d8fddc1),
      TOBN(0xd0069127, 0xd5b05aa9), TOBN(0xb81bdd76, 0x2170481c),
      TOBN(0x1f612970, 0xcee2d7af), TOBN(0x233ba186, 0x515be7ed),
      TOBN(0x99b2964f, 0xa090c3a2), TOBN(0x287c5947, 0x4e6bc05d),
      TOBN(0x2e8efc14, 0x1fbecaa6), TOBN(0xdbbbc2db, 0x04de8ef9),
      TOBN(0x2583e9ca, 0x2ad44ce8), TOBN(0x1a946834, 0xb6150bda),
      TOBN(0x99c32718, 0x6af4e23c), TOBN(0x88719a10, 0xbdba5b26),
      TOBN(0x1a723c12, 0xa787e6d7), TOBN(0x4b82d120, 0xa9210801),
      TOBN(0x43db5bfc, 0xe0fd108e), TOBN(0x08e24fa0, 0x74e5ab31),
      TOBN(0x770988c0, 0xbad946e2), TOBN(0xbbe11757, 0x7a615d6c),
      TOBN(0x521f2b18, 0x177b200c), TOBN(0xd8760273, 0x3ec86a64),
      TOBN(0xf12ffa06, 0xd98a0864), TOBN(0xcee3d226, 0x1ad2ee6b),
      TOBN(0x1e8c94e0, 0x4a25619d), TOBN(0xabf5ae8c, 0xdb0933d7),
      TOBN(0xb3970f85, 0xa6e1e4c7), TOBN(0x8aea7157, 0x5d060c7d),
      TOBN(0xecfb8504, 0x58dbef0a), TOBN(0xa85521ab, 0xdf1cba64),
      TOBN(0xad33170d, 0x04507a33), TOBN(0x15728e5a, 0x8aaac42d),
      TOBN(0x15d22618, 0x98fa0510), TOBN(0x3995497c, 0xea956ae5),
      TOBN(0xde2bcbf6, 0x95581718), TOBN(0xb5c55df0, 0x6f4c52c9),
      TOBN(0x9b2783a2, 0xec07a28f), TOBN(0xe39e772c, 0x180e8603),
      TOBN(0x32905e46, 0x2e36ce3b), TOBN(0xf1746c08, 0xca18217c),
      TOBN(0x670c354e, 0x4abc9804), TOBN(0x9ed52907, 0x7096966d),
      TOBN(0x1c62f356, 0x208552bb), TOBN(0x83655d23, 0xdca3ad96),
      TOBN(0x69163fa8, 0xfd24cf5f), TOBN(0x98da4836, 0x1c55d39a),
      TOBN(0xc2007cb8, 0xa163bf05), TOBN(0x49286651, 0xece45b3d),
      TOBN(0xae9f2411, 0x7c4b1fe6), TOBN(0xee386bfb, 0x5a899fa5),
      TOBN(0x0bff5cb6, 0xf406b7ed), TOBN(0xf44c42e9, 0xa637ed6b),
      TOBN(0xe485b576, 0x625e7ec6), TOBN(0x4fe1356d, 0x6d51c245),
      TOBN(0x302b0a6d, 0xf25f1437), TOBN(0xef9519b3, 0xcd3a431b),
      TOBN(0x514a0879, 0x8e3404dd), TOBN(0x020bbea6, 0x3b139b22),
      TOBN(0x29024e08, 0x8a67cc74), TOBN(0xc4c6628b, 0x80dc1cd1),
      TOBN(0xc90fdaa2, 0x2168c234), TOBN(0xffffffff, 0xffffffff),
  };
  return get_params(ret, kWords, OPENSSL_ARRAY_SIZE(kWords));
}

BIGNUM* BN_get_rfc3526_prime_8192(BIGNUM* ret) {
  static const BN_ULONG kWords[] = {
      TOBN(0xffffffff, 0xffffffff), TOBN(0x60c980dd, 0x98edd3df),
      TOBN(0xc81f56e8, 0x80b96e71), TOBN(0x9e3050e2, 0x765694df),
      TOBN(0x9558e447, 0x5677e9aa), TOBN(0xc9190da6, 0xfc026e47),
      TOBN(0x889a002e, 0xd5ee382b), TOBN(0x4009438b, 0x481c6cd7),
      TOBN(0x359046f4, 0xeb879f92), TOBN(0xfaf36bc3, 0x1ecfa268),
      TOBN(0xb1d510bd, 0x7ee74d73), TOBN(0xf9ab4819, 0x5ded7ea1),
      TOBN(0x64f31cc5, 0x0846851d), TOBN(0x4597e899, 0xa0255dc1),
      TOBN(0xdf310ee0, 0x74ab6a36), TOBN(0x6d2a13f8, 0x3f44f82d),
      TOBN(0x062b3cf5, 0xb3a278a6), TOBN(0x79683303, 0xed5bdd3a),
      TOBN(0xfa9d4b7f, 0xa2c087e8), TOBN(0x4bcbc886, 0x2f8385dd),
      TOBN(0x3473fc64, 0x6cea306b), TOBN(0x13eb57a8, 0x1a23f0c7),
      TOBN(0x22222e04, 0xa4037c07), TOBN(0xe3fdb8be, 0xfc848ad9),
      TOBN(0x238f16cb, 0xe39d652d), TOBN(0x3423b474, 0x2bf1c978),
      TOBN(0x3aab639c, 0x5ae4f568), TOBN(0x2576f693, 0x6ba42466),
      TOBN(0x741fa7bf, 0x8afc47ed), TOBN(0x3bc832b6, 0x8d9dd300),
      TOBN(0xd8bec4d0, 0x73b931ba), TOBN(0x38777cb6, 0xa932df8c),
      TOBN(0x74a3926f, 0x12fee5e4), TOBN(0xe694f91e, 0x6dbe1159),
      TOBN(0x12bf2d5b, 0x0b7474d6), TOBN(0x043e8f66, 0x3f4860ee),
      TOBN(0x387fe8d7, 0x6e3c0468), TOBN(0xda56c9ec, 0x2ef29632),
      TOBN(0xeb19ccb1, 0xa313d55c), TOBN(0xf550aa3d, 0x8a1fbff0),
      TOBN(0x06a1d58b, 0xb7c5da76), TOBN(0xa79715ee, 0xf29be328),
      TOBN(0x14cc5ed2, 0x0f8037e0), TOBN(0xcc8f6d7e, 0xbf48e1d8),
      TOBN(0x4bd407b2, 0x2b4154aa), TOBN(0x0f1d45b7, 0xff585ac5),
      TOBN(0x23a97a7e, 0x36cc88be), TOBN(0x59e7c97f, 0xbec7e8f3),
      TOBN(0xb5a84031, 0x900b1c9e), TOBN(0xd55e702f, 0x46980c82),
      TOBN(0xf482d7ce, 0x6e74fef6), TOBN(0xf032ea15, 0xd1721d03),
      TOBN(0x5983ca01, 0xc64b92ec), TOBN(0x6fb8f401, 0x378cd2bf),
      TOBN(0x33205151, 0x2bd7af42), TOBN(0xdb7f1447, 0xe6cc254b),
      TOBN(0x44ce6cba, 0xced4bb1b), TOBN(0xda3edbeb, 0xcf9b14ed),
      TOBN(0x179727b0, 0x865a8918), TOBN(0xb06a53ed, 0x9027d831),
      TOBN(0xe5db382f, 0x413001ae), TOBN(0xf8ff9406, 0xad9e530e),
      TOBN(0xc9751e76, 0x3dba37bd), TOBN(0xc1d4dcb2, 0x602646de),
      TOBN(0x36c3fab4, 0xd27c7026), TOBN(0x4df435c9, 0x34028492),
      TOBN(0x86ffb7dc, 0x90a6c08f), TOBN(0x93b4ea98, 0x8d8fddc1),
      TOBN(0xd0069127, 0xd5b05aa9), TOBN(0xb81bdd76, 0x2170481c),
      TOBN(0x1f612970, 0xcee2d7af), TOBN(0x233ba186, 0x515be7ed),
      TOBN(0x99b2964f, 0xa090c3a2), TOBN(0x287c5947, 0x4e6bc05d),
      TOBN(0x2e8efc14, 0x1fbecaa6), TOBN(0xdbbbc2db, 0x04de8ef9),
      TOBN(0x2583e9ca, 0x2ad44ce8), TOBN(0x1a946834, 0xb6150bda),
      TOBN(0x99c32718, 0x6af4e23c), TOBN(0x88719a10, 0xbdba5b26),
      TOBN(0x1a723c12, 0xa787e6d7), TOBN(0x4b82d120, 0xa9210801),
      TOBN(0x43db5bfc, 0xe0fd108e), TOBN(0x08e24fa0, 0x74e5ab31),
      TOBN(0x770988c0, 0xbad946e2), TOBN(0xbbe11757, 0x7a615d6c),
      TOBN(0x521f2b18, 0x177b200c), TOBN(0xd8760273, 0x3ec86a64),
      TOBN(0xf12ffa06, 0xd98a0864), TOBN(0xcee3d226, 0x1ad2ee6b),
      TOBN(0x1e8c94e0, 0x4a25619d), TOBN(0xabf5ae8c, 0xdb0933d7),
      TOBN(0xb3970f85, 0xa6e1e4c7), TOBN(0x8aea7157, 0x5d060c7d),
      TOBN(0xecfb8504, 0x58dbef0a), TOBN(0xa85521ab, 0xdf1cba64),
      TOBN(0xad33170d, 0x04507a33), TOBN(0x15728e5a, 0x8aaac42d),
      TOBN(0x15d22618, 0x98fa0510), TOBN(0x3995497c, 0xea956ae5),
      TOBN(0xde2bcbf6, 0x95581718), TOBN(0xb5c55df0, 0x6f4c52c9),
      TOBN(0x9b2783a2, 0xec07a28f), TOBN(0xe39e772c, 0x180e8603),
      TOBN(0x32905e46, 0x2e36ce3b), TOBN(0xf1746c08, 0xca18217c),
      TOBN(0x670c354e, 0x4abc9804), TOBN(0x9ed52907, 0x7096966d),
      TOBN(0x1c62f356, 0x208552bb), TOBN(0x83655d23, 0xdca3ad96),
      TOBN(0x69163fa8, 0xfd24cf5f), TOBN(0x98da4836, 0x1c55d39a),
      TOBN(0xc2007cb8, 0xa163bf05), TOBN(0x49286651, 0xece45b3d),
      TOBN(0xae9f2411, 0x7c4b1fe6), TOBN(0xee386bfb, 0x5a899fa5),
      TOBN(0x0bff5cb6, 0xf406b7ed), TOBN(0xf44c42e9, 0xa637ed6b),
      TOBN(0xe485b576, 0x625e7ec6), TOBN(0x4fe1356d, 0x6d51c245),
      TOBN(0x302b0a6d, 0xf25f1437), TOBN(0xef9519b3, 0xcd3a431b),
      TOBN(0x514a0879, 0x8e3404dd), TOBN(0x020bbea6, 0x3b139b22),
      TOBN(0x29024e08, 0x8a67cc74), TOBN(0xc4c6628b, 0x80dc1cd1),
      TOBN(0xc90fdaa2, 0x2168c234), TOBN(0xffffffff, 0xffffffff),
  };
  return get_params(ret, kWords, OPENSSL_ARRAY_SIZE(kWords));
}

#endif  // DEPS_NCRYPTO_DH_PRIMES_H_
