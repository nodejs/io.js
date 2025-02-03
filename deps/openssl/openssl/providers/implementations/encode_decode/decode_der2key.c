/*
 * Copyright 2020-2024 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * low level APIs are deprecated for public use, but still ok for
 * internal use.
 */
#include "internal/deprecated.h"

#include <openssl/core_dispatch.h>
#include <openssl/core_names.h>
#include <openssl/core_object.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/params.h>
#include <openssl/pem.h>         /* PEM_BUFSIZE and public PEM functions */
#include <openssl/pkcs12.h>
#include <openssl/x509.h>
#include <openssl/proverr.h>
#include "internal/cryptlib.h"   /* ossl_assert() */
#include "internal/asn1.h"
#include "crypto/dh.h"
#include "crypto/dsa.h"
#include "crypto/ec.h"
#include "crypto/evp.h"
#include "crypto/ecx.h"
#include "crypto/rsa.h"
#include "crypto/x509.h"
#include "openssl/obj_mac.h"
#include "prov/bio.h"
#include "prov/implementations.h"
#include "endecoder_local.h"

struct der2key_ctx_st;           /* Forward declaration */
typedef int check_key_fn(void *, struct der2key_ctx_st *ctx);
typedef void adjust_key_fn(void *, struct der2key_ctx_st *ctx);
typedef void free_key_fn(void *);
typedef void *d2i_PKCS8_fn(void **, const unsigned char **, long,
                           struct der2key_ctx_st *);
struct keytype_desc_st {
    const char *keytype_name;
    const OSSL_DISPATCH *fns; /* Keymgmt (to pilfer functions from) */

    /* The input structure name */
    const char *structure_name;

    /*
     * The EVP_PKEY_xxx type macro.  Should be zero for type specific
     * structures, non-zero when the outermost structure is PKCS#8 or
     * SubjectPublicKeyInfo.  This determines which of the function
     * pointers below will be used.
     */
    int evp_type;

    /* The selection mask for OSSL_FUNC_decoder_does_selection() */
    int selection_mask;

    /* For type specific decoders, we use the corresponding d2i */
    d2i_of_void *d2i_private_key; /* From type-specific DER */
    d2i_of_void *d2i_public_key;  /* From type-specific DER */
    d2i_of_void *d2i_key_params;  /* From type-specific DER */
    d2i_PKCS8_fn *d2i_PKCS8;      /* Wrapped in a PrivateKeyInfo */
    d2i_of_void *d2i_PUBKEY;      /* Wrapped in a SubjectPublicKeyInfo */

    /*
     * For any key, we may need to check that the key meets expectations.
     * This is useful when the same functions can decode several variants
     * of a key.
     */
    check_key_fn *check_key;

    /*
     * For any key, we may need to make provider specific adjustments, such
     * as ensure the key carries the correct library context.
     */
    adjust_key_fn *adjust_key;
    /* {type}_free() */
    free_key_fn *free_key;
};

/*
 * Context used for DER to key decoding.
 */
struct der2key_ctx_st {
    PROV_CTX *provctx;
    const struct keytype_desc_st *desc;
    /* The selection that is passed to der2key_decode() */
    int selection;
    /* Flag used to signal that a failure is fatal */
    unsigned int flag_fatal : 1;
};

typedef void *key_from_pkcs8_t(const PKCS8_PRIV_KEY_INFO *p8inf,
                               OSSL_LIB_CTX *libctx, const char *propq);
static void *der2key_decode_p8(const unsigned char **input_der,
                               long input_der_len, struct der2key_ctx_st *ctx,
                               key_from_pkcs8_t *key_from_pkcs8)
{
    PKCS8_PRIV_KEY_INFO *p8inf = NULL;
    const X509_ALGOR *alg = NULL;
    void *key = NULL;

    if ((p8inf = d2i_PKCS8_PRIV_KEY_INFO(NULL, input_der, input_der_len)) != NULL
        && PKCS8_pkey_get0(NULL, NULL, NULL, &alg, p8inf)
        && (OBJ_obj2nid(alg->algorithm) == ctx->desc->evp_type
            /* Allow decoding sm2 private key with id_ecPublicKey */
            || (OBJ_obj2nid(alg->algorithm) == NID_X9_62_id_ecPublicKey
                && ctx->desc->evp_type == NID_sm2)))
        key = key_from_pkcs8(p8inf, PROV_LIBCTX_OF(ctx->provctx), NULL);
    PKCS8_PRIV_KEY_INFO_free(p8inf);

    return key;
}

/* ---------------------------------------------------------------------- */

static OSSL_FUNC_decoder_freectx_fn der2key_freectx;
static OSSL_FUNC_decoder_decode_fn der2key_decode;
static OSSL_FUNC_decoder_export_object_fn der2key_export_object;

static struct der2key_ctx_st *
der2key_newctx(void *provctx, const struct keytype_desc_st *desc)
{
    struct der2key_ctx_st *ctx = OPENSSL_zalloc(sizeof(*ctx));

    if (ctx != NULL) {
        ctx->provctx = provctx;
        ctx->desc = desc;
    }
    return ctx;
}

static void der2key_freectx(void *vctx)
{
    struct der2key_ctx_st *ctx = vctx;

    OPENSSL_free(ctx);
}

static int der2key_check_selection(int selection,
                                   const struct keytype_desc_st *desc)
{
    /*
     * The selections are kinda sorta "levels", i.e. each selection given
     * here is assumed to include those following.
     */
    int checks[] = {
        OSSL_KEYMGMT_SELECT_PRIVATE_KEY,
        OSSL_KEYMGMT_SELECT_PUBLIC_KEY,
        OSSL_KEYMGMT_SELECT_ALL_PARAMETERS
    };
    size_t i;

    /* The decoder implementations made here support guessing */
    if (selection == 0)
        return 1;

    for (i = 0; i < OSSL_NELEM(checks); i++) {
        int check1 = (selection & checks[i]) != 0;
        int check2 = (desc->selection_mask & checks[i]) != 0;

        /*
         * If the caller asked for the currently checked bit(s), return
         * whether the decoder description says it's supported.
         */
        if (check1)
            return check2;
    }

    /* This should be dead code, but just to be safe... */
    return 0;
}

static int der2key_decode(void *vctx, OSSL_CORE_BIO *cin, int selection,
                          OSSL_CALLBACK *data_cb, void *data_cbarg,
                          OSSL_PASSPHRASE_CALLBACK *pw_cb, void *pw_cbarg)
{
    struct der2key_ctx_st *ctx = vctx;
    unsigned char *der = NULL;
    const unsigned char *derp;
    long der_len = 0;
    void *key = NULL;
    int ok = 0;

    ctx->selection = selection;
    /*
     * The caller is allowed to specify 0 as a selection mark, to have the
     * structure and key type guessed.  For type-specific structures, this
     * is not recommended, as some structures are very similar.
     * Note that 0 isn't the same as OSSL_KEYMGMT_SELECT_ALL, as the latter
     * signifies a private key structure, where everything else is assumed
     * to be present as well.
     */
    if (selection == 0)
        selection = ctx->desc->selection_mask;
    if ((selection & ctx->desc->selection_mask) == 0) {
        ERR_raise(ERR_LIB_PROV, ERR_R_PASSED_INVALID_ARGUMENT);
        return 0;
    }

    ok = ossl_read_der(ctx->provctx, cin, &der, &der_len);
    if (!ok)
        goto next;

    ok = 0; /* Assume that we fail */

    ERR_set_mark();
    if ((selection & OSSL_KEYMGMT_SELECT_PRIVATE_KEY) != 0) {
        derp = der;
        if (ctx->desc->d2i_PKCS8 != NULL) {
            key = ctx->desc->d2i_PKCS8(NULL, &derp, der_len, ctx);
            if (ctx->flag_fatal) {
                ERR_clear_last_mark();
                goto end;
            }
        } else if (ctx->desc->d2i_private_key != NULL) {
            key = ctx->desc->d2i_private_key(NULL, &derp, der_len);
        }
        if (key == NULL && ctx->selection != 0) {
            ERR_clear_last_mark();
            goto next;
        }
    }
    if (key == NULL && (selection & OSSL_KEYMGMT_SELECT_PUBLIC_KEY) != 0) {
        derp = der;
        if (ctx->desc->d2i_PUBKEY != NULL)
            key = ctx->desc->d2i_PUBKEY(NULL, &derp, der_len);
        else if (ctx->desc->d2i_public_key != NULL)
            key = ctx->desc->d2i_public_key(NULL, &derp, der_len);
        if (key == NULL && ctx->selection != 0) {
            ERR_clear_last_mark();
            goto next;
        }
    }
    if (key == NULL && (selection & OSSL_KEYMGMT_SELECT_ALL_PARAMETERS) != 0) {
        derp = der;
        if (ctx->desc->d2i_key_params != NULL)
            key = ctx->desc->d2i_key_params(NULL, &derp, der_len);
        if (key == NULL && ctx->selection != 0) {
            ERR_clear_last_mark();
            goto next;
        }
    }
    if (key == NULL)
        ERR_clear_last_mark();
    else
        ERR_pop_to_mark();

    /*
     * Last minute check to see if this was the correct type of key.  This
     * should never lead to a fatal error, i.e. the decoding itself was
     * correct, it was just an unexpected key type.  This is generally for
     * classes of key types that have subtle variants, like RSA-PSS keys as
     * opposed to plain RSA keys.
     */
    if (key != NULL
        && ctx->desc->check_key != NULL
        && !ctx->desc->check_key(key, ctx)) {
        ctx->desc->free_key(key);
        key = NULL;
    }

    if (key != NULL && ctx->desc->adjust_key != NULL)
        ctx->desc->adjust_key(key, ctx);

 next:
    /*
     * Indicated that we successfully decoded something, or not at all.
     * Ending up "empty handed" is not an error.
     */
    ok = 1;

    /*
     * We free memory here so it's not held up during the callback, because
     * we know the process is recursive and the allocated chunks of memory
     * add up.
     */
    OPENSSL_free(der);
    der = NULL;

    if (key != NULL) {
        OSSL_PARAM params[4];
        int object_type = OSSL_OBJECT_PKEY;

        params[0] =
            OSSL_PARAM_construct_int(OSSL_OBJECT_PARAM_TYPE, &object_type);

#ifndef OPENSSL_NO_SM2
        if (strcmp(ctx->desc->keytype_name, "EC") == 0
            && (EC_KEY_get_flags(key) & EC_FLAG_SM2_RANGE) != 0)
            params[1] =
                OSSL_PARAM_construct_utf8_string(OSSL_OBJECT_PARAM_DATA_TYPE,
                                                 "SM2", 0);
        else
#endif
            params[1] =
                OSSL_PARAM_construct_utf8_string(OSSL_OBJECT_PARAM_DATA_TYPE,
                                                 (char *)ctx->desc->keytype_name,
                                                 0);
        /* The address of the key becomes the octet string */
        params[2] =
            OSSL_PARAM_construct_octet_string(OSSL_OBJECT_PARAM_REFERENCE,
                                              &key, sizeof(key));
        params[3] = OSSL_PARAM_construct_end();

        ok = data_cb(params, data_cbarg);
    }

 end:
    ctx->desc->free_key(key);
    OPENSSL_free(der);

    return ok;
}

static int der2key_export_object(void *vctx,
                                 const void *reference, size_t reference_sz,
                                 OSSL_CALLBACK *export_cb, void *export_cbarg)
{
    struct der2key_ctx_st *ctx = vctx;
    OSSL_FUNC_keymgmt_export_fn *export =
        ossl_prov_get_keymgmt_export(ctx->desc->fns);
    void *keydata;

    if (reference_sz == sizeof(keydata) && export != NULL) {
        int selection = ctx->selection;

        if (selection == 0)
            selection = OSSL_KEYMGMT_SELECT_ALL;
        /* The contents of the reference is the address to our object */
        keydata = *(void **)reference;

        return export(keydata, selection, export_cb, export_cbarg);
    }
    return 0;
}

/* ---------------------------------------------------------------------- */

#ifndef OPENSSL_NO_DH
# define dh_evp_type                    EVP_PKEY_DH
# define dh_d2i_private_key             NULL
# define dh_d2i_public_key              NULL
# define dh_d2i_key_params              (d2i_of_void *)d2i_DHparams

static void *dh_d2i_PKCS8(void **key, const unsigned char **der, long der_len,
                          struct der2key_ctx_st *ctx)
{
    return der2key_decode_p8(der, der_len, ctx,
                             (key_from_pkcs8_t *)ossl_dh_key_from_pkcs8);
}

# define dh_d2i_PUBKEY                  (d2i_of_void *)ossl_d2i_DH_PUBKEY
# define dh_free                        (free_key_fn *)DH_free
# define dh_check                       NULL

static void dh_adjust(void *key, struct der2key_ctx_st *ctx)
{
    ossl_dh_set0_libctx(key, PROV_LIBCTX_OF(ctx->provctx));
}

# define dhx_evp_type                   EVP_PKEY_DHX
# define dhx_d2i_private_key            NULL
# define dhx_d2i_public_key             NULL
# define dhx_d2i_key_params             (d2i_of_void *)d2i_DHxparams
# define dhx_d2i_PKCS8                  dh_d2i_PKCS8
# define dhx_d2i_PUBKEY                 (d2i_of_void *)ossl_d2i_DHx_PUBKEY
# define dhx_free                       (free_key_fn *)DH_free
# define dhx_check                      NULL
# define dhx_adjust                     dh_adjust
#endif

/* ---------------------------------------------------------------------- */

#ifndef OPENSSL_NO_DSA
# define dsa_evp_type                   EVP_PKEY_DSA
# define dsa_d2i_private_key            (d2i_of_void *)d2i_DSAPrivateKey
# define dsa_d2i_public_key             (d2i_of_void *)d2i_DSAPublicKey
# define dsa_d2i_key_params             (d2i_of_void *)d2i_DSAparams

static void *dsa_d2i_PKCS8(void **key, const unsigned char **der, long der_len,
                           struct der2key_ctx_st *ctx)
{
    return der2key_decode_p8(der, der_len, ctx,
                             (key_from_pkcs8_t *)ossl_dsa_key_from_pkcs8);
}

# define dsa_d2i_PUBKEY                 (d2i_of_void *)ossl_d2i_DSA_PUBKEY
# define dsa_free                       (free_key_fn *)DSA_free
# define dsa_check                      NULL

static void dsa_adjust(void *key, struct der2key_ctx_st *ctx)
{
    ossl_dsa_set0_libctx(key, PROV_LIBCTX_OF(ctx->provctx));
}
#endif

/* ---------------------------------------------------------------------- */

#ifndef OPENSSL_NO_EC
# define ec_evp_type                    EVP_PKEY_EC
# define ec_d2i_private_key             (d2i_of_void *)d2i_ECPrivateKey
# define ec_d2i_public_key              NULL
# define ec_d2i_key_params              (d2i_of_void *)d2i_ECParameters

static void *ec_d2i_PKCS8(void **key, const unsigned char **der, long der_len,
                          struct der2key_ctx_st *ctx)
{
    return der2key_decode_p8(der, der_len, ctx,
                             (key_from_pkcs8_t *)ossl_ec_key_from_pkcs8);
}

# define ec_d2i_PUBKEY                  (d2i_of_void *)d2i_EC_PUBKEY
# define ec_free                        (free_key_fn *)EC_KEY_free

static int ec_check(void *key, struct der2key_ctx_st *ctx)
{
    /* We're trying to be clever by comparing two truths */
    int ret = 0;
    int sm2 = (EC_KEY_get_flags(key) & EC_FLAG_SM2_RANGE) != 0;

    if (sm2)
        ret = ctx->desc->evp_type == EVP_PKEY_SM2
            || ctx->desc->evp_type == NID_X9_62_id_ecPublicKey;
    else
        ret = ctx->desc->evp_type != EVP_PKEY_SM2;

    return ret;
}

static void ec_adjust(void *key, struct der2key_ctx_st *ctx)
{
    ossl_ec_key_set0_libctx(key, PROV_LIBCTX_OF(ctx->provctx));
}

/*
 * ED25519, ED448, X25519, X448 only implement PKCS#8 and SubjectPublicKeyInfo,
 * so no d2i functions to be had.
 */

static void *ecx_d2i_PKCS8(void **key, const unsigned char **der, long der_len,
                           struct der2key_ctx_st *ctx)
{
    return der2key_decode_p8(der, der_len, ctx,
                             (key_from_pkcs8_t *)ossl_ecx_key_from_pkcs8);
}

static void ecx_key_adjust(void *key, struct der2key_ctx_st *ctx)
{
    ossl_ecx_key_set0_libctx(key, PROV_LIBCTX_OF(ctx->provctx));
}

# define ed25519_evp_type               EVP_PKEY_ED25519
# define ed25519_d2i_private_key        NULL
# define ed25519_d2i_public_key         NULL
# define ed25519_d2i_key_params         NULL
# define ed25519_d2i_PKCS8              ecx_d2i_PKCS8
# define ed25519_d2i_PUBKEY             (d2i_of_void *)ossl_d2i_ED25519_PUBKEY
# define ed25519_free                   (free_key_fn *)ossl_ecx_key_free
# define ed25519_check                  NULL
# define ed25519_adjust                 ecx_key_adjust

# define ed448_evp_type                 EVP_PKEY_ED448
# define ed448_d2i_private_key          NULL
# define ed448_d2i_public_key           NULL
# define ed448_d2i_key_params           NULL
# define ed448_d2i_PKCS8                ecx_d2i_PKCS8
# define ed448_d2i_PUBKEY               (d2i_of_void *)ossl_d2i_ED448_PUBKEY
# define ed448_free                     (free_key_fn *)ossl_ecx_key_free
# define ed448_check                    NULL
# define ed448_adjust                   ecx_key_adjust

# define x25519_evp_type                EVP_PKEY_X25519
# define x25519_d2i_private_key         NULL
# define x25519_d2i_public_key          NULL
# define x25519_d2i_key_params          NULL
# define x25519_d2i_PKCS8               ecx_d2i_PKCS8
# define x25519_d2i_PUBKEY              (d2i_of_void *)ossl_d2i_X25519_PUBKEY
# define x25519_free                    (free_key_fn *)ossl_ecx_key_free
# define x25519_check                   NULL
# define x25519_adjust                  ecx_key_adjust

# define x448_evp_type                  EVP_PKEY_X448
# define x448_d2i_private_key           NULL
# define x448_d2i_public_key            NULL
# define x448_d2i_key_params            NULL
# define x448_d2i_PKCS8                 ecx_d2i_PKCS8
# define x448_d2i_PUBKEY                (d2i_of_void *)ossl_d2i_X448_PUBKEY
# define x448_free                      (free_key_fn *)ossl_ecx_key_free
# define x448_check                     NULL
# define x448_adjust                    ecx_key_adjust

# ifndef OPENSSL_NO_SM2
#  define sm2_evp_type                  EVP_PKEY_SM2
#  define sm2_d2i_private_key           (d2i_of_void *)d2i_ECPrivateKey
#  define sm2_d2i_public_key            NULL
#  define sm2_d2i_key_params            (d2i_of_void *)d2i_ECParameters

static void *sm2_d2i_PKCS8(void **key, const unsigned char **der, long der_len,
                           struct der2key_ctx_st *ctx)
{
    return der2key_decode_p8(der, der_len, ctx,
                             (key_from_pkcs8_t *)ossl_ec_key_from_pkcs8);
}

#  define sm2_d2i_PUBKEY                (d2i_of_void *)d2i_EC_PUBKEY
#  define sm2_free                      (free_key_fn *)EC_KEY_free
#  define sm2_check                     ec_check
#  define sm2_adjust                    ec_adjust
# endif
#endif

/* ---------------------------------------------------------------------- */

#define rsa_evp_type                    EVP_PKEY_RSA
#define rsa_d2i_private_key             (d2i_of_void *)d2i_RSAPrivateKey
#define rsa_d2i_public_key              (d2i_of_void *)d2i_RSAPublicKey
#define rsa_d2i_key_params              NULL

static void *rsa_d2i_PKCS8(void **key, const unsigned char **der, long der_len,
                           struct der2key_ctx_st *ctx)
{
    return der2key_decode_p8(der, der_len, ctx,
                             (key_from_pkcs8_t *)ossl_rsa_key_from_pkcs8);
}

#define rsa_d2i_PUBKEY                  (d2i_of_void *)d2i_RSA_PUBKEY
#define rsa_free                        (free_key_fn *)RSA_free

static int rsa_check(void *key, struct der2key_ctx_st *ctx)
{
    switch (RSA_test_flags(key, RSA_FLAG_TYPE_MASK)) {
    case RSA_FLAG_TYPE_RSA:
        return ctx->desc->evp_type == EVP_PKEY_RSA;
    case RSA_FLAG_TYPE_RSASSAPSS:
        return ctx->desc->evp_type == EVP_PKEY_RSA_PSS;
    }

    /* Currently unsupported RSA key type */
    return 0;
}

static void rsa_adjust(void *key, struct der2key_ctx_st *ctx)
{
    ossl_rsa_set0_libctx(key, PROV_LIBCTX_OF(ctx->provctx));
}

#define rsapss_evp_type                 EVP_PKEY_RSA_PSS
#define rsapss_d2i_private_key          (d2i_of_void *)d2i_RSAPrivateKey
#define rsapss_d2i_public_key           (d2i_of_void *)d2i_RSAPublicKey
#define rsapss_d2i_key_params           NULL
#define rsapss_d2i_PKCS8                rsa_d2i_PKCS8
#define rsapss_d2i_PUBKEY               (d2i_of_void *)d2i_RSA_PUBKEY
#define rsapss_free                     (free_key_fn *)RSA_free
#define rsapss_check                    rsa_check
#define rsapss_adjust                   rsa_adjust

/* ---------------------------------------------------------------------- */

/*
 * The DO_ macros help define the selection mask and the method functions
 * for each kind of object we want to decode.
 */
#define DO_type_specific_keypair(keytype)               \
    "type-specific", keytype##_evp_type,                \
        ( OSSL_KEYMGMT_SELECT_KEYPAIR ),                \
        keytype##_d2i_private_key,                      \
        keytype##_d2i_public_key,                       \
        NULL,                                           \
        NULL,                                           \
        NULL,                                           \
        keytype##_check,                                \
        keytype##_adjust,                               \
        keytype##_free

#define DO_type_specific_pub(keytype)                   \
    "type-specific", keytype##_evp_type,                \
        ( OSSL_KEYMGMT_SELECT_PUBLIC_KEY ),             \
        NULL,                                           \
        keytype##_d2i_public_key,                       \
        NULL,                                           \
        NULL,                                           \
        NULL,                                           \
        keytype##_check,                                \
        keytype##_adjust,                               \
        keytype##_free

#define DO_type_specific_priv(keytype)                  \
    "type-specific", keytype##_evp_type,                \
        ( OSSL_KEYMGMT_SELECT_PRIVATE_KEY ),            \
        keytype##_d2i_private_key,                      \
        NULL,                                           \
        NULL,                                           \
        NULL,                                           \
        NULL,                                           \
        keytype##_check,                                \
        keytype##_adjust,                               \
        keytype##_free

#define DO_type_specific_params(keytype)                \
    "type-specific", keytype##_evp_type,                \
        ( OSSL_KEYMGMT_SELECT_ALL_PARAMETERS ),         \
        NULL,                                           \
        NULL,                                           \
        keytype##_d2i_key_params,                       \
        NULL,                                           \
        NULL,                                           \
        keytype##_check,                                \
        keytype##_adjust,                               \
        keytype##_free

#define DO_type_specific(keytype)                       \
    "type-specific", keytype##_evp_type,                \
        ( OSSL_KEYMGMT_SELECT_ALL ),                    \
        keytype##_d2i_private_key,                      \
        keytype##_d2i_public_key,                       \
        keytype##_d2i_key_params,                       \
        NULL,                                           \
        NULL,                                           \
        keytype##_check,                                \
        keytype##_adjust,                               \
        keytype##_free

#define DO_type_specific_no_pub(keytype)                \
    "type-specific", keytype##_evp_type,                \
        ( OSSL_KEYMGMT_SELECT_PRIVATE_KEY               \
          | OSSL_KEYMGMT_SELECT_ALL_PARAMETERS ),       \
        keytype##_d2i_private_key,                      \
        NULL,                                           \
        keytype##_d2i_key_params,                       \
        NULL,                                           \
        NULL,                                           \
        keytype##_check,                                \
        keytype##_adjust,                               \
        keytype##_free

#define DO_PrivateKeyInfo(keytype)                      \
    "PrivateKeyInfo", keytype##_evp_type,               \
        ( OSSL_KEYMGMT_SELECT_PRIVATE_KEY ),            \
        NULL,                                           \
        NULL,                                           \
        NULL,                                           \
        keytype##_d2i_PKCS8,                            \
        NULL,                                           \
        keytype##_check,                                \
        keytype##_adjust,                               \
        keytype##_free

#define DO_SubjectPublicKeyInfo(keytype)                \
    "SubjectPublicKeyInfo", keytype##_evp_type,         \
        ( OSSL_KEYMGMT_SELECT_PUBLIC_KEY ),             \
        NULL,                                           \
        NULL,                                           \
        NULL,                                           \
        NULL,                                           \
        keytype##_d2i_PUBKEY,                           \
        keytype##_check,                                \
        keytype##_adjust,                               \
        keytype##_free

#define DO_DH(keytype)                                  \
    "DH", keytype##_evp_type,                           \
        ( OSSL_KEYMGMT_SELECT_ALL_PARAMETERS ),         \
        NULL,                                           \
        NULL,                                           \
        keytype##_d2i_key_params,                       \
        NULL,                                           \
        NULL,                                           \
        keytype##_check,                                \
        keytype##_adjust,                               \
        keytype##_free

#define DO_DHX(keytype)                                 \
    "DHX", keytype##_evp_type,                          \
        ( OSSL_KEYMGMT_SELECT_ALL_PARAMETERS ),         \
        NULL,                                           \
        NULL,                                           \
        keytype##_d2i_key_params,                       \
        NULL,                                           \
        NULL,                                           \
        keytype##_check,                                \
        keytype##_adjust,                               \
        keytype##_free

#define DO_DSA(keytype)                                 \
    "DSA", keytype##_evp_type,                          \
        ( OSSL_KEYMGMT_SELECT_ALL ),                    \
        keytype##_d2i_private_key,                      \
        keytype##_d2i_public_key,                       \
        keytype##_d2i_key_params,                       \
        NULL,                                           \
        NULL,                                           \
        keytype##_check,                                \
        keytype##_adjust,                               \
        keytype##_free

#define DO_EC(keytype)                                  \
    "EC", keytype##_evp_type,                           \
        ( OSSL_KEYMGMT_SELECT_PRIVATE_KEY               \
          | OSSL_KEYMGMT_SELECT_ALL_PARAMETERS ),       \
        keytype##_d2i_private_key,                      \
        NULL,                                           \
        keytype##_d2i_key_params,                       \
        NULL,                                           \
        NULL,                                           \
        keytype##_check,                                \
        keytype##_adjust,                               \
        keytype##_free

#define DO_RSA(keytype)                                 \
    "RSA", keytype##_evp_type,                          \
        ( OSSL_KEYMGMT_SELECT_KEYPAIR ),                \
        keytype##_d2i_private_key,                      \
        keytype##_d2i_public_key,                       \
        NULL,                                           \
        NULL,                                           \
        NULL,                                           \
        keytype##_check,                                \
        keytype##_adjust,                               \
        keytype##_free

/*
 * MAKE_DECODER is the single driver for creating OSSL_DISPATCH tables.
 * It takes the following arguments:
 *
 * keytype_name The implementation key type as a string.
 * keytype      The implementation key type.  This must correspond exactly
 *              to our existing keymgmt keytype names...  in other words,
 *              there must exist an ossl_##keytype##_keymgmt_functions.
 * type         The type name for the set of functions that implement the
 *              decoder for the key type.  This isn't necessarily the same
 *              as keytype.  For example, the key types ed25519, ed448,
 *              x25519 and x448 are all handled by the same functions with
 *              the common type name ecx.
 * kind         The kind of support to implement.  This translates into
 *              the DO_##kind macros above, to populate the keytype_desc_st
 *              structure.
 */
#define MAKE_DECODER(keytype_name, keytype, type, kind)                 \
    static const struct keytype_desc_st kind##_##keytype##_desc =       \
        { keytype_name, ossl_##keytype##_keymgmt_functions,             \
          DO_##kind(keytype) };                                         \
                                                                        \
    static OSSL_FUNC_decoder_newctx_fn kind##_der2##keytype##_newctx;   \
                                                                        \
    static void *kind##_der2##keytype##_newctx(void *provctx)           \
    {                                                                   \
        return der2key_newctx(provctx, &kind##_##keytype##_desc);       \
    }                                                                   \
    static int kind##_der2##keytype##_does_selection(void *provctx,     \
                                                     int selection)     \
    {                                                                   \
        return der2key_check_selection(selection,                       \
                                       &kind##_##keytype##_desc);       \
    }                                                                   \
    const OSSL_DISPATCH                                                 \
    ossl_##kind##_der_to_##keytype##_decoder_functions[] = {            \
        { OSSL_FUNC_DECODER_NEWCTX,                                     \
          (void (*)(void))kind##_der2##keytype##_newctx },              \
        { OSSL_FUNC_DECODER_FREECTX,                                    \
          (void (*)(void))der2key_freectx },                            \
        { OSSL_FUNC_DECODER_DOES_SELECTION,                             \
          (void (*)(void))kind##_der2##keytype##_does_selection },      \
        { OSSL_FUNC_DECODER_DECODE,                                     \
          (void (*)(void))der2key_decode },                             \
        { OSSL_FUNC_DECODER_EXPORT_OBJECT,                              \
          (void (*)(void))der2key_export_object },                      \
        { 0, NULL }                                                     \
    }

#ifndef OPENSSL_NO_DH
MAKE_DECODER("DH", dh, dh, PrivateKeyInfo);
MAKE_DECODER("DH", dh, dh, SubjectPublicKeyInfo);
MAKE_DECODER("DH", dh, dh, type_specific_params);
MAKE_DECODER("DH", dh, dh, DH);
MAKE_DECODER("DHX", dhx, dhx, PrivateKeyInfo);
MAKE_DECODER("DHX", dhx, dhx, SubjectPublicKeyInfo);
MAKE_DECODER("DHX", dhx, dhx, type_specific_params);
MAKE_DECODER("DHX", dhx, dhx, DHX);
#endif
#ifndef OPENSSL_NO_DSA
MAKE_DECODER("DSA", dsa, dsa, PrivateKeyInfo);
MAKE_DECODER("DSA", dsa, dsa, SubjectPublicKeyInfo);
MAKE_DECODER("DSA", dsa, dsa, type_specific);
MAKE_DECODER("DSA", dsa, dsa, DSA);
#endif
#ifndef OPENSSL_NO_EC
MAKE_DECODER("EC", ec, ec, PrivateKeyInfo);
MAKE_DECODER("EC", ec, ec, SubjectPublicKeyInfo);
MAKE_DECODER("EC", ec, ec, type_specific_no_pub);
MAKE_DECODER("EC", ec, ec, EC);
MAKE_DECODER("X25519", x25519, ecx, PrivateKeyInfo);
MAKE_DECODER("X25519", x25519, ecx, SubjectPublicKeyInfo);
MAKE_DECODER("X448", x448, ecx, PrivateKeyInfo);
MAKE_DECODER("X448", x448, ecx, SubjectPublicKeyInfo);
MAKE_DECODER("ED25519", ed25519, ecx, PrivateKeyInfo);
MAKE_DECODER("ED25519", ed25519, ecx, SubjectPublicKeyInfo);
MAKE_DECODER("ED448", ed448, ecx, PrivateKeyInfo);
MAKE_DECODER("ED448", ed448, ecx, SubjectPublicKeyInfo);
# ifndef OPENSSL_NO_SM2
MAKE_DECODER("SM2", sm2, ec, PrivateKeyInfo);
MAKE_DECODER("SM2", sm2, ec, SubjectPublicKeyInfo);
# endif
#endif
MAKE_DECODER("RSA", rsa, rsa, PrivateKeyInfo);
MAKE_DECODER("RSA", rsa, rsa, SubjectPublicKeyInfo);
MAKE_DECODER("RSA", rsa, rsa, type_specific_keypair);
MAKE_DECODER("RSA", rsa, rsa, RSA);
MAKE_DECODER("RSA-PSS", rsapss, rsapss, PrivateKeyInfo);
MAKE_DECODER("RSA-PSS", rsapss, rsapss, SubjectPublicKeyInfo);
