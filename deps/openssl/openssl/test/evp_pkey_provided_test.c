/*
 * Copyright 2019-2024 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <string.h> /* memset */
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/encoder.h>
#include <openssl/provider.h>
#include <openssl/param_build.h>
#include <openssl/core_names.h>
#include <openssl/sha.h>
#include "crypto/ecx.h"
#include "crypto/evp.h"          /* For the internal API */
#include "crypto/bn_dh.h"        /* _bignum_ffdhe2048_p */
#include "internal/nelem.h"
#include "testutil.h"

static char *datadir = NULL;

/*
 * Do not change the order of the following defines unless you also
 * update the for loop bounds used inside test_print_key_using_encoder() and
 * test_print_key_using_encoder_public().
 */
#define PRIV_TEXT    0
#define PRIV_PEM     1
#define PRIV_DER     2
#define PUB_TEXT     3
#define PUB_PEM      4
#define PUB_DER      5

static void stripcr(char *buf, size_t *len)
{
    size_t i;
    char *curr, *writ;

    for (i = *len, curr = buf, writ = buf; i > 0; i--, curr++) {
        if (*curr == '\r') {
            (*len)--;
            continue;
        }
        if (curr != writ)
            *writ = *curr;
        writ++;
    }
}

static int compare_with_file(const char *alg, int type, BIO *membio)
{
    char filename[80];
    BIO *file = NULL;
    char buf[4096];
    char *memdata, *fullfile = NULL;
    const char *suffix;
    size_t readbytes;
    int ret = 0;
    int len;
    size_t slen;

    switch (type) {
    case PRIV_TEXT:
        suffix = "priv.txt";
        break;

    case PRIV_PEM:
        suffix = "priv.pem";
        break;

    case PRIV_DER:
        suffix = "priv.der";
        break;

    case PUB_TEXT:
        suffix = "pub.txt";
        break;

    case PUB_PEM:
        suffix = "pub.pem";
        break;

    case PUB_DER:
        suffix = "pub.der";
        break;

    default:
        TEST_error("Invalid file type");
        goto err;
    }

    BIO_snprintf(filename, sizeof(filename), "%s.%s", alg, suffix);
    fullfile = test_mk_file_path(datadir, filename);
    if (!TEST_ptr(fullfile))
        goto err;

    file = BIO_new_file(fullfile, "rb");
    if (!TEST_ptr(file))
        goto err;

    if (!TEST_true(BIO_read_ex(file, buf, sizeof(buf), &readbytes))
            || !TEST_true(BIO_eof(file))
            || !TEST_size_t_lt(readbytes, sizeof(buf)))
        goto err;

    len = BIO_get_mem_data(membio, &memdata);
    if (!TEST_int_gt(len, 0))
        goto err;

    slen = len;
    if (type != PRIV_DER && type != PUB_DER) {
        stripcr(memdata, &slen);
        stripcr(buf, &readbytes);
    }

    if (!TEST_mem_eq(memdata, slen, buf, readbytes))
        goto err;

    ret = 1;
 err:
    OPENSSL_free(fullfile);
    (void)BIO_reset(membio);
    BIO_free(file);
    return ret;
}

static int pass_cb(char *buf, int size, int rwflag, void *u)
{
    return 0;
}

static int pass_cb_error(char *buf, int size, int rwflag, void *u)
{
    return -1;
}

static int test_print_key_using_pem(const char *alg, const EVP_PKEY *pk)
{
    BIO *membio = BIO_new(BIO_s_mem());
    int ret = 0;

    if (!TEST_ptr(membio))
        goto err;

    if (/* Output Encrypted private key in PEM form */
        !TEST_true(PEM_write_bio_PrivateKey(bio_out, pk, EVP_aes_256_cbc(),
                                            (unsigned char *)"pass", 4,
                                            NULL, NULL))
        /* Output zero-length passphrase encrypted private key in PEM form */
        || !TEST_true(PEM_write_bio_PKCS8PrivateKey(bio_out, pk,
                                                    EVP_aes_256_cbc(),
                                                    (const char *)~0, 0,
                                                    NULL, NULL))
        || !TEST_true(PEM_write_bio_PKCS8PrivateKey(bio_out, pk,
                                                    EVP_aes_256_cbc(),
                                                    NULL, 0, NULL, ""))
        || !TEST_true(PEM_write_bio_PKCS8PrivateKey(bio_out, pk,
                                                    EVP_aes_256_cbc(),
                                                    NULL, 0, pass_cb, NULL))
        || !TEST_false(PEM_write_bio_PKCS8PrivateKey(bio_out, pk,
                                                     EVP_aes_256_cbc(),
                                                     NULL, 0, pass_cb_error,
                                                     NULL))
#ifndef OPENSSL_NO_DES
        || !TEST_true(PEM_write_bio_PKCS8PrivateKey_nid(
            bio_out, pk, NID_pbe_WithSHA1And3_Key_TripleDES_CBC,
            (const char *)~0, 0, NULL, NULL))
        || !TEST_true(PEM_write_bio_PKCS8PrivateKey_nid(
            bio_out, pk, NID_pbe_WithSHA1And3_Key_TripleDES_CBC, NULL, 0,
            NULL, ""))
        || !TEST_true(PEM_write_bio_PKCS8PrivateKey_nid(
            bio_out, pk, NID_pbe_WithSHA1And3_Key_TripleDES_CBC, NULL, 0,
            pass_cb, NULL))
        || !TEST_false(PEM_write_bio_PKCS8PrivateKey_nid(
            bio_out, pk, NID_pbe_WithSHA1And3_Key_TripleDES_CBC, NULL, 0,
            pass_cb_error, NULL))
#endif
        /* Private key in text form */
        || !TEST_int_gt(EVP_PKEY_print_private(membio, pk, 0, NULL), 0)
        || !TEST_true(compare_with_file(alg, PRIV_TEXT, membio))
        /* Public key in PEM form */
        || !TEST_true(PEM_write_bio_PUBKEY(membio, pk))
        || !TEST_true(compare_with_file(alg, PUB_PEM, membio))
        /* Unencrypted private key in PEM form */
        || !TEST_true(PEM_write_bio_PrivateKey(membio, pk,
                                               NULL, NULL, 0, NULL, NULL))
        || !TEST_true(compare_with_file(alg, PRIV_PEM, membio))
        /* NULL key */
        || !TEST_false(PEM_write_bio_PrivateKey(membio, NULL,
                                               NULL, NULL, 0, NULL, NULL))
        || !TEST_false(PEM_write_bio_PrivateKey_traditional(membio, NULL,
                                               NULL, NULL, 0, NULL, NULL)))
        goto err;

    ret = 1;
 err:
    BIO_free(membio);
    return ret;
}

static int test_print_key_type_using_encoder(const char *alg, int type,
                                             const EVP_PKEY *pk)
{
    const char *output_type, *output_structure;
    int selection;
    OSSL_ENCODER_CTX *ctx = NULL;
    BIO *membio = BIO_new(BIO_s_mem());
    int ret = 0;

    switch (type) {
    case PRIV_TEXT:
        output_type = "TEXT";
        output_structure = NULL;
        selection = OSSL_KEYMGMT_SELECT_KEYPAIR
            | OSSL_KEYMGMT_SELECT_DOMAIN_PARAMETERS;
        break;

    case PRIV_PEM:
        output_type = "PEM";
        output_structure = "PrivateKeyInfo";
        selection = OSSL_KEYMGMT_SELECT_KEYPAIR
            | OSSL_KEYMGMT_SELECT_DOMAIN_PARAMETERS;
        break;

    case PRIV_DER:
        output_type = "DER";
        output_structure = "PrivateKeyInfo";
        selection = OSSL_KEYMGMT_SELECT_KEYPAIR
            | OSSL_KEYMGMT_SELECT_DOMAIN_PARAMETERS;
        break;

    case PUB_TEXT:
        output_type = "TEXT";
        output_structure = NULL;
        selection = OSSL_KEYMGMT_SELECT_PUBLIC_KEY
            | OSSL_KEYMGMT_SELECT_DOMAIN_PARAMETERS;
        break;

    case PUB_PEM:
        output_type = "PEM";
        output_structure = "SubjectPublicKeyInfo";
        selection = OSSL_KEYMGMT_SELECT_PUBLIC_KEY
            | OSSL_KEYMGMT_SELECT_DOMAIN_PARAMETERS;
        break;

    case PUB_DER:
        output_type = "DER";
        output_structure = "SubjectPublicKeyInfo";
        selection = OSSL_KEYMGMT_SELECT_PUBLIC_KEY
            | OSSL_KEYMGMT_SELECT_DOMAIN_PARAMETERS;
        break;

    default:
        TEST_error("Invalid encoding type");
        goto err;
    }

    if (!TEST_ptr(membio))
        goto err;

    /* Make a context, it's valid for several prints */
    TEST_note("Setting up a OSSL_ENCODER context with passphrase");
    if (!TEST_ptr(ctx = OSSL_ENCODER_CTX_new_for_pkey(pk, selection,
                                                      output_type,
                                                      output_structure,
                                                      NULL))
        /* Check that this operation is supported */
        || !TEST_int_ne(OSSL_ENCODER_CTX_get_num_encoders(ctx), 0))
        goto err;

    /* Use no cipher.  This should give us an unencrypted PEM */
    TEST_note("Testing with no encryption");
    if (!TEST_true(OSSL_ENCODER_to_bio(ctx, membio))
        || !TEST_true(compare_with_file(alg, type, membio)))
        goto err;

    if (type == PRIV_PEM) {
        /* Set a passphrase to be used later */
        if (!TEST_true(OSSL_ENCODER_CTX_set_passphrase(ctx,
                                                          (unsigned char *)"pass",
                                                          4)))
            goto err;

        /* Use a valid cipher name */
        TEST_note("Displaying PEM encrypted with AES-256-CBC");
        if (!TEST_true(OSSL_ENCODER_CTX_set_cipher(ctx, "AES-256-CBC", NULL))
            || !TEST_true(OSSL_ENCODER_to_bio(ctx, bio_out)))
            goto err;

        /* Use an invalid cipher name, which should generate no output */
        TEST_note("NOT Displaying PEM encrypted with (invalid) FOO");
        if (!TEST_false(OSSL_ENCODER_CTX_set_cipher(ctx, "FOO", NULL))
            || !TEST_false(OSSL_ENCODER_to_bio(ctx, bio_out)))
            goto err;

        /* Clear the cipher.  This should give us an unencrypted PEM again */
        TEST_note("Testing with encryption cleared (no encryption)");
        if (!TEST_true(OSSL_ENCODER_CTX_set_cipher(ctx, NULL, NULL))
            || !TEST_true(OSSL_ENCODER_to_bio(ctx, membio))
            || !TEST_true(compare_with_file(alg, type, membio)))
            goto err;
    }
    ret = 1;
err:
    BIO_free(membio);
    OSSL_ENCODER_CTX_free(ctx);
    return ret;
}

static int test_print_key_using_encoder(const char *alg, const EVP_PKEY *pk)
{
    int i;
    int ret = 1;

    for (i = PRIV_TEXT; i <= PUB_DER; i++)
        ret = ret && test_print_key_type_using_encoder(alg, i, pk);

    return ret;
}

#ifndef OPENSSL_NO_EC
static int test_print_key_using_encoder_public(const char *alg,
                                               const EVP_PKEY *pk)
{
    int i;
    int ret = 1;

    for (i = PUB_TEXT; i <= PUB_DER; i++)
        ret = ret && test_print_key_type_using_encoder(alg, i, pk);

    return ret;
}
#endif

/* Array indexes used in test_fromdata_rsa */
#define N       0
#define E       1
#define D       2
#define P       3
#define Q       4
#define DP      5
#define DQ      6
#define QINV    7

static int test_fromdata_rsa(void)
{
    int ret = 0, i;
    EVP_PKEY_CTX *ctx = NULL, *key_ctx = NULL;
    EVP_PKEY *pk = NULL, *copy_pk = NULL, *dup_pk = NULL;
    /*
     * 32-bit RSA key, extracted from this command,
     * executed with OpenSSL 1.0.2:
     *
     * openssl genrsa 32 | openssl rsa -text
     */
    static unsigned long key_numbers[] = {
        0xbc747fc5,              /* N */
        0x10001,                 /* E */
        0x7b133399,              /* D */
        0xe963,                  /* P */
        0xceb7,                  /* Q */
        0x8599,                  /* DP */
        0xbd87,                  /* DQ */
        0xcc3b,                  /* QINV */
    };
    OSSL_PARAM fromdata_params[] = {
        OSSL_PARAM_ulong(OSSL_PKEY_PARAM_RSA_N, &key_numbers[N]),
        OSSL_PARAM_ulong(OSSL_PKEY_PARAM_RSA_E, &key_numbers[E]),
        OSSL_PARAM_ulong(OSSL_PKEY_PARAM_RSA_D, &key_numbers[D]),
        OSSL_PARAM_ulong(OSSL_PKEY_PARAM_RSA_FACTOR1, &key_numbers[P]),
        OSSL_PARAM_ulong(OSSL_PKEY_PARAM_RSA_FACTOR2, &key_numbers[Q]),
        OSSL_PARAM_ulong(OSSL_PKEY_PARAM_RSA_EXPONENT1, &key_numbers[DP]),
        OSSL_PARAM_ulong(OSSL_PKEY_PARAM_RSA_EXPONENT2, &key_numbers[DQ]),
        OSSL_PARAM_ulong(OSSL_PKEY_PARAM_RSA_COEFFICIENT1, &key_numbers[QINV]),
        OSSL_PARAM_END
    };
    BIGNUM *bn = BN_new();
    BIGNUM *bn_from = BN_new();

    if (!TEST_ptr(ctx = EVP_PKEY_CTX_new_from_name(NULL, "RSA", NULL)))
        goto err;

    if (!TEST_int_eq(EVP_PKEY_fromdata_init(ctx), 1)
        || !TEST_int_eq(EVP_PKEY_fromdata(ctx, &pk, EVP_PKEY_KEYPAIR,
                                          fromdata_params), 1))
        goto err;

    while (dup_pk == NULL) {
        ret = 0;
        if (!TEST_int_eq(EVP_PKEY_get_bits(pk), 32)
            || !TEST_int_eq(EVP_PKEY_get_security_bits(pk), 8)
            || !TEST_int_eq(EVP_PKEY_get_size(pk), 4)
            || !TEST_false(EVP_PKEY_missing_parameters(pk)))
            goto err;

        EVP_PKEY_CTX_free(key_ctx);
        if (!TEST_ptr(key_ctx = EVP_PKEY_CTX_new_from_pkey(NULL, pk, "")))
            goto err;

        if (!TEST_int_gt(EVP_PKEY_check(key_ctx), 0)
            || !TEST_int_gt(EVP_PKEY_public_check(key_ctx), 0)
            || !TEST_int_gt(EVP_PKEY_private_check(key_ctx), 0)
            || !TEST_int_gt(EVP_PKEY_pairwise_check(key_ctx), 0))
            goto err;

        /* EVP_PKEY_copy_parameters() should fail for RSA */
        if (!TEST_ptr(copy_pk = EVP_PKEY_new())
            || !TEST_false(EVP_PKEY_copy_parameters(copy_pk, pk)))
            goto err;
        EVP_PKEY_free(copy_pk);
        copy_pk = NULL;

        ret = test_print_key_using_pem("RSA", pk)
              && test_print_key_using_encoder("RSA", pk);

        if (!ret || !TEST_ptr(dup_pk = EVP_PKEY_dup(pk)))
            goto err;
        ret = ret && TEST_int_eq(EVP_PKEY_eq(pk, dup_pk), 1);
        EVP_PKEY_free(pk);
        pk = dup_pk;
        if (!ret)
            goto err;
    }
 err:
    /* for better diagnostics always compare key params */
    for (i = 0; fromdata_params[i].key != NULL; ++i) {
        if (!TEST_true(BN_set_word(bn_from, key_numbers[i]))
            || !TEST_true(EVP_PKEY_get_bn_param(pk, fromdata_params[i].key, &bn))
            || !TEST_BN_eq(bn, bn_from))
            ret = 0;
    }
    BN_free(bn_from);
    BN_free(bn);
    EVP_PKEY_free(pk);
    EVP_PKEY_free(copy_pk);
    EVP_PKEY_CTX_free(key_ctx);
    EVP_PKEY_CTX_free(ctx);

    return ret;
}

static int test_evp_pkey_get_bn_param_large(void)
{
    int ret = 0;
    EVP_PKEY_CTX *ctx = NULL, *key_ctx = NULL;
    EVP_PKEY *pk = NULL;
    OSSL_PARAM_BLD *bld = NULL;
    OSSL_PARAM *fromdata_params = NULL;
    BIGNUM *n = NULL, *e = NULL, *d = NULL, *n_out = NULL;
    /*
     * The buffer size chosen here for n_data larger than the buffer used
     * internally in EVP_PKEY_get_bn_param.
     */
    static unsigned char n_data[2050];
    static const unsigned char e_data[] = {
        0x1, 0x00, 0x01
    };
    static const unsigned char d_data[]= {
       0x99, 0x33, 0x13, 0x7b
    };

    /* N is a large buffer */
    memset(n_data, 0xCE, sizeof(n_data));

    if (!TEST_ptr(bld = OSSL_PARAM_BLD_new())
        || !TEST_ptr(n = BN_bin2bn(n_data, sizeof(n_data), NULL))
        || !TEST_ptr(e = BN_bin2bn(e_data, sizeof(e_data), NULL))
        || !TEST_ptr(d = BN_bin2bn(d_data, sizeof(d_data), NULL))
        || !TEST_true(OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_N, n))
        || !TEST_true(OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_E, e))
        || !TEST_true(OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_D, d))
        || !TEST_ptr(fromdata_params = OSSL_PARAM_BLD_to_param(bld))
        || !TEST_ptr(ctx = EVP_PKEY_CTX_new_from_name(NULL, "RSA", NULL))
        || !TEST_int_eq(EVP_PKEY_fromdata_init(ctx), 1)
        || !TEST_int_eq(EVP_PKEY_fromdata(ctx, &pk, EVP_PKEY_KEYPAIR,
                                          fromdata_params), 1)
        || !TEST_ptr(key_ctx = EVP_PKEY_CTX_new_from_pkey(NULL, pk, ""))
        || !TEST_true(EVP_PKEY_get_bn_param(pk, OSSL_PKEY_PARAM_RSA_N, &n_out))
        || !TEST_BN_eq(n, n_out))
        goto err;
    ret = 1;
 err:
    BN_free(n_out);
    BN_free(n);
    BN_free(e);
    BN_free(d);
    EVP_PKEY_free(pk);
    EVP_PKEY_CTX_free(key_ctx);
    EVP_PKEY_CTX_free(ctx);
    OSSL_PARAM_free(fromdata_params);
    OSSL_PARAM_BLD_free(bld);
    return ret;
}


#ifndef OPENSSL_NO_DH
static int test_fromdata_dh_named_group(void)
{
    int ret = 0;
    int gindex = 0, pcounter = 0, hindex = 0;
    EVP_PKEY_CTX *ctx = NULL, *key_ctx = NULL;
    EVP_PKEY *pk = NULL, *copy_pk = NULL, *dup_pk = NULL;
    size_t len;
    BIGNUM *pub = NULL, *priv = NULL;
    BIGNUM *pub_out = NULL, *priv_out = NULL;
    BIGNUM *p = NULL, *q = NULL, *g = NULL, *j = NULL;
    OSSL_PARAM *fromdata_params = NULL;
    OSSL_PARAM_BLD *bld = NULL;
    char name_out[80];
    unsigned char seed_out[32];

    /*
     * DH key data was generated using the following:
     * openssl genpkey -algorithm DH -pkeyopt group:ffdhe2048
     *                 -pkeyopt priv_len:224 -text
     */
    static const unsigned char priv_data[] = {
        0x88, 0x85, 0xe7, 0x9f, 0xee, 0x6d, 0xc5, 0x7c, 0x78, 0xaf, 0x63, 0x5d,
        0x38, 0x2a, 0xd0, 0xed, 0x56, 0x4b, 0x47, 0x21, 0x2b, 0xfa, 0x55, 0xfa,
        0x87, 0xe8, 0xa9, 0x7b,
    };
    static const unsigned char pub_data[] = {
        0x00, 0xd6, 0x2d, 0x77, 0xe0, 0xd3, 0x7d, 0xf8, 0xeb, 0x98, 0x50, 0xa1,
        0x82, 0x22, 0x65, 0xd5, 0xd9, 0xfe, 0xc9, 0x3f, 0xbe, 0x16, 0x83, 0xbd,
        0x33, 0xe9, 0xc6, 0x93, 0xcf, 0x08, 0xaf, 0x83, 0xfa, 0x80, 0x8a, 0x6c,
        0x64, 0xdf, 0x70, 0x64, 0xd5, 0x0a, 0x7c, 0x5a, 0x72, 0xda, 0x66, 0xe6,
        0xf9, 0xf5, 0x31, 0x21, 0x92, 0xb0, 0x60, 0x1a, 0xb5, 0xd3, 0xf0, 0xa5,
        0xfa, 0x48, 0x95, 0x2e, 0x38, 0xd9, 0xc5, 0xe6, 0xda, 0xfb, 0x6c, 0x03,
        0x9d, 0x4b, 0x69, 0xb7, 0x95, 0xe4, 0x5c, 0xc0, 0x93, 0x4f, 0x48, 0xd9,
        0x7e, 0x06, 0x22, 0xb2, 0xde, 0xf3, 0x79, 0x24, 0xed, 0xe1, 0xd1, 0x4a,
        0x57, 0xf1, 0x40, 0x86, 0x70, 0x42, 0x25, 0xc5, 0x27, 0x68, 0xc9, 0xfa,
        0xe5, 0x8e, 0x62, 0x7e, 0xff, 0x49, 0x6c, 0x5b, 0xb5, 0xba, 0xf9, 0xef,
        0x9a, 0x1a, 0x10, 0xd4, 0x81, 0x53, 0xcf, 0x83, 0x04, 0x18, 0x1c, 0xe1,
        0xdb, 0xe1, 0x65, 0xa9, 0x7f, 0xe1, 0x33, 0xeb, 0xc3, 0x4f, 0xe3, 0xb7,
        0x22, 0xf7, 0x1c, 0x09, 0x4f, 0xed, 0xc6, 0x07, 0x8e, 0x78, 0x05, 0x8f,
        0x7c, 0x96, 0xd9, 0x12, 0xe0, 0x81, 0x74, 0x1a, 0xe9, 0x13, 0xc0, 0x20,
        0x82, 0x65, 0xbb, 0x42, 0x3b, 0xed, 0x08, 0x6a, 0x84, 0x4f, 0xea, 0x77,
        0x14, 0x32, 0xf9, 0xed, 0xc2, 0x12, 0xd6, 0xc5, 0xc6, 0xb3, 0xe5, 0xf2,
        0x6e, 0xf6, 0x16, 0x7f, 0x37, 0xde, 0xbc, 0x09, 0xc7, 0x06, 0x6b, 0x12,
        0xbc, 0xad, 0x2d, 0x49, 0x25, 0xd5, 0xdc, 0xf4, 0x18, 0x14, 0xd2, 0xf0,
        0xf1, 0x1d, 0x1f, 0x3a, 0xaa, 0x15, 0x55, 0xbb, 0x0d, 0x7f, 0xbe, 0x67,
        0xa1, 0xa7, 0xf0, 0xaa, 0xb3, 0xfb, 0x41, 0x82, 0x39, 0x49, 0x93, 0xbc,
        0xa8, 0xee, 0x72, 0x13, 0x45, 0x65, 0x15, 0x42, 0x17, 0xaa, 0xd8, 0xab,
        0xcf, 0x33, 0x42, 0x83, 0x42
    };
    static const char group_name[] = "ffdhe2048";
    static const long priv_len = 224;

    if (!TEST_ptr(bld = OSSL_PARAM_BLD_new())
        || !TEST_ptr(pub = BN_bin2bn(pub_data, sizeof(pub_data), NULL))
        || !TEST_ptr(priv = BN_bin2bn(priv_data, sizeof(priv_data), NULL))
        || !TEST_true(OSSL_PARAM_BLD_push_utf8_string(bld,
                                                      OSSL_PKEY_PARAM_GROUP_NAME,
                                                      group_name, 0))
        || !TEST_true(OSSL_PARAM_BLD_push_long(bld, OSSL_PKEY_PARAM_DH_PRIV_LEN,
                                               priv_len))
        || !TEST_true(OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_PUB_KEY, pub))
        || !TEST_true(OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_PRIV_KEY, priv))
        || !TEST_ptr(fromdata_params = OSSL_PARAM_BLD_to_param(bld)))
        goto err;

    if (!TEST_ptr(ctx = EVP_PKEY_CTX_new_from_name(NULL, "DH", NULL)))
        goto err;

    if (!TEST_int_eq(EVP_PKEY_fromdata_init(ctx), 1)
        || !TEST_int_eq(EVP_PKEY_fromdata(ctx, &pk, EVP_PKEY_KEYPAIR,
                                          fromdata_params), 1))
        goto err;

    /*
     * A few extra checks of EVP_PKEY_get_utf8_string_param() to see that
     * it behaves as expected with regards to string length and terminating
     * NUL byte.
     */
    if (!TEST_true(EVP_PKEY_get_utf8_string_param(pk,
                                                  OSSL_PKEY_PARAM_GROUP_NAME,
                                                  NULL, sizeof(name_out),
                                                  &len))
        || !TEST_size_t_eq(len, sizeof(group_name) - 1)
        /* Just enough space to hold the group name and a terminating NUL */
        || !TEST_true(EVP_PKEY_get_utf8_string_param(pk,
                                                     OSSL_PKEY_PARAM_GROUP_NAME,
                                                     name_out,
                                                     sizeof(group_name),
                                                     &len))
        || !TEST_size_t_eq(len, sizeof(group_name) - 1)
        /* Too small buffer to hold the terminating NUL byte */
        || !TEST_false(EVP_PKEY_get_utf8_string_param(pk,
                                                      OSSL_PKEY_PARAM_GROUP_NAME,
                                                      name_out,
                                                      sizeof(group_name) - 1,
                                                      &len))
        /* Too small buffer to hold the whole group name, even! */
        || !TEST_false(EVP_PKEY_get_utf8_string_param(pk,
                                                      OSSL_PKEY_PARAM_GROUP_NAME,
                                                      name_out,
                                                      sizeof(group_name) - 2,
                                                      &len)))
        goto err;

    while (dup_pk == NULL) {
        ret = 0;
        if (!TEST_int_eq(EVP_PKEY_get_bits(pk), 2048)
            || !TEST_int_eq(EVP_PKEY_get_security_bits(pk), 112)
            || !TEST_int_eq(EVP_PKEY_get_size(pk), 256)
            || !TEST_false(EVP_PKEY_missing_parameters(pk)))
            goto err;

        if (!TEST_true(EVP_PKEY_get_utf8_string_param(pk,
                                                      OSSL_PKEY_PARAM_GROUP_NAME,
                                                      name_out,
                                                      sizeof(name_out),
                                                      &len))
            || !TEST_str_eq(name_out, group_name)
            || !TEST_true(EVP_PKEY_get_bn_param(pk, OSSL_PKEY_PARAM_PUB_KEY,
                                                &pub_out))

            || !TEST_BN_eq(pub, pub_out)
            || !TEST_true(EVP_PKEY_get_bn_param(pk, OSSL_PKEY_PARAM_PRIV_KEY,
                                                &priv_out))
            || !TEST_BN_eq(priv, priv_out)
            || !TEST_true(EVP_PKEY_get_bn_param(pk, OSSL_PKEY_PARAM_FFC_P, &p))
            || !TEST_BN_eq(&ossl_bignum_ffdhe2048_p, p)
            || !TEST_true(EVP_PKEY_get_bn_param(pk, OSSL_PKEY_PARAM_FFC_Q, &q))
            || !TEST_ptr(q)
            || !TEST_true(EVP_PKEY_get_bn_param(pk, OSSL_PKEY_PARAM_FFC_G, &g))
            || !TEST_BN_eq(&ossl_bignum_const_2, g)
            || !TEST_false(EVP_PKEY_get_bn_param(pk,
                                                 OSSL_PKEY_PARAM_FFC_COFACTOR,
                                                 &j))
            || !TEST_ptr_null(j)
            || !TEST_false(EVP_PKEY_get_octet_string_param(pk,
                                                           OSSL_PKEY_PARAM_FFC_SEED,
                                                           seed_out,
                                                           sizeof(seed_out),
                                                           &len))
            || !TEST_true(EVP_PKEY_get_int_param(pk, OSSL_PKEY_PARAM_FFC_GINDEX,
                                                 &gindex))
            || !TEST_int_eq(gindex, -1)
            || !TEST_true(EVP_PKEY_get_int_param(pk, OSSL_PKEY_PARAM_FFC_H,
                                                 &hindex))
            || !TEST_int_eq(hindex, 0)
            || !TEST_true(EVP_PKEY_get_int_param(pk,
                                                 OSSL_PKEY_PARAM_FFC_PCOUNTER,
                                                 &pcounter))
            || !TEST_int_eq(pcounter, -1))
            goto err;
        BN_free(p);
        p = NULL;
        BN_free(q);
        q = NULL;
        BN_free(g);
        g = NULL;
        BN_free(j);
        j = NULL;
        BN_free(pub_out);
        pub_out = NULL;
        BN_free(priv_out);
        priv_out = NULL;

        if (!TEST_ptr(key_ctx = EVP_PKEY_CTX_new_from_pkey(NULL, pk, "")))
            goto err;

        if (!TEST_int_gt(EVP_PKEY_check(key_ctx), 0)
            || !TEST_int_gt(EVP_PKEY_public_check(key_ctx), 0)
            || !TEST_int_gt(EVP_PKEY_private_check(key_ctx), 0)
            || !TEST_int_gt(EVP_PKEY_pairwise_check(key_ctx), 0))
            goto err;
        EVP_PKEY_CTX_free(key_ctx);
        key_ctx = NULL;

        if (!TEST_ptr(copy_pk = EVP_PKEY_new())
            || !TEST_true(EVP_PKEY_copy_parameters(copy_pk, pk)))
            goto err;
        EVP_PKEY_free(copy_pk);
        copy_pk = NULL;

        ret = test_print_key_using_pem("DH", pk)
              && test_print_key_using_encoder("DH", pk);

        if (!ret || !TEST_ptr(dup_pk = EVP_PKEY_dup(pk)))
            goto err;
        ret = ret && TEST_int_eq(EVP_PKEY_eq(pk, dup_pk), 1);
        EVP_PKEY_free(pk);
        pk = dup_pk;
        if (!ret)
            goto err;
    }
err:
    BN_free(p);
    BN_free(q);
    BN_free(g);
    BN_free(j);
    BN_free(pub);
    BN_free(priv);
    BN_free(pub_out);
    BN_free(priv_out);
    EVP_PKEY_free(copy_pk);
    EVP_PKEY_free(pk);
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_CTX_free(key_ctx);
    OSSL_PARAM_free(fromdata_params);
    OSSL_PARAM_BLD_free(bld);

    return ret;
}

static int test_fromdata_dh_fips186_4(void)
{
    int ret = 0;
    int gindex = 0, pcounter = 0, hindex = 0;
    EVP_PKEY_CTX *ctx = NULL, *key_ctx = NULL;
    EVP_PKEY *pk = NULL, *dup_pk = NULL;
    size_t len;
    BIGNUM *pub = NULL, *priv = NULL;
    BIGNUM *pub_out = NULL, *priv_out = NULL;
    BIGNUM *p = NULL, *q = NULL, *g = NULL, *j = NULL;
    OSSL_PARAM_BLD *bld = NULL;
    OSSL_PARAM *fromdata_params = NULL;
    char name_out[80];
    unsigned char seed_out[32];

    /*
     * DH key data was generated using the following:
     * openssl genpkey -algorithm DH
     *                 -pkeyopt group:ffdhe2048 -pkeyopt priv_len:224 -text
     */
    static const unsigned char priv_data[] = {
       0x88, 0x85, 0xe7, 0x9f, 0xee, 0x6d, 0xc5, 0x7c, 0x78, 0xaf, 0x63, 0x5d,
       0x38, 0x2a, 0xd0, 0xed, 0x56, 0x4b, 0x47, 0x21, 0x2b, 0xfa, 0x55, 0xfa,
       0x87, 0xe8, 0xa9, 0x7b,
    };
    static const unsigned char pub_data[] = {
       0xd6, 0x2d, 0x77, 0xe0, 0xd3, 0x7d, 0xf8, 0xeb, 0x98, 0x50, 0xa1, 0x82,
       0x22, 0x65, 0xd5, 0xd9, 0xfe, 0xc9, 0x3f, 0xbe, 0x16, 0x83, 0xbd, 0x33,
       0xe9, 0xc6, 0x93, 0xcf, 0x08, 0xaf, 0x83, 0xfa, 0x80, 0x8a, 0x6c, 0x64,
       0xdf, 0x70, 0x64, 0xd5, 0x0a, 0x7c, 0x5a, 0x72, 0xda, 0x66, 0xe6, 0xf9,
       0xf5, 0x31, 0x21, 0x92, 0xb0, 0x60, 0x1a, 0xb5, 0xd3, 0xf0, 0xa5, 0xfa,
       0x48, 0x95, 0x2e, 0x38, 0xd9, 0xc5, 0xe6, 0xda, 0xfb, 0x6c, 0x03, 0x9d,
       0x4b, 0x69, 0xb7, 0x95, 0xe4, 0x5c, 0xc0, 0x93, 0x4f, 0x48, 0xd9, 0x7e,
       0x06, 0x22, 0xb2, 0xde, 0xf3, 0x79, 0x24, 0xed, 0xe1, 0xd1, 0x4a, 0x57,
       0xf1, 0x40, 0x86, 0x70, 0x42, 0x25, 0xc5, 0x27, 0x68, 0xc9, 0xfa, 0xe5,
       0x8e, 0x62, 0x7e, 0xff, 0x49, 0x6c, 0x5b, 0xb5, 0xba, 0xf9, 0xef, 0x9a,
       0x1a, 0x10, 0xd4, 0x81, 0x53, 0xcf, 0x83, 0x04, 0x18, 0x1c, 0xe1, 0xdb,
       0xe1, 0x65, 0xa9, 0x7f, 0xe1, 0x33, 0xeb, 0xc3, 0x4f, 0xe3, 0xb7, 0x22,
       0xf7, 0x1c, 0x09, 0x4f, 0xed, 0xc6, 0x07, 0x8e, 0x78, 0x05, 0x8f, 0x7c,
       0x96, 0xd9, 0x12, 0xe0, 0x81, 0x74, 0x1a, 0xe9, 0x13, 0xc0, 0x20, 0x82,
       0x65, 0xbb, 0x42, 0x3b, 0xed, 0x08, 0x6a, 0x84, 0x4f, 0xea, 0x77, 0x14,
       0x32, 0xf9, 0xed, 0xc2, 0x12, 0xd6, 0xc5, 0xc6, 0xb3, 0xe5, 0xf2, 0x6e,
       0xf6, 0x16, 0x7f, 0x37, 0xde, 0xbc, 0x09, 0xc7, 0x06, 0x6b, 0x12, 0xbc,
       0xad, 0x2d, 0x49, 0x25, 0xd5, 0xdc, 0xf4, 0x18, 0x14, 0xd2, 0xf0, 0xf1,
       0x1d, 0x1f, 0x3a, 0xaa, 0x15, 0x55, 0xbb, 0x0d, 0x7f, 0xbe, 0x67, 0xa1,
       0xa7, 0xf0, 0xaa, 0xb3, 0xfb, 0x41, 0x82, 0x39, 0x49, 0x93, 0xbc, 0xa8,
       0xee, 0x72, 0x13, 0x45, 0x65, 0x15, 0x42, 0x17, 0xaa, 0xd8, 0xab, 0xcf,
       0x33, 0x42, 0x83, 0x42
    };
    static const char group_name[] = "ffdhe2048";
    static const long priv_len = 224;


    if (!TEST_ptr(bld = OSSL_PARAM_BLD_new())
        || !TEST_ptr(pub = BN_bin2bn(pub_data, sizeof(pub_data), NULL))
        || !TEST_ptr(priv = BN_bin2bn(priv_data, sizeof(priv_data), NULL))
        || !TEST_true(OSSL_PARAM_BLD_push_utf8_string(bld,
                                                      OSSL_PKEY_PARAM_GROUP_NAME,
                                                      group_name, 0))
        || !TEST_true(OSSL_PARAM_BLD_push_long(bld, OSSL_PKEY_PARAM_DH_PRIV_LEN,
                                               priv_len))
        || !TEST_true(OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_PUB_KEY, pub))
        || !TEST_true(OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_PRIV_KEY, priv))
        || !TEST_ptr(fromdata_params = OSSL_PARAM_BLD_to_param(bld)))
        goto err;

    if (!TEST_ptr(ctx = EVP_PKEY_CTX_new_from_name(NULL, "DH", NULL)))
        goto err;

    if (!TEST_int_eq(EVP_PKEY_fromdata_init(ctx), 1)
        || !TEST_int_eq(EVP_PKEY_fromdata(ctx, &pk, EVP_PKEY_KEYPAIR,
                                          fromdata_params), 1))
        goto err;

    while (dup_pk == NULL) {
        ret = 0;
        if (!TEST_int_eq(EVP_PKEY_get_bits(pk), 2048)
            || !TEST_int_eq(EVP_PKEY_get_security_bits(pk), 112)
            || !TEST_int_eq(EVP_PKEY_get_size(pk), 256)
            || !TEST_false(EVP_PKEY_missing_parameters(pk)))
            goto err;

        if (!TEST_true(EVP_PKEY_get_utf8_string_param(pk,
                                                      OSSL_PKEY_PARAM_GROUP_NAME,
                                                      name_out,
                                                      sizeof(name_out),
                                                      &len))
            || !TEST_str_eq(name_out, group_name)
            || !TEST_true(EVP_PKEY_get_bn_param(pk, OSSL_PKEY_PARAM_PUB_KEY,
                                                &pub_out))
            || !TEST_BN_eq(pub, pub_out)
            || !TEST_true(EVP_PKEY_get_bn_param(pk, OSSL_PKEY_PARAM_PRIV_KEY,
                                                &priv_out))
            || !TEST_BN_eq(priv, priv_out)
            || !TEST_true(EVP_PKEY_get_bn_param(pk, OSSL_PKEY_PARAM_FFC_P, &p))
            || !TEST_BN_eq(&ossl_bignum_ffdhe2048_p, p)
            || !TEST_true(EVP_PKEY_get_bn_param(pk, OSSL_PKEY_PARAM_FFC_Q, &q))
            || !TEST_ptr(q)
            || !TEST_true(EVP_PKEY_get_bn_param(pk, OSSL_PKEY_PARAM_FFC_G, &g))
            || !TEST_BN_eq(&ossl_bignum_const_2, g)
            || !TEST_false(EVP_PKEY_get_bn_param(pk,
                                                 OSSL_PKEY_PARAM_FFC_COFACTOR,
                                                 &j))
            || !TEST_ptr_null(j)
            || !TEST_false(EVP_PKEY_get_octet_string_param(pk,
                                                           OSSL_PKEY_PARAM_FFC_SEED,
                                                           seed_out,
                                                           sizeof(seed_out),
                                                           &len))
            || !TEST_true(EVP_PKEY_get_int_param(pk,
                                                 OSSL_PKEY_PARAM_FFC_GINDEX,
                                                 &gindex))
            || !TEST_int_eq(gindex, -1)
            || !TEST_true(EVP_PKEY_get_int_param(pk, OSSL_PKEY_PARAM_FFC_H,
                                                 &hindex))
            || !TEST_int_eq(hindex, 0)
            || !TEST_true(EVP_PKEY_get_int_param(pk,
                                                 OSSL_PKEY_PARAM_FFC_PCOUNTER,
                                                 &pcounter))
            || !TEST_int_eq(pcounter, -1))
            goto err;
        BN_free(p);
        p = NULL;
        BN_free(q);
        q = NULL;
        BN_free(g);
        g = NULL;
        BN_free(j);
        j = NULL;
        BN_free(pub_out);
        pub_out = NULL;
        BN_free(priv_out);
        priv_out = NULL;

        if (!TEST_ptr(key_ctx = EVP_PKEY_CTX_new_from_pkey(NULL, pk, "")))
            goto err;

        if (!TEST_int_gt(EVP_PKEY_check(key_ctx), 0)
            || !TEST_int_gt(EVP_PKEY_public_check(key_ctx), 0)
            || !TEST_int_gt(EVP_PKEY_private_check(key_ctx), 0)
            || !TEST_int_gt(EVP_PKEY_pairwise_check(key_ctx), 0))
            goto err;
        EVP_PKEY_CTX_free(key_ctx);
        key_ctx = NULL;

        ret = test_print_key_using_pem("DH", pk)
              && test_print_key_using_encoder("DH", pk);

        if (!ret || !TEST_ptr(dup_pk = EVP_PKEY_dup(pk)))
            goto err;
        ret = ret && TEST_int_eq(EVP_PKEY_eq(pk, dup_pk), 1);
        EVP_PKEY_free(pk);
        pk = dup_pk;
        if (!ret)
            goto err;
    }
err:
    BN_free(p);
    BN_free(q);
    BN_free(g);
    BN_free(j);
    BN_free(pub);
    BN_free(priv);
    BN_free(pub_out);
    BN_free(priv_out);
    EVP_PKEY_free(pk);
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_CTX_free(key_ctx);
    OSSL_PARAM_free(fromdata_params);
    OSSL_PARAM_BLD_free(bld);

    return ret;
}

#endif



#ifndef OPENSSL_NO_EC
/* Array indexes used in test_fromdata_ecx */
# define PRIV_KEY        0
# define PUB_KEY         1

# define X25519_IDX      0
# define X448_IDX        1
# define ED25519_IDX     2
# define ED448_IDX       3

/*
 * tst uses indexes 0 ... (3 * 4 - 1)
 * For the 4 ECX key types (X25519_IDX..ED448_IDX)
 * 0..3  = public + private key.
 * 4..7  = private key (This will generate the public key from the private key)
 * 8..11 = public key
 */
static int test_fromdata_ecx(int tst)
{
    int ret = 0;
    EVP_PKEY_CTX *ctx = NULL, *ctx2 = NULL;
    EVP_PKEY *pk = NULL, *copy_pk = NULL, *dup_pk = NULL;
    const char *alg = NULL;
    size_t len;
    unsigned char out_pub[ED448_KEYLEN];
    unsigned char out_priv[ED448_KEYLEN];
    OSSL_PARAM params[3] = { OSSL_PARAM_END, OSSL_PARAM_END, OSSL_PARAM_END };

    /* ED448_KEYLEN > X448_KEYLEN > X25519_KEYLEN == ED25519_KEYLEN */
    static unsigned char key_numbers[4][2][ED448_KEYLEN] = {
        /* X25519: Keys from RFC 7748 6.1 */
        {
            /* Private Key */
            {
                0x77, 0x07, 0x6d, 0x0a, 0x73, 0x18, 0xa5, 0x7d, 0x3c, 0x16,
                0xc1, 0x72, 0x51, 0xb2, 0x66, 0x45, 0xdf, 0x4c, 0x2f, 0x87,
                0xeb, 0xc0, 0x99, 0x2a, 0xb1, 0x77, 0xfb, 0xa5, 0x1d, 0xb9,
                0x2c, 0x2a
            },
            /* Public Key */
            {
                0x85, 0x20, 0xf0, 0x09, 0x89, 0x30, 0xa7, 0x54, 0x74, 0x8b,
                0x7d, 0xdc, 0xb4, 0x3e, 0xf7, 0x5a, 0x0d, 0xbf, 0x3a, 0x0d,
                0x26, 0x38, 0x1a, 0xf4, 0xeb, 0xa4, 0xa9, 0x8e, 0xaa, 0x9b,
                0x4e, 0x6a
            }
        },
        /* X448: Keys from RFC 7748 6.2 */
        {
            /* Private Key */
            {
                0x9a, 0x8f, 0x49, 0x25, 0xd1, 0x51, 0x9f, 0x57, 0x75, 0xcf,
                0x46, 0xb0, 0x4b, 0x58, 0x00, 0xd4, 0xee, 0x9e, 0xe8, 0xba,
                0xe8, 0xbc, 0x55, 0x65, 0xd4, 0x98, 0xc2, 0x8d, 0xd9, 0xc9,
                0xba, 0xf5, 0x74, 0xa9, 0x41, 0x97, 0x44, 0x89, 0x73, 0x91,
                0x00, 0x63, 0x82, 0xa6, 0xf1, 0x27, 0xab, 0x1d, 0x9a, 0xc2,
                0xd8, 0xc0, 0xa5, 0x98, 0x72, 0x6b
            },
            /* Public Key */
            {
                0x9b, 0x08, 0xf7, 0xcc, 0x31, 0xb7, 0xe3, 0xe6, 0x7d, 0x22,
                0xd5, 0xae, 0xa1, 0x21, 0x07, 0x4a, 0x27, 0x3b, 0xd2, 0xb8,
                0x3d, 0xe0, 0x9c, 0x63, 0xfa, 0xa7, 0x3d, 0x2c, 0x22, 0xc5,
                0xd9, 0xbb, 0xc8, 0x36, 0x64, 0x72, 0x41, 0xd9, 0x53, 0xd4,
                0x0c, 0x5b, 0x12, 0xda, 0x88, 0x12, 0x0d, 0x53, 0x17, 0x7f,
                0x80, 0xe5, 0x32, 0xc4, 0x1f, 0xa0
            }
        },
        /* ED25519: Keys from RFC 8032 */
        {
            /* Private Key */
            {
                0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84,
                0x4a, 0xf4, 0x92, 0xec, 0x2c, 0xc4, 0x44, 0x49, 0xc5, 0x69,
                0x7b, 0x32, 0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae,
                0x7f, 0x60
            },
            /* Public Key */
            {
                0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b,
                0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3,
                0xda, 0xa6, 0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07,
                0x51, 0x1a
            }
        },
        /* ED448: Keys from RFC 8032 */
        {
            /* Private Key */
            {
                0x6c, 0x82, 0xa5, 0x62, 0xcb, 0x80, 0x8d, 0x10, 0xd6, 0x32,
                0xbe, 0x89, 0xc8, 0x51, 0x3e, 0xbf, 0x6c, 0x92, 0x9f, 0x34,
                0xdd, 0xfa, 0x8c, 0x9f, 0x63, 0xc9, 0x96, 0x0e, 0xf6, 0xe3,
                0x48, 0xa3, 0x52, 0x8c, 0x8a, 0x3f, 0xcc, 0x2f, 0x04, 0x4e,
                0x39, 0xa3, 0xfc, 0x5b, 0x94, 0x49, 0x2f, 0x8f, 0x03, 0x2e,
                0x75, 0x49, 0xa2, 0x00, 0x98, 0xf9, 0x5b
            },
            /* Public Key */
            {
                0x5f, 0xd7, 0x44, 0x9b, 0x59, 0xb4, 0x61, 0xfd, 0x2c, 0xe7,
                0x87, 0xec, 0x61, 0x6a, 0xd4, 0x6a, 0x1d, 0xa1, 0x34, 0x24,
                0x85, 0xa7, 0x0e, 0x1f, 0x8a, 0x0e, 0xa7, 0x5d, 0x80, 0xe9,
                0x67, 0x78, 0xed, 0xf1, 0x24, 0x76, 0x9b, 0x46, 0xc7, 0x06,
                0x1b, 0xd6, 0x78, 0x3d, 0xf1, 0xe5, 0x0f, 0x6c, 0xd1, 0xfa,
                0x1a, 0xbe, 0xaf, 0xe8, 0x25, 0x61, 0x80
            }
        }
    };
    OSSL_PARAM x25519_fromdata_params[] = {
        OSSL_PARAM_octet_string(OSSL_PKEY_PARAM_PRIV_KEY,
                                key_numbers[X25519_IDX][PRIV_KEY],
                                X25519_KEYLEN),
        OSSL_PARAM_octet_string(OSSL_PKEY_PARAM_PUB_KEY,
                                key_numbers[X25519_IDX][PUB_KEY],
                                X25519_KEYLEN),
        OSSL_PARAM_END
    };
    OSSL_PARAM x448_fromdata_params[] = {
        OSSL_PARAM_octet_string(OSSL_PKEY_PARAM_PRIV_KEY,
                                key_numbers[X448_IDX][PRIV_KEY],
                                X448_KEYLEN),
        OSSL_PARAM_octet_string(OSSL_PKEY_PARAM_PUB_KEY,
                                key_numbers[X448_IDX][PUB_KEY],
                                X448_KEYLEN),
        OSSL_PARAM_END
    };
    OSSL_PARAM ed25519_fromdata_params[] = {
        OSSL_PARAM_octet_string(OSSL_PKEY_PARAM_PRIV_KEY,
                                key_numbers[ED25519_IDX][PRIV_KEY],
                                ED25519_KEYLEN),
        OSSL_PARAM_octet_string(OSSL_PKEY_PARAM_PUB_KEY,
                                key_numbers[ED25519_IDX][PUB_KEY],
                                ED25519_KEYLEN),
        OSSL_PARAM_END
    };
    OSSL_PARAM ed448_fromdata_params[] = {
        OSSL_PARAM_octet_string(OSSL_PKEY_PARAM_PRIV_KEY,
                                key_numbers[ED448_IDX][PRIV_KEY],
                                ED448_KEYLEN),
        OSSL_PARAM_octet_string(OSSL_PKEY_PARAM_PUB_KEY,
                                key_numbers[ED448_IDX][PUB_KEY],
                                ED448_KEYLEN),
        OSSL_PARAM_END
    };
    OSSL_PARAM *fromdata_params = NULL;
    int bits = 0, security_bits = 0, size = 0;
    OSSL_PARAM *orig_fromdata_params = NULL;

    switch (tst & 3) {
    case X25519_IDX:
        fromdata_params = x25519_fromdata_params;
        bits = X25519_BITS;
        security_bits = X25519_SECURITY_BITS;
        size = X25519_KEYLEN;
        alg = "X25519";
        break;

    case X448_IDX:
        fromdata_params = x448_fromdata_params;
        bits = X448_BITS;
        security_bits = X448_SECURITY_BITS;
        size = X448_KEYLEN;
        alg = "X448";
        break;

    case ED25519_IDX:
        fromdata_params = ed25519_fromdata_params;
        bits = ED25519_BITS;
        security_bits = ED25519_SECURITY_BITS;
        size = ED25519_SIGSIZE;
        alg = "ED25519";
        break;

    case ED448_IDX:
        fromdata_params = ed448_fromdata_params;
        bits = ED448_BITS;
        security_bits = ED448_SECURITY_BITS;
        size = ED448_SIGSIZE;
        alg = "ED448";
        break;
    default:
        goto err;
    }

    ctx = EVP_PKEY_CTX_new_from_name(NULL, alg, NULL);
    if (!TEST_ptr(ctx))
        goto err;

    orig_fromdata_params = fromdata_params;
    if (tst > 7) {
        /* public key only */
        fromdata_params++;
    } else if (tst > 3) {
        /* private key only */
        params[0] = fromdata_params[0];
        params[1] = fromdata_params[2];
        fromdata_params = params;
    }

    if (!TEST_int_eq(EVP_PKEY_fromdata_init(ctx), 1)
        || !TEST_int_eq(EVP_PKEY_fromdata(ctx, &pk, EVP_PKEY_KEYPAIR,
                                          fromdata_params), 1))
        goto err;

    while (dup_pk == NULL) {
        ret = 0;
        if (!TEST_int_eq(EVP_PKEY_get_bits(pk), bits)
            || !TEST_int_eq(EVP_PKEY_get_security_bits(pk), security_bits)
            || !TEST_int_eq(EVP_PKEY_get_size(pk), size)
            || !TEST_false(EVP_PKEY_missing_parameters(pk)))
            goto err;

        if (!TEST_ptr(ctx2 = EVP_PKEY_CTX_new_from_pkey(NULL, pk, NULL)))
            goto err;
        if (tst <= 7) {
            if (!TEST_int_gt(EVP_PKEY_check(ctx2), 0))
                goto err;
            if (!TEST_true(EVP_PKEY_get_octet_string_param(
                               pk, orig_fromdata_params[PRIV_KEY].key,
                               out_priv, sizeof(out_priv), &len))
                || !TEST_mem_eq(out_priv, len,
                                orig_fromdata_params[PRIV_KEY].data,
                                orig_fromdata_params[PRIV_KEY].data_size)
                || !TEST_true(EVP_PKEY_get_octet_string_param(
                                  pk, orig_fromdata_params[PUB_KEY].key,
                                  out_pub, sizeof(out_pub), &len))
                || !TEST_mem_eq(out_pub, len,
                                orig_fromdata_params[PUB_KEY].data,
                                orig_fromdata_params[PUB_KEY].data_size))
                goto err;
        } else {
            /* The private key check should fail if there is only a public key */
            if (!TEST_int_gt(EVP_PKEY_public_check(ctx2), 0)
                || !TEST_int_le(EVP_PKEY_private_check(ctx2), 0)
                || !TEST_int_le(EVP_PKEY_check(ctx2), 0))
                goto err;
        }
        EVP_PKEY_CTX_free(ctx2);
        ctx2 = NULL;

        if (!TEST_ptr(copy_pk = EVP_PKEY_new())
               /* This should succeed because there are no parameters to copy */
            || !TEST_true(EVP_PKEY_copy_parameters(copy_pk, pk)))
            goto err;
        if (!TEST_ptr(ctx2 = EVP_PKEY_CTX_new_from_pkey(NULL, copy_pk, NULL))
               /* This should fail because copy_pk has no pubkey */
            || !TEST_int_le(EVP_PKEY_public_check(ctx2), 0))
            goto err;
        EVP_PKEY_CTX_free(ctx2);
        ctx2 = NULL;
        EVP_PKEY_free(copy_pk);
        copy_pk = NULL;

        if (tst > 7)
            ret = test_print_key_using_encoder_public(alg, pk);
        else
            ret = test_print_key_using_pem(alg, pk)
                  && test_print_key_using_encoder(alg, pk);

        if (!ret || !TEST_ptr(dup_pk = EVP_PKEY_dup(pk)))
            goto err;
        ret = ret && TEST_int_eq(EVP_PKEY_eq(pk, dup_pk), 1);
        EVP_PKEY_free(pk);
        pk = dup_pk;
        if (!ret)
            goto err;
    }

err:
    EVP_PKEY_free(pk);
    EVP_PKEY_free(copy_pk);
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_CTX_free(ctx2);

    return ret;
}

static int test_fromdata_ec(void)
{
    int ret = 0;
    EVP_PKEY_CTX *ctx = NULL;
    EVP_PKEY *pk = NULL, *copy_pk = NULL, *dup_pk = NULL;
    OSSL_PARAM_BLD *bld = NULL;
    BIGNUM *ec_priv_bn = NULL;
    BIGNUM *bn_priv = NULL;
    OSSL_PARAM *fromdata_params = NULL;
    const char *alg = "EC";
    const char *curve = "prime256v1";
    const char bad_curve[] = "nonexistent-curve";
    OSSL_PARAM nokey_params[2] = {
       OSSL_PARAM_END,
       OSSL_PARAM_END
    };
    /* UNCOMPRESSED FORMAT */
    static const unsigned char ec_pub_keydata[] = {
       POINT_CONVERSION_UNCOMPRESSED,
       0x1b, 0x93, 0x67, 0x55, 0x1c, 0x55, 0x9f, 0x63,
       0xd1, 0x22, 0xa4, 0xd8, 0xd1, 0x0a, 0x60, 0x6d,
       0x02, 0xa5, 0x77, 0x57, 0xc8, 0xa3, 0x47, 0x73,
       0x3a, 0x6a, 0x08, 0x28, 0x39, 0xbd, 0xc9, 0xd2,
       0x80, 0xec, 0xe9, 0xa7, 0x08, 0x29, 0x71, 0x2f,
       0xc9, 0x56, 0x82, 0xee, 0x9a, 0x85, 0x0f, 0x6d,
       0x7f, 0x59, 0x5f, 0x8c, 0xd1, 0x96, 0x0b, 0xdf,
       0x29, 0x3e, 0x49, 0x07, 0x88, 0x3f, 0x9a, 0x29
    };
    /* SAME BUT COMPRESSED FORMAT */
    static const unsigned char ec_pub_keydata_compressed[] = {
       POINT_CONVERSION_COMPRESSED+1,
       0x1b, 0x93, 0x67, 0x55, 0x1c, 0x55, 0x9f, 0x63,
       0xd1, 0x22, 0xa4, 0xd8, 0xd1, 0x0a, 0x60, 0x6d,
       0x02, 0xa5, 0x77, 0x57, 0xc8, 0xa3, 0x47, 0x73,
       0x3a, 0x6a, 0x08, 0x28, 0x39, 0xbd, 0xc9, 0xd2
    };
    static const unsigned char ec_priv_keydata[] = {
        0x33, 0xd0, 0x43, 0x83, 0xa9, 0x89, 0x56, 0x03,
        0xd2, 0xd7, 0xfe, 0x6b, 0x01, 0x6f, 0xe4, 0x59,
        0xcc, 0x0d, 0x9a, 0x24, 0x6c, 0x86, 0x1b, 0x2e,
        0xdc, 0x4b, 0x4d, 0x35, 0x43, 0xe1, 0x1b, 0xad
    };
    unsigned char out_pub[sizeof(ec_pub_keydata)];
    char out_curve_name[80];
    const OSSL_PARAM *gettable = NULL;
    size_t len;
    EC_GROUP *group = NULL;
    BIGNUM *group_a = NULL;
    BIGNUM *group_b = NULL;
    BIGNUM *group_p = NULL;
    BIGNUM *a = NULL;
    BIGNUM *b = NULL;
    BIGNUM *p = NULL;


    if (!TEST_ptr(bld = OSSL_PARAM_BLD_new()))
        goto err;
    if (!TEST_ptr(ec_priv_bn = BN_bin2bn(ec_priv_keydata,
                                         sizeof(ec_priv_keydata), NULL)))
        goto err;

    if (OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_GROUP_NAME,
                                        curve, 0) <= 0)
        goto err;
    /*
     * We intentionally provide the input point in compressed format,
     * and avoid setting `OSSL_PKEY_PARAM_EC_POINT_CONVERSION_FORMAT`.
     *
     * Later on we check what format is used when exporting the
     * `OSSL_PKEY_PARAM_PUB_KEY` and expect to default to uncompressed
     * format.
     */
    if (OSSL_PARAM_BLD_push_octet_string(bld, OSSL_PKEY_PARAM_PUB_KEY,
                                         ec_pub_keydata_compressed,
                                         sizeof(ec_pub_keydata_compressed)) <= 0)
        goto err;
    if (OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_PRIV_KEY, ec_priv_bn) <= 0)
        goto err;
    if (!TEST_ptr(fromdata_params = OSSL_PARAM_BLD_to_param(bld)))
        goto err;
    ctx = EVP_PKEY_CTX_new_from_name(NULL, alg, NULL);
    if (!TEST_ptr(ctx))
        goto err;

    /* try importing parameters with bad curve first */
    nokey_params[0] =
        OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME,
                                         (char *)bad_curve, sizeof(bad_curve));
    if (!TEST_int_eq(EVP_PKEY_fromdata_init(ctx), 1)
        || !TEST_int_eq(EVP_PKEY_fromdata(ctx, &pk, EVP_PKEY_KEY_PARAMETERS,
                                          nokey_params), 0)
        || !TEST_ptr_null(pk))
        goto err;

    if (!TEST_int_eq(EVP_PKEY_fromdata_init(ctx), 1)
        || !TEST_int_eq(EVP_PKEY_fromdata(ctx, &pk, EVP_PKEY_KEYPAIR,
                                          fromdata_params), 1))
        goto err;

    while (dup_pk == NULL) {
        ret = 0;
        if (!TEST_int_eq(EVP_PKEY_get_bits(pk), 256)
            || !TEST_int_eq(EVP_PKEY_get_security_bits(pk), 128)
            || !TEST_int_eq(EVP_PKEY_get_size(pk), 2 + 35 * 2)
            || !TEST_false(EVP_PKEY_missing_parameters(pk)))
            goto err;

        if (!TEST_ptr(copy_pk = EVP_PKEY_new())
            || !TEST_true(EVP_PKEY_copy_parameters(copy_pk, pk)))
            goto err;
        EVP_PKEY_free(copy_pk);
        copy_pk = NULL;

        if (!TEST_ptr(gettable = EVP_PKEY_gettable_params(pk))
            || !TEST_ptr(OSSL_PARAM_locate_const(gettable,
                                                 OSSL_PKEY_PARAM_GROUP_NAME))
            || !TEST_ptr(OSSL_PARAM_locate_const(gettable,
                                                 OSSL_PKEY_PARAM_PUB_KEY))
            || !TEST_ptr(OSSL_PARAM_locate_const(gettable,
                                                 OSSL_PKEY_PARAM_PRIV_KEY)))
            goto err;

        if (!TEST_ptr(group = EC_GROUP_new_by_curve_name(OBJ_sn2nid(curve)))
            || !TEST_ptr(group_p = BN_new())
            || !TEST_ptr(group_a = BN_new())
            || !TEST_ptr(group_b = BN_new())
            || !TEST_true(EC_GROUP_get_curve(group, group_p, group_a, group_b, NULL)))
            goto err;

        if (!TEST_true(EVP_PKEY_get_bn_param(pk, OSSL_PKEY_PARAM_EC_A, &a))
            || !TEST_true(EVP_PKEY_get_bn_param(pk, OSSL_PKEY_PARAM_EC_B, &b))
            || !TEST_true(EVP_PKEY_get_bn_param(pk, OSSL_PKEY_PARAM_EC_P, &p)))
            goto err;

        if (!TEST_BN_eq(group_p, p) || !TEST_BN_eq(group_a, a)
            || !TEST_BN_eq(group_b, b))
            goto err;

        if (!EVP_PKEY_get_utf8_string_param(pk, OSSL_PKEY_PARAM_GROUP_NAME,
                                            out_curve_name,
                                            sizeof(out_curve_name),
                                            &len)
            || !TEST_str_eq(out_curve_name, curve)
            || !EVP_PKEY_get_octet_string_param(pk, OSSL_PKEY_PARAM_PUB_KEY,
                                            out_pub, sizeof(out_pub), &len)

            /*
             * Our providers use uncompressed format by default if
             * `OSSL_PKEY_PARAM_EC_POINT_CONVERSION_FORMAT` was not
             * explicitly set, irrespective of the format used for the
             * input point given as a param to create this key.
             */
            || !TEST_true(out_pub[0] == POINT_CONVERSION_UNCOMPRESSED)
            || !TEST_mem_eq(out_pub + 1, len - 1,
                            ec_pub_keydata + 1, sizeof(ec_pub_keydata) - 1)

            || !TEST_true(EVP_PKEY_get_bn_param(pk, OSSL_PKEY_PARAM_PRIV_KEY,
                                                &bn_priv))
            || !TEST_BN_eq(ec_priv_bn, bn_priv))
            goto err;
        BN_free(bn_priv);
        bn_priv = NULL;

        ret = test_print_key_using_pem(alg, pk)
              && test_print_key_using_encoder(alg, pk);

        if (!ret || !TEST_ptr(dup_pk = EVP_PKEY_dup(pk)))
            goto err;
        ret = ret && TEST_int_eq(EVP_PKEY_eq(pk, dup_pk), 1);
        EVP_PKEY_free(pk);
        pk = dup_pk;
        if (!ret)
            goto err;
    }

err:
    EC_GROUP_free(group);
    BN_free(group_a);
    BN_free(group_b);
    BN_free(group_p);
    BN_free(a);
    BN_free(b);
    BN_free(p);
    BN_free(bn_priv);
    BN_free(ec_priv_bn);
    OSSL_PARAM_free(fromdata_params);
    OSSL_PARAM_BLD_free(bld);
    EVP_PKEY_free(pk);
    EVP_PKEY_free(copy_pk);
    EVP_PKEY_CTX_free(ctx);
    return ret;
}

static int test_ec_dup_no_operation(void)
{
    int ret = 0;
    EVP_PKEY_CTX *pctx = NULL, *ctx = NULL, *kctx = NULL;
    EVP_PKEY *param = NULL, *pkey = NULL;

    if (!TEST_ptr(pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL))
        || !TEST_int_gt(EVP_PKEY_paramgen_init(pctx), 0)
        || !TEST_int_gt(EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx,
                        NID_X9_62_prime256v1), 0)
        || !TEST_int_gt(EVP_PKEY_paramgen(pctx, &param), 0)
        || !TEST_ptr(param))
        goto err;

    EVP_PKEY_CTX_free(pctx);
    pctx = NULL;

    if (!TEST_ptr(ctx = EVP_PKEY_CTX_new_from_pkey(NULL, param, NULL))
        || !TEST_ptr(kctx = EVP_PKEY_CTX_dup(ctx))
        || !TEST_int_gt(EVP_PKEY_keygen_init(kctx), 0)
        || !TEST_int_gt(EVP_PKEY_keygen(kctx, &pkey), 0))
        goto err;
    ret = 1;
err:
    EVP_PKEY_free(pkey);
    EVP_PKEY_free(param);
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_CTX_free(kctx);
    EVP_PKEY_CTX_free(pctx);
    return ret;
}

/* Test that keygen doesn't support EVP_PKEY_CTX_dup */
static int test_ec_dup_keygen_operation(void)
{
    int ret = 0;
    EVP_PKEY_CTX *pctx = NULL, *ctx = NULL, *kctx = NULL;
    EVP_PKEY *param = NULL, *pkey = NULL;

    if (!TEST_ptr(pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL))
        || !TEST_int_gt(EVP_PKEY_paramgen_init(pctx), 0)
        || !TEST_int_gt(EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx,
                        NID_X9_62_prime256v1), 0)
        || !TEST_int_gt(EVP_PKEY_paramgen(pctx, &param), 0)
        || !TEST_ptr(param))
        goto err;

    EVP_PKEY_CTX_free(pctx);
    pctx = NULL;

    if (!TEST_ptr(ctx = EVP_PKEY_CTX_new_from_pkey(NULL, param, NULL))
        || !TEST_int_gt(EVP_PKEY_keygen_init(ctx), 0)
        || !TEST_ptr_null(kctx = EVP_PKEY_CTX_dup(ctx)))
        goto err;
    ret = 1;
err:
    EVP_PKEY_free(pkey);
    EVP_PKEY_free(param);
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_CTX_free(kctx);
    EVP_PKEY_CTX_free(pctx);
    return ret;
}

#endif /* OPENSSL_NO_EC */

#ifndef OPENSSL_NO_DSA
static int test_fromdata_dsa_fips186_4(void)
{
    int ret = 0;
    EVP_PKEY_CTX *ctx = NULL, *key_ctx = NULL;
    EVP_PKEY *pk = NULL, *copy_pk = NULL, *dup_pk = NULL;
    BIGNUM *pub = NULL, *priv = NULL;
    BIGNUM *p = NULL, *q = NULL, *g = NULL;
    BIGNUM *pub_out = NULL, *priv_out = NULL;
    BIGNUM *p_out = NULL, *q_out = NULL, *g_out = NULL, *j_out = NULL;
    int gindex_out = 0, pcounter_out = 0, hindex_out = 0;
    char name_out[80];
    unsigned char seed_out[32];
    size_t len;
    OSSL_PARAM_BLD *bld = NULL;
    OSSL_PARAM *fromdata_params = NULL;

    /*
     * DSA parameter data was generated using the following:
     * openssl genpkey -genparam -algorithm DSA -pkeyopt pbits:2048 \
     *                 -pkeyopt qbits:256 -pkeyopt type:0 \
     *                 -pkeyopt gindex:1 -out dsa_params.pem -text
     */
    static const unsigned char p_data[] = {
        0x00, 0xa0, 0xb7, 0x02, 0xc4, 0xac, 0xa6, 0x42, 0xab, 0xf2, 0x34, 0x0b,
        0x22, 0x47, 0x1f, 0x33, 0xcf, 0xd5, 0x04, 0xe4, 0x3e, 0xec, 0xa1, 0x21,
        0xc8, 0x41, 0x2b, 0xef, 0xb8, 0x1f, 0x0b, 0x5b, 0x88, 0x8b, 0x67, 0xf8,
        0x68, 0x6d, 0x7c, 0x4d, 0x96, 0x5f, 0x3c, 0x66, 0xef, 0x58, 0x34, 0xd7,
        0xf6, 0xa2, 0x1b, 0xad, 0xc8, 0x12, 0x52, 0xb8, 0xe8, 0x2a, 0x63, 0xcc,
        0xea, 0xe7, 0x4e, 0xc8, 0x34, 0x4c, 0x58, 0x59, 0x0a, 0xc2, 0x4a, 0xe4,
        0xb4, 0x64, 0x20, 0xf4, 0xf6, 0x0a, 0xcf, 0x86, 0x01, 0x6c, 0x7f, 0x23,
        0x4a, 0x51, 0x07, 0x99, 0x42, 0x28, 0x7a, 0xff, 0x18, 0x67, 0x52, 0x64,
        0xf2, 0x9a, 0x62, 0x30, 0xc3, 0x00, 0xde, 0x23, 0xe9, 0x11, 0x95, 0x7e,
        0xd1, 0x3d, 0x8d, 0xb4, 0x0e, 0x9f, 0x9e, 0xb1, 0x30, 0x03, 0xf0, 0x73,
        0xa8, 0x40, 0x48, 0x42, 0x7b, 0x60, 0xa0, 0xc4, 0xf2, 0x3b, 0x2d, 0x0a,
        0x0c, 0xb8, 0x19, 0xfb, 0xb4, 0xf8, 0xe0, 0x2a, 0xc7, 0xf1, 0xc0, 0xc6,
        0x86, 0x14, 0x60, 0x12, 0x0f, 0xc0, 0xde, 0x4a, 0x67, 0xec, 0xc7, 0xde,
        0x76, 0x21, 0x1a, 0x55, 0x7f, 0x86, 0xc3, 0x97, 0x98, 0xce, 0xf5, 0xcd,
        0xf0, 0xe7, 0x12, 0xd6, 0x93, 0xee, 0x1b, 0x9b, 0x61, 0xef, 0x05, 0x8c,
        0x45, 0x46, 0xd9, 0x64, 0x6f, 0xbe, 0x27, 0xaa, 0x67, 0x01, 0xcc, 0x71,
        0xb1, 0x60, 0xce, 0x21, 0xd8, 0x51, 0x17, 0x27, 0x0d, 0x90, 0x3d, 0x18,
        0x7c, 0x87, 0x15, 0x8e, 0x48, 0x4c, 0x6c, 0xc5, 0x72, 0xeb, 0xb7, 0x56,
        0xf5, 0x6b, 0x60, 0x8f, 0xc2, 0xfd, 0x3f, 0x46, 0x5c, 0x00, 0x91, 0x85,
        0x79, 0x45, 0x5b, 0x1c, 0x82, 0xc4, 0x87, 0x50, 0x79, 0xba, 0xcc, 0x1c,
        0x32, 0x7e, 0x2e, 0xb8, 0x2e, 0xc5, 0x4e, 0xd1, 0x9b, 0xdb, 0x66, 0x79,
        0x7c, 0xfe, 0xaf, 0x6a, 0x05
    };
    static const unsigned char q_data[] = {
        0xa8, 0xcd, 0xf4, 0x33, 0x7b, 0x13, 0x0a, 0x24, 0xc1, 0xde, 0x4a, 0x04,
        0x7b, 0x4b, 0x71, 0x51, 0x32, 0xe9, 0x47, 0x74, 0xbd, 0x0c, 0x21, 0x40,
        0x84, 0x12, 0x0a, 0x17, 0x73, 0xdb, 0x29, 0xc7
    };
    static const unsigned char g_data[] = {
        0x6c, 0xc6, 0xa4, 0x3e, 0x61, 0x84, 0xc1, 0xff, 0x6f, 0x4a, 0x1a, 0x6b,
        0xb0, 0x24, 0x4b, 0xd2, 0x92, 0x5b, 0x29, 0x5c, 0x61, 0xb8, 0xc9, 0x2b,
        0xd6, 0xf7, 0x59, 0xfd, 0xd8, 0x70, 0x66, 0x77, 0xfc, 0xc1, 0xa4, 0xd4,
        0xb0, 0x1e, 0xd5, 0xbf, 0x59, 0x98, 0xb3, 0x66, 0x8b, 0xf4, 0x2e, 0xe6,
        0x12, 0x3e, 0xcc, 0xf8, 0x02, 0xb8, 0xc6, 0xc3, 0x47, 0xd2, 0xf5, 0xaa,
        0x0c, 0x5f, 0x51, 0xf5, 0xd0, 0x4c, 0x55, 0x3d, 0x07, 0x73, 0xa6, 0x57,
        0xce, 0x5a, 0xad, 0x42, 0x0c, 0x13, 0x0f, 0xe2, 0x31, 0x25, 0x8e, 0x72,
        0x12, 0x73, 0x10, 0xdb, 0x7f, 0x79, 0xeb, 0x59, 0xfc, 0xfe, 0xf7, 0x0c,
        0x1a, 0x81, 0x53, 0x96, 0x22, 0xb8, 0xe7, 0x58, 0xd8, 0x67, 0x80, 0x60,
        0xad, 0x8b, 0x55, 0x1c, 0x91, 0xf0, 0x72, 0x9a, 0x7e, 0xad, 0x37, 0xf1,
        0x77, 0x18, 0x96, 0x8a, 0x68, 0x70, 0xfc, 0x71, 0xa9, 0xa2, 0xe8, 0x35,
        0x27, 0x78, 0xf2, 0xef, 0x59, 0x36, 0x6d, 0x7c, 0xb6, 0x98, 0xd8, 0x1e,
        0xfa, 0x25, 0x73, 0x97, 0x45, 0x58, 0xe3, 0xae, 0xbd, 0x52, 0x54, 0x05,
        0xd8, 0x26, 0x26, 0xba, 0xba, 0x05, 0xb5, 0xe9, 0xe5, 0x76, 0xae, 0x25,
        0xdd, 0xfc, 0x10, 0x89, 0x5a, 0xa9, 0xee, 0x59, 0xc5, 0x79, 0x8b, 0xeb,
        0x1e, 0x2c, 0x61, 0xab, 0x0d, 0xd1, 0x10, 0x04, 0x91, 0x32, 0x77, 0x4a,
        0xa6, 0x64, 0x53, 0xda, 0x4c, 0xd7, 0x3a, 0x29, 0xd4, 0xf3, 0x82, 0x25,
        0x1d, 0x6f, 0x4a, 0x7f, 0xd3, 0x08, 0x3b, 0x42, 0x30, 0x10, 0xd8, 0xd0,
        0x97, 0x3a, 0xeb, 0x92, 0x63, 0xec, 0x93, 0x2b, 0x6f, 0x32, 0xd8, 0xcd,
        0x80, 0xd3, 0xc0, 0x4c, 0x03, 0xd5, 0xca, 0xbc, 0x8f, 0xc7, 0x43, 0x53,
        0x64, 0x66, 0x1c, 0x82, 0x2d, 0xfb, 0xff, 0x39, 0xba, 0xd6, 0x42, 0x62,
        0x02, 0x6f, 0x96, 0x36
    };
    static const unsigned char seed_data[] = {
        0x64, 0x46, 0x07, 0x32, 0x8d, 0x70, 0x9c, 0xb3, 0x8a, 0x35, 0xde, 0x62,
        0x00, 0xf2, 0x6d, 0x52, 0x37, 0x4d, 0xb3, 0x84, 0xe1, 0x9d, 0x41, 0x04,
        0xda, 0x7b, 0xdc, 0x0d, 0x8b, 0x5e, 0xe0, 0x84
    };
    const int gindex = 1;
    const int pcounter = 53;
    /*
     * The keypair was generated using
     * openssl genpkey -paramfile dsa_params.pem --pkeyopt pcounter:53 \
     *                 -pkeyopt gindex:1 \
     *                 -pkeyopt hexseed:644607328d709cb38a35de6200f26d -text
     */
    static const unsigned char priv_data[] = {
        0x00, 0x8f, 0xc5, 0x9e, 0xd0, 0xf7, 0x2a, 0x0b, 0x66, 0xf1, 0x32, 0x73,
        0xae, 0xf6, 0xd9, 0xd4, 0xdb, 0x2d, 0x96, 0x55, 0x89, 0xff, 0xef, 0xa8,
        0x5f, 0x47, 0x8f, 0xca, 0x02, 0x8a, 0xe1, 0x35, 0x90
    };
    static const unsigned char pub_data[] = {
        0x44, 0x19, 0xc9, 0x46, 0x45, 0x57, 0xc1, 0xa9, 0xd8, 0x30, 0x99, 0x29,
        0x6a, 0x4b, 0x63, 0x71, 0x69, 0x96, 0x35, 0x17, 0xb2, 0x62, 0x9b, 0x80,
        0x0a, 0x95, 0x9d, 0x6a, 0xc0, 0x32, 0x0d, 0x07, 0x5f, 0x19, 0x44, 0x02,
        0xf1, 0xbd, 0xce, 0xdf, 0x10, 0xf8, 0x02, 0x5d, 0x7d, 0x98, 0x8a, 0x73,
        0x89, 0x00, 0xb6, 0x24, 0xd6, 0x33, 0xe7, 0xcf, 0x8b, 0x49, 0x2a, 0xaf,
        0x13, 0x1c, 0xb2, 0x52, 0x15, 0xfd, 0x9b, 0xd5, 0x40, 0x4a, 0x1a, 0xda,
        0x29, 0x4c, 0x92, 0x7e, 0x66, 0x06, 0xdb, 0x61, 0x86, 0xac, 0xb5, 0xda,
        0x3c, 0x7d, 0x73, 0x7e, 0x54, 0x32, 0x68, 0xa5, 0x02, 0xbc, 0x59, 0x47,
        0x84, 0xd3, 0x87, 0x71, 0x5f, 0xeb, 0x43, 0x45, 0x24, 0xd3, 0xec, 0x08,
        0x52, 0xc2, 0x89, 0x2d, 0x9c, 0x1a, 0xcc, 0x91, 0x65, 0x5d, 0xa3, 0xa1,
        0x35, 0x31, 0x10, 0x1c, 0x3a, 0xa8, 0x4d, 0x18, 0xd5, 0x06, 0xaf, 0xb2,
        0xec, 0x5c, 0x89, 0x9e, 0x90, 0x86, 0x10, 0x01, 0xeb, 0x51, 0xd5, 0x1b,
        0x9c, 0xcb, 0x66, 0x07, 0x3f, 0xc4, 0x6e, 0x0a, 0x1b, 0x73, 0xa0, 0x4b,
        0x5f, 0x4d, 0xab, 0x35, 0x28, 0xfa, 0xda, 0x3a, 0x0c, 0x08, 0xe8, 0xf3,
        0xef, 0x42, 0x67, 0xbc, 0x21, 0xf2, 0xc2, 0xb8, 0xff, 0x1a, 0x81, 0x05,
        0x68, 0x73, 0x62, 0xdf, 0xd7, 0xab, 0x0f, 0x22, 0x89, 0x57, 0x96, 0xd4,
        0x93, 0xaf, 0xa1, 0x21, 0xa3, 0x48, 0xe9, 0xf0, 0x97, 0x47, 0xa0, 0x27,
        0xba, 0x87, 0xb8, 0x15, 0x5f, 0xff, 0x2c, 0x50, 0x41, 0xf1, 0x7e, 0xc6,
        0x81, 0xc4, 0x51, 0xf1, 0xfd, 0xd6, 0x86, 0xf7, 0x69, 0x97, 0xf1, 0x49,
        0xc9, 0xf9, 0xf4, 0x9b, 0xf4, 0xe8, 0x85, 0xa7, 0xbd, 0x36, 0x55, 0x4a,
        0x3d, 0xe8, 0x65, 0x09, 0x7b, 0xb7, 0x12, 0x64, 0xd2, 0x0a, 0x53, 0x60,
        0x48, 0xd1, 0x8a, 0xbd
    };

    if (!TEST_ptr(bld = OSSL_PARAM_BLD_new())
        || !TEST_ptr(pub = BN_bin2bn(pub_data, sizeof(pub_data), NULL))
        || !TEST_ptr(priv = BN_bin2bn(priv_data, sizeof(priv_data), NULL))
        || !TEST_ptr(p = BN_bin2bn(p_data, sizeof(p_data), NULL))
        || !TEST_ptr(q = BN_bin2bn(q_data, sizeof(q_data), NULL))
        || !TEST_ptr(g = BN_bin2bn(g_data, sizeof(g_data), NULL))

        || !TEST_true(OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_FFC_P, p))
        || !TEST_true(OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_FFC_Q, q))
        || !TEST_true(OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_FFC_G, g))
        || !TEST_true(OSSL_PARAM_BLD_push_octet_string(bld,
                                                       OSSL_PKEY_PARAM_FFC_SEED,
                                                       seed_data,
                                                       sizeof(seed_data)))
        || !TEST_true(OSSL_PARAM_BLD_push_int(bld, OSSL_PKEY_PARAM_FFC_GINDEX,
                                              gindex))
        || !TEST_true(OSSL_PARAM_BLD_push_int(bld,
                                              OSSL_PKEY_PARAM_FFC_PCOUNTER,
                                              pcounter))
        || !TEST_true(OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_PUB_KEY,
                                             pub))
        || !TEST_true(OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_PRIV_KEY,
                                             priv))
        || !TEST_ptr(fromdata_params = OSSL_PARAM_BLD_to_param(bld)))
        goto err;

    if (!TEST_ptr(ctx = EVP_PKEY_CTX_new_from_name(NULL, "DSA", NULL)))
        goto err;

    if (!TEST_int_eq(EVP_PKEY_fromdata_init(ctx), 1)
        || !TEST_int_eq(EVP_PKEY_fromdata(ctx, &pk, EVP_PKEY_KEYPAIR,
                                          fromdata_params), 1))
        goto err;

    while (dup_pk == NULL) {
        ret = 0;
        if (!TEST_int_eq(EVP_PKEY_get_bits(pk), 2048)
            || !TEST_int_eq(EVP_PKEY_get_security_bits(pk), 112)
            || !TEST_int_eq(EVP_PKEY_get_size(pk), 2 + 2 * (3 + sizeof(q_data)))
            || !TEST_false(EVP_PKEY_missing_parameters(pk)))
            goto err;

        if (!TEST_false(EVP_PKEY_get_utf8_string_param(pk,
                                                       OSSL_PKEY_PARAM_GROUP_NAME,
                                                       name_out,
                                                       sizeof(name_out),
                                                       &len))
            || !TEST_true(EVP_PKEY_get_bn_param(pk, OSSL_PKEY_PARAM_PUB_KEY,
                                                &pub_out))
            || !TEST_BN_eq(pub, pub_out)
            || !TEST_true(EVP_PKEY_get_bn_param(pk, OSSL_PKEY_PARAM_PRIV_KEY,
                                                &priv_out))
            || !TEST_BN_eq(priv, priv_out)
            || !TEST_true(EVP_PKEY_get_bn_param(pk, OSSL_PKEY_PARAM_FFC_P,
                                                &p_out))
            || !TEST_BN_eq(p, p_out)
            || !TEST_true(EVP_PKEY_get_bn_param(pk, OSSL_PKEY_PARAM_FFC_Q,
                                                &q_out))
            || !TEST_BN_eq(q, q_out)
            || !TEST_true(EVP_PKEY_get_bn_param(pk, OSSL_PKEY_PARAM_FFC_G,
                                                &g_out))
            || !TEST_BN_eq(g, g_out)
            || !TEST_false(EVP_PKEY_get_bn_param(pk,
                                                 OSSL_PKEY_PARAM_FFC_COFACTOR,
                                                 &j_out))
            || !TEST_ptr_null(j_out)
            || !TEST_true(EVP_PKEY_get_octet_string_param(pk,
                                                          OSSL_PKEY_PARAM_FFC_SEED,
                                                          seed_out,
                                                          sizeof(seed_out),
                                                          &len))
            || !TEST_true(EVP_PKEY_get_int_param(pk,
                                                 OSSL_PKEY_PARAM_FFC_GINDEX,
                                                 &gindex_out))
            || !TEST_int_eq(gindex, gindex_out)
            || !TEST_true(EVP_PKEY_get_int_param(pk, OSSL_PKEY_PARAM_FFC_H,
                                                 &hindex_out))
            || !TEST_int_eq(hindex_out, 0)
            || !TEST_true(EVP_PKEY_get_int_param(pk,
                                                 OSSL_PKEY_PARAM_FFC_PCOUNTER,
                                                 &pcounter_out))
            || !TEST_int_eq(pcounter, pcounter_out))
            goto err;
        BN_free(p);
        p = NULL;
        BN_free(q);
        q = NULL;
        BN_free(g);
        g = NULL;
        BN_free(j_out);
        j_out = NULL;
        BN_free(pub_out);
        pub_out = NULL;
        BN_free(priv_out);
        priv_out = NULL;

        if (!TEST_ptr(key_ctx = EVP_PKEY_CTX_new_from_pkey(NULL, pk, "")))
            goto err;

        if (!TEST_int_gt(EVP_PKEY_check(key_ctx), 0)
            || !TEST_int_gt(EVP_PKEY_public_check(key_ctx), 0)
            || !TEST_int_gt(EVP_PKEY_private_check(key_ctx), 0)
            || !TEST_int_gt(EVP_PKEY_pairwise_check(key_ctx), 0))
            goto err;
        EVP_PKEY_CTX_free(key_ctx);
        key_ctx = NULL;

        if (!TEST_ptr(copy_pk = EVP_PKEY_new())
            || !TEST_true(EVP_PKEY_copy_parameters(copy_pk, pk)))
            goto err;
        EVP_PKEY_free(copy_pk);
        copy_pk = NULL;

        ret = test_print_key_using_pem("DSA", pk)
              && test_print_key_using_encoder("DSA", pk);

        if (!ret || !TEST_ptr(dup_pk = EVP_PKEY_dup(pk)))
            goto err;
        ret = ret && TEST_int_eq(EVP_PKEY_eq(pk, dup_pk), 1);
        EVP_PKEY_free(pk);
        pk = dup_pk;
        if (!ret)
            goto err;
    }

 err:
    OSSL_PARAM_free(fromdata_params);
    OSSL_PARAM_BLD_free(bld);
    BN_free(p);
    BN_free(q);
    BN_free(g);
    BN_free(pub);
    BN_free(priv);
    BN_free(p_out);
    BN_free(q_out);
    BN_free(g_out);
    BN_free(pub_out);
    BN_free(priv_out);
    BN_free(j_out);
    EVP_PKEY_free(pk);
    EVP_PKEY_free(copy_pk);
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_CTX_free(key_ctx);

    return ret;
}

static int test_check_dsa(void)
{
    int ret = 0;
    EVP_PKEY_CTX *ctx = NULL;

    if (!TEST_ptr(ctx = EVP_PKEY_CTX_new_from_name(NULL, "DSA", NULL))
        || !TEST_int_le(EVP_PKEY_check(ctx), 0)
        || !TEST_int_le(EVP_PKEY_public_check(ctx), 0)
        || !TEST_int_le(EVP_PKEY_private_check(ctx), 0)
        || !TEST_int_le(EVP_PKEY_pairwise_check(ctx), 0))
       goto err;

    ret = 1;
 err:
    EVP_PKEY_CTX_free(ctx);

    return ret;
}
#endif /* OPENSSL_NO_DSA */


static OSSL_PARAM *do_construct_hkdf_params(char *digest, char *key,
                                            size_t keylen, char *salt)
{
    OSSL_PARAM *params = OPENSSL_malloc(sizeof(OSSL_PARAM) * 5);
    OSSL_PARAM *p = params;

    *p++ = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest, 0);
    *p++ = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
                                             salt, strlen(salt));
    *p++ = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,
                                             (unsigned char *)key, keylen);
    *p++ = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_MODE,
                                            "EXTRACT_ONLY", 0);
    *p = OSSL_PARAM_construct_end();

    return params;
}

/* Test that EVP_PKEY_CTX_dup() fails gracefully for a KDF */
static int test_evp_pkey_ctx_dup_kdf_fail(void)
{
    int ret = 0;
    size_t len = 0;
    EVP_PKEY_CTX *pctx = NULL, *dctx = NULL;
    OSSL_PARAM *params = NULL;

    if (!TEST_ptr(params = do_construct_hkdf_params("sha256", "secret", 6,
                                                    "salt")))
        goto err;
    if (!TEST_ptr(pctx = EVP_PKEY_CTX_new_from_name(NULL, "HKDF", NULL)))
        goto err;
    if (!TEST_int_eq(EVP_PKEY_derive_init_ex(pctx, params), 1))
        goto err;
    if (!TEST_int_eq(EVP_PKEY_derive(pctx, NULL, &len), 1)
        || !TEST_size_t_eq(len, SHA256_DIGEST_LENGTH))
        goto err;
    if (!TEST_ptr_null(dctx = EVP_PKEY_CTX_dup(pctx)))
        goto err;
    ret = 1;
err:
    OPENSSL_free(params);
    EVP_PKEY_CTX_free(dctx);
    EVP_PKEY_CTX_free(pctx);
    return ret;
}

int setup_tests(void)
{
    if (!test_skip_common_options()) {
        TEST_error("Error parsing test options\n");
        return 0;
    }

    if (!TEST_ptr(datadir = test_get_argument(0)))
        return 0;

    ADD_TEST(test_evp_pkey_ctx_dup_kdf_fail);
    ADD_TEST(test_evp_pkey_get_bn_param_large);
    ADD_TEST(test_fromdata_rsa);
#ifndef OPENSSL_NO_DH
    ADD_TEST(test_fromdata_dh_fips186_4);
    ADD_TEST(test_fromdata_dh_named_group);
#endif
#ifndef OPENSSL_NO_DSA
    ADD_TEST(test_check_dsa);
    ADD_TEST(test_fromdata_dsa_fips186_4);
#endif
#ifndef OPENSSL_NO_EC
    ADD_ALL_TESTS(test_fromdata_ecx, 4 * 3);
    ADD_TEST(test_fromdata_ec);
    ADD_TEST(test_ec_dup_no_operation);
    ADD_TEST(test_ec_dup_keygen_operation);
#endif
    return 1;
}
