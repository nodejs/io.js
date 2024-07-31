/*
 * Copyright 2019-2023 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * DES low level APIs are deprecated for public use, but still ok for internal
 * use.
 */
#include "internal/deprecated.h"

#include <openssl/rand.h>
#include <openssl/proverr.h>
#include "prov/ciphercommon.h"
#include "cipher_tdes.h"
#include "prov/implementations.h"
#include "prov/providercommon.h"

void *ossl_tdes_newctx(void *provctx, int mode, size_t kbits, size_t blkbits,
                       size_t ivbits, uint64_t flags, const PROV_CIPHER_HW *hw)
{
    PROV_TDES_CTX *tctx;

    if (!ossl_prov_is_running())
        return NULL;

    tctx = OPENSSL_zalloc(sizeof(*tctx));
    if (tctx != NULL)
        ossl_cipher_generic_initkey(tctx, kbits, blkbits, ivbits, mode, flags,
                                    hw, provctx);
    return tctx;
}

void *ossl_tdes_dupctx(void *ctx)
{
    PROV_TDES_CTX *in = (PROV_TDES_CTX *)ctx;
    PROV_TDES_CTX *ret;

    if (!ossl_prov_is_running())
        return NULL;

    ret = OPENSSL_malloc(sizeof(*ret));
    if (ret == NULL) {
        ERR_raise(ERR_LIB_PROV, ERR_R_MALLOC_FAILURE);
        return NULL;
    }
    in->base.hw->copyctx(&ret->base, &in->base);

    return ret;
}

void ossl_tdes_freectx(void *vctx)
{
    PROV_TDES_CTX *ctx = (PROV_TDES_CTX *)vctx;

    ossl_cipher_generic_reset_ctx((PROV_CIPHER_CTX *)vctx);
    OPENSSL_clear_free(ctx,  sizeof(*ctx));
}

static int tdes_init(void *vctx, const unsigned char *key, size_t keylen,
                     const unsigned char *iv, size_t ivlen,
                     const OSSL_PARAM params[], int enc)
{
    PROV_CIPHER_CTX *ctx = (PROV_CIPHER_CTX *)vctx;

    if (!ossl_prov_is_running())
        return 0;

    ctx->num = 0;
    ctx->bufsz = 0;
    ctx->enc = enc;

    if (iv != NULL) {
        if (!ossl_cipher_generic_initiv(ctx, iv, ivlen))
            return 0;
    } else if (ctx->iv_set
               && (ctx->mode == EVP_CIPH_CBC_MODE
                   || ctx->mode == EVP_CIPH_CFB_MODE
                   || ctx->mode == EVP_CIPH_OFB_MODE)) {
        /* reset IV to keep compatibility with 1.1.1 */
        memcpy(ctx->iv, ctx->oiv, ctx->ivlen);
    }

    if (key != NULL) {
        if (keylen != ctx->keylen) {
            ERR_raise(ERR_LIB_PROV, PROV_R_INVALID_KEY_LENGTH);
            return 0;
        }
        if (!ctx->hw->init(ctx, key, ctx->keylen))
            return 0;
        ctx->key_set = 1;
    }
    return ossl_cipher_generic_set_ctx_params(ctx, params);
}

int ossl_tdes_einit(void *vctx, const unsigned char *key, size_t keylen,
                    const unsigned char *iv, size_t ivlen,
                    const OSSL_PARAM params[])
{
    return tdes_init(vctx, key, keylen, iv, ivlen, params, 1);
}

int ossl_tdes_dinit(void *vctx, const unsigned char *key, size_t keylen,
                    const unsigned char *iv, size_t ivlen,
                    const OSSL_PARAM params[])
{
    return tdes_init(vctx, key, keylen, iv, ivlen, params, 0);
}

CIPHER_DEFAULT_GETTABLE_CTX_PARAMS_START(ossl_tdes)
    OSSL_PARAM_octet_string(OSSL_CIPHER_PARAM_RANDOM_KEY, NULL, 0),
CIPHER_DEFAULT_GETTABLE_CTX_PARAMS_END(ossl_tdes)

static int tdes_generatekey(PROV_CIPHER_CTX *ctx, void *ptr)
{

    DES_cblock *deskey = ptr;
    size_t kl = ctx->keylen;

    if (kl == 0 || RAND_priv_bytes_ex(ctx->libctx, ptr, kl, 0) <= 0)
        return 0;
    DES_set_odd_parity(deskey);
    if (kl >= 16) {
        DES_set_odd_parity(deskey + 1);
        if (kl >= 24)
            DES_set_odd_parity(deskey + 2);
    }
    return 1;
}

int ossl_tdes_get_ctx_params(void *vctx, OSSL_PARAM params[])
{
    PROV_CIPHER_CTX  *ctx = (PROV_CIPHER_CTX *)vctx;
    OSSL_PARAM *p;

    if (!ossl_cipher_generic_get_ctx_params(vctx, params))
        return 0;

    p = OSSL_PARAM_locate(params, OSSL_CIPHER_PARAM_RANDOM_KEY);
    if (p != NULL && !tdes_generatekey(ctx, p->data)) {
        ERR_raise(ERR_LIB_PROV, PROV_R_FAILED_TO_GENERATE_KEY);
        return 0;
    }
    return 1;
}
