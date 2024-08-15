/*
 * Copyright 1995-2024 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <openssl/objects.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/core_names.h>
#include <openssl/ocsp.h>
#include <openssl/conf.h>
#include <openssl/x509v3.h>
#include <openssl/dh.h>
#include <openssl/bn.h>
#include <openssl/provider.h>
#include <openssl/param_build.h>
#include "internal/nelem.h"
#include "internal/sizes.h"
#include "internal/tlsgroups.h"
#include "internal/cryptlib.h"
#include "ssl_local.h"
#include <openssl/ct.h>

static const SIGALG_LOOKUP *find_sig_alg(SSL *s, X509 *x, EVP_PKEY *pkey);
static int tls12_sigalg_allowed(const SSL *s, int op, const SIGALG_LOOKUP *lu);

SSL3_ENC_METHOD const TLSv1_enc_data = {
    tls1_enc,
    tls1_mac,
    tls1_setup_key_block,
    tls1_generate_master_secret,
    tls1_change_cipher_state,
    tls1_final_finish_mac,
    TLS_MD_CLIENT_FINISH_CONST, TLS_MD_CLIENT_FINISH_CONST_SIZE,
    TLS_MD_SERVER_FINISH_CONST, TLS_MD_SERVER_FINISH_CONST_SIZE,
    tls1_alert_code,
    tls1_export_keying_material,
    0,
    ssl3_set_handshake_header,
    tls_close_construct_packet,
    ssl3_handshake_write
};

SSL3_ENC_METHOD const TLSv1_1_enc_data = {
    tls1_enc,
    tls1_mac,
    tls1_setup_key_block,
    tls1_generate_master_secret,
    tls1_change_cipher_state,
    tls1_final_finish_mac,
    TLS_MD_CLIENT_FINISH_CONST, TLS_MD_CLIENT_FINISH_CONST_SIZE,
    TLS_MD_SERVER_FINISH_CONST, TLS_MD_SERVER_FINISH_CONST_SIZE,
    tls1_alert_code,
    tls1_export_keying_material,
    SSL_ENC_FLAG_EXPLICIT_IV,
    ssl3_set_handshake_header,
    tls_close_construct_packet,
    ssl3_handshake_write
};

SSL3_ENC_METHOD const TLSv1_2_enc_data = {
    tls1_enc,
    tls1_mac,
    tls1_setup_key_block,
    tls1_generate_master_secret,
    tls1_change_cipher_state,
    tls1_final_finish_mac,
    TLS_MD_CLIENT_FINISH_CONST, TLS_MD_CLIENT_FINISH_CONST_SIZE,
    TLS_MD_SERVER_FINISH_CONST, TLS_MD_SERVER_FINISH_CONST_SIZE,
    tls1_alert_code,
    tls1_export_keying_material,
    SSL_ENC_FLAG_EXPLICIT_IV | SSL_ENC_FLAG_SIGALGS | SSL_ENC_FLAG_SHA256_PRF
        | SSL_ENC_FLAG_TLS1_2_CIPHERS,
    ssl3_set_handshake_header,
    tls_close_construct_packet,
    ssl3_handshake_write
};

SSL3_ENC_METHOD const TLSv1_3_enc_data = {
    tls13_enc,
    tls1_mac,
    tls13_setup_key_block,
    tls13_generate_master_secret,
    tls13_change_cipher_state,
    tls13_final_finish_mac,
    TLS_MD_CLIENT_FINISH_CONST, TLS_MD_CLIENT_FINISH_CONST_SIZE,
    TLS_MD_SERVER_FINISH_CONST, TLS_MD_SERVER_FINISH_CONST_SIZE,
    tls13_alert_code,
    tls13_export_keying_material,
    SSL_ENC_FLAG_SIGALGS | SSL_ENC_FLAG_SHA256_PRF,
    ssl3_set_handshake_header,
    tls_close_construct_packet,
    ssl3_handshake_write
};

long tls1_default_timeout(void)
{
    /*
     * 2 hours, the 24 hours mentioned in the TLSv1 spec is way too long for
     * http, the cache would over fill
     */
    return (60 * 60 * 2);
}

int tls1_new(SSL *s)
{
    if (!ssl3_new(s))
        return 0;
    if (!s->method->ssl_clear(s))
        return 0;

    return 1;
}

void tls1_free(SSL *s)
{
    OPENSSL_free(s->ext.session_ticket);
    ssl3_free(s);
}

int tls1_clear(SSL *s)
{
    if (!ssl3_clear(s))
        return 0;

    if (s->method->version == TLS_ANY_VERSION)
        s->version = TLS_MAX_VERSION_INTERNAL;
    else
        s->version = s->method->version;

    return 1;
}

/* Legacy NID to group_id mapping. Only works for groups we know about */
static struct {
    int nid;
    uint16_t group_id;
} nid_to_group[] = {
    {NID_sect163k1, OSSL_TLS_GROUP_ID_sect163k1},
    {NID_sect163r1, OSSL_TLS_GROUP_ID_sect163r1},
    {NID_sect163r2, OSSL_TLS_GROUP_ID_sect163r2},
    {NID_sect193r1, OSSL_TLS_GROUP_ID_sect193r1},
    {NID_sect193r2, OSSL_TLS_GROUP_ID_sect193r2},
    {NID_sect233k1, OSSL_TLS_GROUP_ID_sect233k1},
    {NID_sect233r1, OSSL_TLS_GROUP_ID_sect233r1},
    {NID_sect239k1, OSSL_TLS_GROUP_ID_sect239k1},
    {NID_sect283k1, OSSL_TLS_GROUP_ID_sect283k1},
    {NID_sect283r1, OSSL_TLS_GROUP_ID_sect283r1},
    {NID_sect409k1, OSSL_TLS_GROUP_ID_sect409k1},
    {NID_sect409r1, OSSL_TLS_GROUP_ID_sect409r1},
    {NID_sect571k1, OSSL_TLS_GROUP_ID_sect571k1},
    {NID_sect571r1, OSSL_TLS_GROUP_ID_sect571r1},
    {NID_secp160k1, OSSL_TLS_GROUP_ID_secp160k1},
    {NID_secp160r1, OSSL_TLS_GROUP_ID_secp160r1},
    {NID_secp160r2, OSSL_TLS_GROUP_ID_secp160r2},
    {NID_secp192k1, OSSL_TLS_GROUP_ID_secp192k1},
    {NID_X9_62_prime192v1, OSSL_TLS_GROUP_ID_secp192r1},
    {NID_secp224k1, OSSL_TLS_GROUP_ID_secp224k1},
    {NID_secp224r1, OSSL_TLS_GROUP_ID_secp224r1},
    {NID_secp256k1, OSSL_TLS_GROUP_ID_secp256k1},
    {NID_X9_62_prime256v1, OSSL_TLS_GROUP_ID_secp256r1},
    {NID_secp384r1, OSSL_TLS_GROUP_ID_secp384r1},
    {NID_secp521r1, OSSL_TLS_GROUP_ID_secp521r1},
    {NID_brainpoolP256r1, OSSL_TLS_GROUP_ID_brainpoolP256r1},
    {NID_brainpoolP384r1, OSSL_TLS_GROUP_ID_brainpoolP384r1},
    {NID_brainpoolP512r1, OSSL_TLS_GROUP_ID_brainpoolP512r1},
    {EVP_PKEY_X25519, OSSL_TLS_GROUP_ID_x25519},
    {EVP_PKEY_X448, OSSL_TLS_GROUP_ID_x448},
    {NID_id_tc26_gost_3410_2012_256_paramSetA, 0x0022},
    {NID_id_tc26_gost_3410_2012_256_paramSetB, 0x0023},
    {NID_id_tc26_gost_3410_2012_256_paramSetC, 0x0024},
    {NID_id_tc26_gost_3410_2012_256_paramSetD, 0x0025},
    {NID_id_tc26_gost_3410_2012_512_paramSetA, 0x0026},
    {NID_id_tc26_gost_3410_2012_512_paramSetB, 0x0027},
    {NID_id_tc26_gost_3410_2012_512_paramSetC, 0x0028},
    {NID_ffdhe2048, OSSL_TLS_GROUP_ID_ffdhe2048},
    {NID_ffdhe3072, OSSL_TLS_GROUP_ID_ffdhe3072},
    {NID_ffdhe4096, OSSL_TLS_GROUP_ID_ffdhe4096},
    {NID_ffdhe6144, OSSL_TLS_GROUP_ID_ffdhe6144},
    {NID_ffdhe8192, OSSL_TLS_GROUP_ID_ffdhe8192}
};

static const unsigned char ecformats_default[] = {
    TLSEXT_ECPOINTFORMAT_uncompressed,
    TLSEXT_ECPOINTFORMAT_ansiX962_compressed_prime,
    TLSEXT_ECPOINTFORMAT_ansiX962_compressed_char2
};

/* The default curves */
static const uint16_t supported_groups_default[] = {
    29,                      /* X25519 (29) */
    23,                      /* secp256r1 (23) */
    30,                      /* X448 (30) */
    25,                      /* secp521r1 (25) */
    24,                      /* secp384r1 (24) */
    34,                      /* GC256A (34) */
    35,                      /* GC256B (35) */
    36,                      /* GC256C (36) */
    37,                      /* GC256D (37) */
    38,                      /* GC512A (38) */
    39,                      /* GC512B (39) */
    40,                      /* GC512C (40) */
    0x100,                   /* ffdhe2048 (0x100) */
    0x101,                   /* ffdhe3072 (0x101) */
    0x102,                   /* ffdhe4096 (0x102) */
    0x103,                   /* ffdhe6144 (0x103) */
    0x104,                   /* ffdhe8192 (0x104) */
};

static const uint16_t suiteb_curves[] = {
    TLSEXT_curve_P_256,
    TLSEXT_curve_P_384
};

struct provider_group_data_st {
    SSL_CTX *ctx;
    OSSL_PROVIDER *provider;
};

#define TLS_GROUP_LIST_MALLOC_BLOCK_SIZE        10
static OSSL_CALLBACK add_provider_groups;
static int add_provider_groups(const OSSL_PARAM params[], void *data)
{
    struct provider_group_data_st *pgd = data;
    SSL_CTX *ctx = pgd->ctx;
    OSSL_PROVIDER *provider = pgd->provider;
    const OSSL_PARAM *p;
    TLS_GROUP_INFO *ginf = NULL;
    EVP_KEYMGMT *keymgmt;
    unsigned int gid;
    unsigned int is_kem = 0;
    int ret = 0;

    if (ctx->group_list_max_len == ctx->group_list_len) {
        TLS_GROUP_INFO *tmp = NULL;

        if (ctx->group_list_max_len == 0)
            tmp = OPENSSL_malloc(sizeof(TLS_GROUP_INFO)
                                 * TLS_GROUP_LIST_MALLOC_BLOCK_SIZE);
        else
            tmp = OPENSSL_realloc(ctx->group_list,
                                  (ctx->group_list_max_len
                                   + TLS_GROUP_LIST_MALLOC_BLOCK_SIZE)
                                  * sizeof(TLS_GROUP_INFO));
        if (tmp == NULL) {
            ERR_raise(ERR_LIB_SSL, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        ctx->group_list = tmp;
        memset(tmp + ctx->group_list_max_len,
               0,
               sizeof(TLS_GROUP_INFO) * TLS_GROUP_LIST_MALLOC_BLOCK_SIZE);
        ctx->group_list_max_len += TLS_GROUP_LIST_MALLOC_BLOCK_SIZE;
    }

    ginf = &ctx->group_list[ctx->group_list_len];

    p = OSSL_PARAM_locate_const(params, OSSL_CAPABILITY_TLS_GROUP_NAME);
    if (p == NULL || p->data_type != OSSL_PARAM_UTF8_STRING) {
        ERR_raise(ERR_LIB_SSL, ERR_R_PASSED_INVALID_ARGUMENT);
        goto err;
    }
    ginf->tlsname = OPENSSL_strdup(p->data);
    if (ginf->tlsname == NULL) {
        ERR_raise(ERR_LIB_SSL, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    p = OSSL_PARAM_locate_const(params, OSSL_CAPABILITY_TLS_GROUP_NAME_INTERNAL);
    if (p == NULL || p->data_type != OSSL_PARAM_UTF8_STRING) {
        ERR_raise(ERR_LIB_SSL, ERR_R_PASSED_INVALID_ARGUMENT);
        goto err;
    }
    ginf->realname = OPENSSL_strdup(p->data);
    if (ginf->realname == NULL) {
        ERR_raise(ERR_LIB_SSL, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    p = OSSL_PARAM_locate_const(params, OSSL_CAPABILITY_TLS_GROUP_ID);
    if (p == NULL || !OSSL_PARAM_get_uint(p, &gid) || gid > UINT16_MAX) {
        ERR_raise(ERR_LIB_SSL, ERR_R_PASSED_INVALID_ARGUMENT);
        goto err;
    }
    ginf->group_id = (uint16_t)gid;

    p = OSSL_PARAM_locate_const(params, OSSL_CAPABILITY_TLS_GROUP_ALG);
    if (p == NULL || p->data_type != OSSL_PARAM_UTF8_STRING) {
        ERR_raise(ERR_LIB_SSL, ERR_R_PASSED_INVALID_ARGUMENT);
        goto err;
    }
    ginf->algorithm = OPENSSL_strdup(p->data);
    if (ginf->algorithm == NULL) {
        ERR_raise(ERR_LIB_SSL, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    p = OSSL_PARAM_locate_const(params, OSSL_CAPABILITY_TLS_GROUP_SECURITY_BITS);
    if (p == NULL || !OSSL_PARAM_get_uint(p, &ginf->secbits)) {
        ERR_raise(ERR_LIB_SSL, ERR_R_PASSED_INVALID_ARGUMENT);
        goto err;
    }

    p = OSSL_PARAM_locate_const(params, OSSL_CAPABILITY_TLS_GROUP_IS_KEM);
    if (p != NULL && (!OSSL_PARAM_get_uint(p, &is_kem) || is_kem > 1)) {
        ERR_raise(ERR_LIB_SSL, ERR_R_PASSED_INVALID_ARGUMENT);
        goto err;
    }
    ginf->is_kem = 1 & is_kem;

    p = OSSL_PARAM_locate_const(params, OSSL_CAPABILITY_TLS_GROUP_MIN_TLS);
    if (p == NULL || !OSSL_PARAM_get_int(p, &ginf->mintls)) {
        ERR_raise(ERR_LIB_SSL, ERR_R_PASSED_INVALID_ARGUMENT);
        goto err;
    }

    p = OSSL_PARAM_locate_const(params, OSSL_CAPABILITY_TLS_GROUP_MAX_TLS);
    if (p == NULL || !OSSL_PARAM_get_int(p, &ginf->maxtls)) {
        ERR_raise(ERR_LIB_SSL, ERR_R_PASSED_INVALID_ARGUMENT);
        goto err;
    }

    p = OSSL_PARAM_locate_const(params, OSSL_CAPABILITY_TLS_GROUP_MIN_DTLS);
    if (p == NULL || !OSSL_PARAM_get_int(p, &ginf->mindtls)) {
        ERR_raise(ERR_LIB_SSL, ERR_R_PASSED_INVALID_ARGUMENT);
        goto err;
    }

    p = OSSL_PARAM_locate_const(params, OSSL_CAPABILITY_TLS_GROUP_MAX_DTLS);
    if (p == NULL || !OSSL_PARAM_get_int(p, &ginf->maxdtls)) {
        ERR_raise(ERR_LIB_SSL, ERR_R_PASSED_INVALID_ARGUMENT);
        goto err;
    }
    /*
     * Now check that the algorithm is actually usable for our property query
     * string. Regardless of the result we still return success because we have
     * successfully processed this group, even though we may decide not to use
     * it.
     */
    ret = 1;
    ERR_set_mark();
    keymgmt = EVP_KEYMGMT_fetch(ctx->libctx, ginf->algorithm, ctx->propq);
    if (keymgmt != NULL) {
        /*
         * We have successfully fetched the algorithm - however if the provider
         * doesn't match this one then we ignore it.
         *
         * Note: We're cheating a little here. Technically if the same algorithm
         * is available from more than one provider then it is undefined which
         * implementation you will get back. Theoretically this could be
         * different every time...we assume here that you'll always get the
         * same one back if you repeat the exact same fetch. Is this a reasonable
         * assumption to make (in which case perhaps we should document this
         * behaviour)?
         */
        if (EVP_KEYMGMT_get0_provider(keymgmt) == provider) {
            /* We have a match - so we will use this group */
            ctx->group_list_len++;
            ginf = NULL;
        }
        EVP_KEYMGMT_free(keymgmt);
    }
    ERR_pop_to_mark();
 err:
    if (ginf != NULL) {
        OPENSSL_free(ginf->tlsname);
        OPENSSL_free(ginf->realname);
        OPENSSL_free(ginf->algorithm);
        ginf->algorithm = ginf->tlsname = ginf->realname = NULL;
    }
    return ret;
}

static int discover_provider_groups(OSSL_PROVIDER *provider, void *vctx)
{
    struct provider_group_data_st pgd;

    pgd.ctx = vctx;
    pgd.provider = provider;
    return OSSL_PROVIDER_get_capabilities(provider, "TLS-GROUP",
                                          add_provider_groups, &pgd);
}

int ssl_load_groups(SSL_CTX *ctx)
{
    size_t i, j, num_deflt_grps = 0;
    uint16_t tmp_supp_groups[OSSL_NELEM(supported_groups_default)];

    if (!OSSL_PROVIDER_do_all(ctx->libctx, discover_provider_groups, ctx))
        return 0;

    for (i = 0; i < OSSL_NELEM(supported_groups_default); i++) {
        for (j = 0; j < ctx->group_list_len; j++) {
            if (ctx->group_list[j].group_id == supported_groups_default[i]) {
                tmp_supp_groups[num_deflt_grps++] = ctx->group_list[j].group_id;
                break;
            }
        }
    }

    if (num_deflt_grps == 0)
        return 1;

    ctx->ext.supported_groups_default
        = OPENSSL_malloc(sizeof(uint16_t) * num_deflt_grps);

    if (ctx->ext.supported_groups_default == NULL) {
        ERR_raise(ERR_LIB_SSL, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    memcpy(ctx->ext.supported_groups_default,
           tmp_supp_groups,
           num_deflt_grps * sizeof(tmp_supp_groups[0]));
    ctx->ext.supported_groups_default_len = num_deflt_grps;

    return 1;
}

static uint16_t tls1_group_name2id(SSL_CTX *ctx, const char *name)
{
    size_t i;

    for (i = 0; i < ctx->group_list_len; i++) {
        if (strcmp(ctx->group_list[i].tlsname, name) == 0
                || strcmp(ctx->group_list[i].realname, name) == 0)
            return ctx->group_list[i].group_id;
    }

    return 0;
}

const TLS_GROUP_INFO *tls1_group_id_lookup(SSL_CTX *ctx, uint16_t group_id)
{
    size_t i;

    for (i = 0; i < ctx->group_list_len; i++) {
        if (ctx->group_list[i].group_id == group_id)
            return &ctx->group_list[i];
    }

    return NULL;
}

int tls1_group_id2nid(uint16_t group_id, int include_unknown)
{
    size_t i;

    if (group_id == 0)
        return NID_undef;

    /*
     * Return well known Group NIDs - for backwards compatibility. This won't
     * work for groups we don't know about.
     */
    for (i = 0; i < OSSL_NELEM(nid_to_group); i++)
    {
        if (nid_to_group[i].group_id == group_id)
            return nid_to_group[i].nid;
    }
    if (!include_unknown)
        return NID_undef;
    return TLSEXT_nid_unknown | (int)group_id;
}

uint16_t tls1_nid2group_id(int nid)
{
    size_t i;

    /*
     * Return well known Group ids - for backwards compatibility. This won't
     * work for groups we don't know about.
     */
    for (i = 0; i < OSSL_NELEM(nid_to_group); i++)
    {
        if (nid_to_group[i].nid == nid)
            return nid_to_group[i].group_id;
    }

    return 0;
}

/*
 * Set *pgroups to the supported groups list and *pgroupslen to
 * the number of groups supported.
 */
void tls1_get_supported_groups(SSL *s, const uint16_t **pgroups,
                               size_t *pgroupslen)
{
    /* For Suite B mode only include P-256, P-384 */
    switch (tls1_suiteb(s)) {
    case SSL_CERT_FLAG_SUITEB_128_LOS:
        *pgroups = suiteb_curves;
        *pgroupslen = OSSL_NELEM(suiteb_curves);
        break;

    case SSL_CERT_FLAG_SUITEB_128_LOS_ONLY:
        *pgroups = suiteb_curves;
        *pgroupslen = 1;
        break;

    case SSL_CERT_FLAG_SUITEB_192_LOS:
        *pgroups = suiteb_curves + 1;
        *pgroupslen = 1;
        break;

    default:
        if (s->ext.supportedgroups == NULL) {
            *pgroups = s->ctx->ext.supported_groups_default;
            *pgroupslen = s->ctx->ext.supported_groups_default_len;
        } else {
            *pgroups = s->ext.supportedgroups;
            *pgroupslen = s->ext.supportedgroups_len;
        }
        break;
    }
}

int tls_valid_group(SSL *s, uint16_t group_id, int minversion, int maxversion,
                    int isec, int *okfortls13)
{
    const TLS_GROUP_INFO *ginfo = tls1_group_id_lookup(s->ctx, group_id);
    int ret;

    if (okfortls13 != NULL)
        *okfortls13 = 0;

    if (ginfo == NULL)
        return 0;

    if (SSL_IS_DTLS(s)) {
        if (ginfo->mindtls < 0 || ginfo->maxdtls < 0)
            return 0;
        if (ginfo->maxdtls == 0)
            ret = 1;
        else
            ret = DTLS_VERSION_LE(minversion, ginfo->maxdtls);
        if (ginfo->mindtls > 0)
            ret &= DTLS_VERSION_GE(maxversion, ginfo->mindtls);
    } else {
        if (ginfo->mintls < 0 || ginfo->maxtls < 0)
            return 0;
        if (ginfo->maxtls == 0)
            ret = 1;
        else
            ret = (minversion <= ginfo->maxtls);
        if (ginfo->mintls > 0)
            ret &= (maxversion >= ginfo->mintls);
        if (ret && okfortls13 != NULL && maxversion == TLS1_3_VERSION)
            *okfortls13 = (ginfo->maxtls == 0)
                          || (ginfo->maxtls >= TLS1_3_VERSION);
    }
    ret &= !isec
           || strcmp(ginfo->algorithm, "EC") == 0
           || strcmp(ginfo->algorithm, "X25519") == 0
           || strcmp(ginfo->algorithm, "X448") == 0;

    return ret;
}

/* See if group is allowed by security callback */
int tls_group_allowed(SSL *s, uint16_t group, int op)
{
    const TLS_GROUP_INFO *ginfo = tls1_group_id_lookup(s->ctx, group);
    unsigned char gtmp[2];

    if (ginfo == NULL)
        return 0;

    gtmp[0] = group >> 8;
    gtmp[1] = group & 0xff;
    return ssl_security(s, op, ginfo->secbits,
                        tls1_group_id2nid(ginfo->group_id, 0), (void *)gtmp);
}

/* Return 1 if "id" is in "list" */
static int tls1_in_list(uint16_t id, const uint16_t *list, size_t listlen)
{
    size_t i;
    for (i = 0; i < listlen; i++)
        if (list[i] == id)
            return 1;
    return 0;
}

/*-
 * For nmatch >= 0, return the id of the |nmatch|th shared group or 0
 * if there is no match.
 * For nmatch == -1, return number of matches
 * For nmatch == -2, return the id of the group to use for
 * a tmp key, or 0 if there is no match.
 */
uint16_t tls1_shared_group(SSL *s, int nmatch)
{
    const uint16_t *pref, *supp;
    size_t num_pref, num_supp, i;
    int k;
    SSL_CTX *ctx = s->ctx;

    /* Can't do anything on client side */
    if (s->server == 0)
        return 0;
    if (nmatch == -2) {
        if (tls1_suiteb(s)) {
            /*
             * For Suite B ciphersuite determines curve: we already know
             * these are acceptable due to previous checks.
             */
            unsigned long cid = s->s3.tmp.new_cipher->id;

            if (cid == TLS1_CK_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256)
                return TLSEXT_curve_P_256;
            if (cid == TLS1_CK_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384)
                return TLSEXT_curve_P_384;
            /* Should never happen */
            return 0;
        }
        /* If not Suite B just return first preference shared curve */
        nmatch = 0;
    }
    /*
     * If server preference set, our groups are the preference order
     * otherwise peer decides.
     */
    if (s->options & SSL_OP_CIPHER_SERVER_PREFERENCE) {
        tls1_get_supported_groups(s, &pref, &num_pref);
        tls1_get_peer_groups(s, &supp, &num_supp);
    } else {
        tls1_get_peer_groups(s, &pref, &num_pref);
        tls1_get_supported_groups(s, &supp, &num_supp);
    }

    for (k = 0, i = 0; i < num_pref; i++) {
        uint16_t id = pref[i];
        const TLS_GROUP_INFO *inf;

        if (!tls1_in_list(id, supp, num_supp)
                || !tls_group_allowed(s, id, SSL_SECOP_CURVE_SHARED))
            continue;
        inf = tls1_group_id_lookup(ctx, id);
        if (!ossl_assert(inf != NULL))
            return 0;
        if (SSL_IS_DTLS(s)) {
            if (inf->maxdtls == -1)
                continue;
            if ((inf->mindtls != 0 && DTLS_VERSION_LT(s->version, inf->mindtls))
                    || (inf->maxdtls != 0
                        && DTLS_VERSION_GT(s->version, inf->maxdtls)))
                continue;
        } else {
            if (inf->maxtls == -1)
                continue;
            if ((inf->mintls != 0 && s->version < inf->mintls)
                    || (inf->maxtls != 0 && s->version > inf->maxtls))
                continue;
        }

        if (nmatch == k)
            return id;
         k++;
    }
    if (nmatch == -1)
        return k;
    /* Out of range (nmatch > k). */
    return 0;
}

int tls1_set_groups(uint16_t **pext, size_t *pextlen,
                    int *groups, size_t ngroups)
{
    uint16_t *glist;
    size_t i;
    /*
     * Bitmap of groups included to detect duplicates: two variables are added
     * to detect duplicates as some values are more than 32.
     */
    unsigned long *dup_list = NULL;
    unsigned long dup_list_egrp = 0;
    unsigned long dup_list_dhgrp = 0;

    if (ngroups == 0) {
        ERR_raise(ERR_LIB_SSL, SSL_R_BAD_LENGTH);
        return 0;
    }
    if ((glist = OPENSSL_malloc(ngroups * sizeof(*glist))) == NULL) {
        ERR_raise(ERR_LIB_SSL, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    for (i = 0; i < ngroups; i++) {
        unsigned long idmask;
        uint16_t id;
        id = tls1_nid2group_id(groups[i]);
        if ((id & 0x00FF) >= (sizeof(unsigned long) * 8))
            goto err;
        idmask = 1L << (id & 0x00FF);
        dup_list = (id < 0x100) ? &dup_list_egrp : &dup_list_dhgrp;
        if (!id || ((*dup_list) & idmask))
            goto err;
        *dup_list |= idmask;
        glist[i] = id;
    }
    OPENSSL_free(*pext);
    *pext = glist;
    *pextlen = ngroups;
    return 1;
err:
    OPENSSL_free(glist);
    return 0;
}

# define GROUPLIST_INCREMENT   40
# define GROUP_NAME_BUFFER_LENGTH 64
typedef struct {
    SSL_CTX *ctx;
    size_t gidcnt;
    size_t gidmax;
    uint16_t *gid_arr;
} gid_cb_st;

static int gid_cb(const char *elem, int len, void *arg)
{
    gid_cb_st *garg = arg;
    size_t i;
    uint16_t gid = 0;
    char etmp[GROUP_NAME_BUFFER_LENGTH];

    if (elem == NULL)
        return 0;
    if (garg->gidcnt == garg->gidmax) {
        uint16_t *tmp =
            OPENSSL_realloc(garg->gid_arr,
                            (garg->gidmax + GROUPLIST_INCREMENT) * sizeof(*garg->gid_arr));
        if (tmp == NULL)
            return 0;
        garg->gidmax += GROUPLIST_INCREMENT;
        garg->gid_arr = tmp;
    }
    if (len > (int)(sizeof(etmp) - 1))
        return 0;
    memcpy(etmp, elem, len);
    etmp[len] = 0;

    gid = tls1_group_name2id(garg->ctx, etmp);
    if (gid == 0) {
        ERR_raise_data(ERR_LIB_SSL, ERR_R_PASSED_INVALID_ARGUMENT,
                       "group '%s' cannot be set", etmp);
        return 0;
    }
    for (i = 0; i < garg->gidcnt; i++)
        if (garg->gid_arr[i] == gid)
            return 0;
    garg->gid_arr[garg->gidcnt++] = gid;
    return 1;
}

/* Set groups based on a colon separated list */
int tls1_set_groups_list(SSL_CTX *ctx, uint16_t **pext, size_t *pextlen,
                         const char *str)
{
    gid_cb_st gcb;
    uint16_t *tmparr;
    int ret = 0;

    gcb.gidcnt = 0;
    gcb.gidmax = GROUPLIST_INCREMENT;
    gcb.gid_arr = OPENSSL_malloc(gcb.gidmax * sizeof(*gcb.gid_arr));
    if (gcb.gid_arr == NULL)
        return 0;
    gcb.ctx = ctx;
    if (!CONF_parse_list(str, ':', 1, gid_cb, &gcb))
        goto end;
    if (pext == NULL) {
        ret = 1;
        goto end;
    }

    /*
     * gid_cb ensurse there are no duplicates so we can just go ahead and set
     * the result
     */
    tmparr = OPENSSL_memdup(gcb.gid_arr, gcb.gidcnt * sizeof(*tmparr));
    if (tmparr == NULL)
        goto end;
    OPENSSL_free(*pext);
    *pext = tmparr;
    *pextlen = gcb.gidcnt;
    ret = 1;
 end:
    OPENSSL_free(gcb.gid_arr);
    return ret;
}

/* Check a group id matches preferences */
int tls1_check_group_id(SSL *s, uint16_t group_id, int check_own_groups)
    {
    const uint16_t *groups;
    size_t groups_len;

    if (group_id == 0)
        return 0;

    /* Check for Suite B compliance */
    if (tls1_suiteb(s) && s->s3.tmp.new_cipher != NULL) {
        unsigned long cid = s->s3.tmp.new_cipher->id;

        if (cid == TLS1_CK_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256) {
            if (group_id != TLSEXT_curve_P_256)
                return 0;
        } else if (cid == TLS1_CK_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384) {
            if (group_id != TLSEXT_curve_P_384)
                return 0;
        } else {
            /* Should never happen */
            return 0;
        }
    }

    if (check_own_groups) {
        /* Check group is one of our preferences */
        tls1_get_supported_groups(s, &groups, &groups_len);
        if (!tls1_in_list(group_id, groups, groups_len))
            return 0;
    }

    if (!tls_group_allowed(s, group_id, SSL_SECOP_CURVE_CHECK))
        return 0;

    /* For clients, nothing more to check */
    if (!s->server)
        return 1;

    /* Check group is one of peers preferences */
    tls1_get_peer_groups(s, &groups, &groups_len);

    /*
     * RFC 4492 does not require the supported elliptic curves extension
     * so if it is not sent we can just choose any curve.
     * It is invalid to send an empty list in the supported groups
     * extension, so groups_len == 0 always means no extension.
     */
    if (groups_len == 0)
            return 1;
    return tls1_in_list(group_id, groups, groups_len);
}

void tls1_get_formatlist(SSL *s, const unsigned char **pformats,
                         size_t *num_formats)
{
    /*
     * If we have a custom point format list use it otherwise use default
     */
    if (s->ext.ecpointformats) {
        *pformats = s->ext.ecpointformats;
        *num_formats = s->ext.ecpointformats_len;
    } else {
        *pformats = ecformats_default;
        /* For Suite B we don't support char2 fields */
        if (tls1_suiteb(s))
            *num_formats = sizeof(ecformats_default) - 1;
        else
            *num_formats = sizeof(ecformats_default);
    }
}

/* Check a key is compatible with compression extension */
static int tls1_check_pkey_comp(SSL *s, EVP_PKEY *pkey)
{
    unsigned char comp_id;
    size_t i;
    int point_conv;

    /* If not an EC key nothing to check */
    if (!EVP_PKEY_is_a(pkey, "EC"))
        return 1;


    /* Get required compression id */
    point_conv = EVP_PKEY_get_ec_point_conv_form(pkey);
    if (point_conv == 0)
        return 0;
    if (point_conv == POINT_CONVERSION_UNCOMPRESSED) {
            comp_id = TLSEXT_ECPOINTFORMAT_uncompressed;
    } else if (SSL_IS_TLS13(s)) {
        /*
         * ec_point_formats extension is not used in TLSv1.3 so we ignore
         * this check.
         */
        return 1;
    } else {
        int field_type = EVP_PKEY_get_field_type(pkey);

        if (field_type == NID_X9_62_prime_field)
            comp_id = TLSEXT_ECPOINTFORMAT_ansiX962_compressed_prime;
        else if (field_type == NID_X9_62_characteristic_two_field)
            comp_id = TLSEXT_ECPOINTFORMAT_ansiX962_compressed_char2;
        else
            return 0;
    }
    /*
     * If point formats extension present check it, otherwise everything is
     * supported (see RFC4492).
     */
    if (s->ext.peer_ecpointformats == NULL)
        return 1;

    for (i = 0; i < s->ext.peer_ecpointformats_len; i++) {
        if (s->ext.peer_ecpointformats[i] == comp_id)
            return 1;
    }
    return 0;
}

/* Return group id of a key */
static uint16_t tls1_get_group_id(EVP_PKEY *pkey)
{
    int curve_nid = ssl_get_EC_curve_nid(pkey);

    if (curve_nid == NID_undef)
        return 0;
    return tls1_nid2group_id(curve_nid);
}

/*
 * Check cert parameters compatible with extensions: currently just checks EC
 * certificates have compatible curves and compression.
 */
static int tls1_check_cert_param(SSL *s, X509 *x, int check_ee_md)
{
    uint16_t group_id;
    EVP_PKEY *pkey;
    pkey = X509_get0_pubkey(x);
    if (pkey == NULL)
        return 0;
    /* If not EC nothing to do */
    if (!EVP_PKEY_is_a(pkey, "EC"))
        return 1;
    /* Check compression */
    if (!tls1_check_pkey_comp(s, pkey))
        return 0;
    group_id = tls1_get_group_id(pkey);
    /*
     * For a server we allow the certificate to not be in our list of supported
     * groups.
     */
    if (!tls1_check_group_id(s, group_id, !s->server))
        return 0;
    /*
     * Special case for suite B. We *MUST* sign using SHA256+P-256 or
     * SHA384+P-384.
     */
    if (check_ee_md && tls1_suiteb(s)) {
        int check_md;
        size_t i;

        /* Check to see we have necessary signing algorithm */
        if (group_id == TLSEXT_curve_P_256)
            check_md = NID_ecdsa_with_SHA256;
        else if (group_id == TLSEXT_curve_P_384)
            check_md = NID_ecdsa_with_SHA384;
        else
            return 0;           /* Should never happen */
        for (i = 0; i < s->shared_sigalgslen; i++) {
            if (check_md == s->shared_sigalgs[i]->sigandhash)
                return 1;;
        }
        return 0;
    }
    return 1;
}

/*
 * tls1_check_ec_tmp_key - Check EC temporary key compatibility
 * @s: SSL connection
 * @cid: Cipher ID we're considering using
 *
 * Checks that the kECDHE cipher suite we're considering using
 * is compatible with the client extensions.
 *
 * Returns 0 when the cipher can't be used or 1 when it can.
 */
int tls1_check_ec_tmp_key(SSL *s, unsigned long cid)
{
    /* If not Suite B just need a shared group */
    if (!tls1_suiteb(s))
        return tls1_shared_group(s, 0) != 0;
    /*
     * If Suite B, AES128 MUST use P-256 and AES256 MUST use P-384, no other
     * curves permitted.
     */
    if (cid == TLS1_CK_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256)
        return tls1_check_group_id(s, TLSEXT_curve_P_256, 1);
    if (cid == TLS1_CK_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384)
        return tls1_check_group_id(s, TLSEXT_curve_P_384, 1);

    return 0;
}

/* Default sigalg schemes */
static const uint16_t tls12_sigalgs[] = {
    TLSEXT_SIGALG_ecdsa_secp256r1_sha256,
    TLSEXT_SIGALG_ecdsa_secp384r1_sha384,
    TLSEXT_SIGALG_ecdsa_secp521r1_sha512,
    TLSEXT_SIGALG_ed25519,
    TLSEXT_SIGALG_ed448,

    TLSEXT_SIGALG_rsa_pss_pss_sha256,
    TLSEXT_SIGALG_rsa_pss_pss_sha384,
    TLSEXT_SIGALG_rsa_pss_pss_sha512,
    TLSEXT_SIGALG_rsa_pss_rsae_sha256,
    TLSEXT_SIGALG_rsa_pss_rsae_sha384,
    TLSEXT_SIGALG_rsa_pss_rsae_sha512,

    TLSEXT_SIGALG_rsa_pkcs1_sha256,
    TLSEXT_SIGALG_rsa_pkcs1_sha384,
    TLSEXT_SIGALG_rsa_pkcs1_sha512,

    TLSEXT_SIGALG_ecdsa_sha224,
    TLSEXT_SIGALG_ecdsa_sha1,

    TLSEXT_SIGALG_rsa_pkcs1_sha224,
    TLSEXT_SIGALG_rsa_pkcs1_sha1,

    TLSEXT_SIGALG_dsa_sha224,
    TLSEXT_SIGALG_dsa_sha1,

    TLSEXT_SIGALG_dsa_sha256,
    TLSEXT_SIGALG_dsa_sha384,
    TLSEXT_SIGALG_dsa_sha512,

#ifndef OPENSSL_NO_GOST
    TLSEXT_SIGALG_gostr34102012_256_intrinsic,
    TLSEXT_SIGALG_gostr34102012_512_intrinsic,
    TLSEXT_SIGALG_gostr34102012_256_gostr34112012_256,
    TLSEXT_SIGALG_gostr34102012_512_gostr34112012_512,
    TLSEXT_SIGALG_gostr34102001_gostr3411,
#endif
};


static const uint16_t suiteb_sigalgs[] = {
    TLSEXT_SIGALG_ecdsa_secp256r1_sha256,
    TLSEXT_SIGALG_ecdsa_secp384r1_sha384
};

static const SIGALG_LOOKUP sigalg_lookup_tbl[] = {
    {"ecdsa_secp256r1_sha256", TLSEXT_SIGALG_ecdsa_secp256r1_sha256,
     NID_sha256, SSL_MD_SHA256_IDX, EVP_PKEY_EC, SSL_PKEY_ECC,
     NID_ecdsa_with_SHA256, NID_X9_62_prime256v1, 1},
    {"ecdsa_secp384r1_sha384", TLSEXT_SIGALG_ecdsa_secp384r1_sha384,
     NID_sha384, SSL_MD_SHA384_IDX, EVP_PKEY_EC, SSL_PKEY_ECC,
     NID_ecdsa_with_SHA384, NID_secp384r1, 1},
    {"ecdsa_secp521r1_sha512", TLSEXT_SIGALG_ecdsa_secp521r1_sha512,
     NID_sha512, SSL_MD_SHA512_IDX, EVP_PKEY_EC, SSL_PKEY_ECC,
     NID_ecdsa_with_SHA512, NID_secp521r1, 1},
    {"ed25519", TLSEXT_SIGALG_ed25519,
     NID_undef, -1, EVP_PKEY_ED25519, SSL_PKEY_ED25519,
     NID_undef, NID_undef, 1},
    {"ed448", TLSEXT_SIGALG_ed448,
     NID_undef, -1, EVP_PKEY_ED448, SSL_PKEY_ED448,
     NID_undef, NID_undef, 1},
    {NULL, TLSEXT_SIGALG_ecdsa_sha224,
     NID_sha224, SSL_MD_SHA224_IDX, EVP_PKEY_EC, SSL_PKEY_ECC,
     NID_ecdsa_with_SHA224, NID_undef, 1},
    {NULL, TLSEXT_SIGALG_ecdsa_sha1,
     NID_sha1, SSL_MD_SHA1_IDX, EVP_PKEY_EC, SSL_PKEY_ECC,
     NID_ecdsa_with_SHA1, NID_undef, 1},
    {"rsa_pss_rsae_sha256", TLSEXT_SIGALG_rsa_pss_rsae_sha256,
     NID_sha256, SSL_MD_SHA256_IDX, EVP_PKEY_RSA_PSS, SSL_PKEY_RSA,
     NID_undef, NID_undef, 1},
    {"rsa_pss_rsae_sha384", TLSEXT_SIGALG_rsa_pss_rsae_sha384,
     NID_sha384, SSL_MD_SHA384_IDX, EVP_PKEY_RSA_PSS, SSL_PKEY_RSA,
     NID_undef, NID_undef, 1},
    {"rsa_pss_rsae_sha512", TLSEXT_SIGALG_rsa_pss_rsae_sha512,
     NID_sha512, SSL_MD_SHA512_IDX, EVP_PKEY_RSA_PSS, SSL_PKEY_RSA,
     NID_undef, NID_undef, 1},
    {"rsa_pss_pss_sha256", TLSEXT_SIGALG_rsa_pss_pss_sha256,
     NID_sha256, SSL_MD_SHA256_IDX, EVP_PKEY_RSA_PSS, SSL_PKEY_RSA_PSS_SIGN,
     NID_undef, NID_undef, 1},
    {"rsa_pss_pss_sha384", TLSEXT_SIGALG_rsa_pss_pss_sha384,
     NID_sha384, SSL_MD_SHA384_IDX, EVP_PKEY_RSA_PSS, SSL_PKEY_RSA_PSS_SIGN,
     NID_undef, NID_undef, 1},
    {"rsa_pss_pss_sha512", TLSEXT_SIGALG_rsa_pss_pss_sha512,
     NID_sha512, SSL_MD_SHA512_IDX, EVP_PKEY_RSA_PSS, SSL_PKEY_RSA_PSS_SIGN,
     NID_undef, NID_undef, 1},
    {"rsa_pkcs1_sha256", TLSEXT_SIGALG_rsa_pkcs1_sha256,
     NID_sha256, SSL_MD_SHA256_IDX, EVP_PKEY_RSA, SSL_PKEY_RSA,
     NID_sha256WithRSAEncryption, NID_undef, 1},
    {"rsa_pkcs1_sha384", TLSEXT_SIGALG_rsa_pkcs1_sha384,
     NID_sha384, SSL_MD_SHA384_IDX, EVP_PKEY_RSA, SSL_PKEY_RSA,
     NID_sha384WithRSAEncryption, NID_undef, 1},
    {"rsa_pkcs1_sha512", TLSEXT_SIGALG_rsa_pkcs1_sha512,
     NID_sha512, SSL_MD_SHA512_IDX, EVP_PKEY_RSA, SSL_PKEY_RSA,
     NID_sha512WithRSAEncryption, NID_undef, 1},
    {"rsa_pkcs1_sha224", TLSEXT_SIGALG_rsa_pkcs1_sha224,
     NID_sha224, SSL_MD_SHA224_IDX, EVP_PKEY_RSA, SSL_PKEY_RSA,
     NID_sha224WithRSAEncryption, NID_undef, 1},
    {"rsa_pkcs1_sha1", TLSEXT_SIGALG_rsa_pkcs1_sha1,
     NID_sha1, SSL_MD_SHA1_IDX, EVP_PKEY_RSA, SSL_PKEY_RSA,
     NID_sha1WithRSAEncryption, NID_undef, 1},
    {NULL, TLSEXT_SIGALG_dsa_sha256,
     NID_sha256, SSL_MD_SHA256_IDX, EVP_PKEY_DSA, SSL_PKEY_DSA_SIGN,
     NID_dsa_with_SHA256, NID_undef, 1},
    {NULL, TLSEXT_SIGALG_dsa_sha384,
     NID_sha384, SSL_MD_SHA384_IDX, EVP_PKEY_DSA, SSL_PKEY_DSA_SIGN,
     NID_undef, NID_undef, 1},
    {NULL, TLSEXT_SIGALG_dsa_sha512,
     NID_sha512, SSL_MD_SHA512_IDX, EVP_PKEY_DSA, SSL_PKEY_DSA_SIGN,
     NID_undef, NID_undef, 1},
    {NULL, TLSEXT_SIGALG_dsa_sha224,
     NID_sha224, SSL_MD_SHA224_IDX, EVP_PKEY_DSA, SSL_PKEY_DSA_SIGN,
     NID_undef, NID_undef, 1},
    {NULL, TLSEXT_SIGALG_dsa_sha1,
     NID_sha1, SSL_MD_SHA1_IDX, EVP_PKEY_DSA, SSL_PKEY_DSA_SIGN,
     NID_dsaWithSHA1, NID_undef, 1},
#ifndef OPENSSL_NO_GOST
    {NULL, TLSEXT_SIGALG_gostr34102012_256_intrinsic,
     NID_id_GostR3411_2012_256, SSL_MD_GOST12_256_IDX,
     NID_id_GostR3410_2012_256, SSL_PKEY_GOST12_256,
     NID_undef, NID_undef, 1},
    {NULL, TLSEXT_SIGALG_gostr34102012_512_intrinsic,
     NID_id_GostR3411_2012_512, SSL_MD_GOST12_512_IDX,
     NID_id_GostR3410_2012_512, SSL_PKEY_GOST12_512,
     NID_undef, NID_undef, 1},
    {NULL, TLSEXT_SIGALG_gostr34102012_256_gostr34112012_256,
     NID_id_GostR3411_2012_256, SSL_MD_GOST12_256_IDX,
     NID_id_GostR3410_2012_256, SSL_PKEY_GOST12_256,
     NID_undef, NID_undef, 1},
    {NULL, TLSEXT_SIGALG_gostr34102012_512_gostr34112012_512,
     NID_id_GostR3411_2012_512, SSL_MD_GOST12_512_IDX,
     NID_id_GostR3410_2012_512, SSL_PKEY_GOST12_512,
     NID_undef, NID_undef, 1},
    {NULL, TLSEXT_SIGALG_gostr34102001_gostr3411,
     NID_id_GostR3411_94, SSL_MD_GOST94_IDX,
     NID_id_GostR3410_2001, SSL_PKEY_GOST01,
     NID_undef, NID_undef, 1}
#endif
};
/* Legacy sigalgs for TLS < 1.2 RSA TLS signatures */
static const SIGALG_LOOKUP legacy_rsa_sigalg = {
    "rsa_pkcs1_md5_sha1", 0,
     NID_md5_sha1, SSL_MD_MD5_SHA1_IDX,
     EVP_PKEY_RSA, SSL_PKEY_RSA,
     NID_undef, NID_undef, 1
};

/*
 * Default signature algorithm values used if signature algorithms not present.
 * From RFC5246. Note: order must match certificate index order.
 */
static const uint16_t tls_default_sigalg[] = {
    TLSEXT_SIGALG_rsa_pkcs1_sha1, /* SSL_PKEY_RSA */
    0, /* SSL_PKEY_RSA_PSS_SIGN */
    TLSEXT_SIGALG_dsa_sha1, /* SSL_PKEY_DSA_SIGN */
    TLSEXT_SIGALG_ecdsa_sha1, /* SSL_PKEY_ECC */
    TLSEXT_SIGALG_gostr34102001_gostr3411, /* SSL_PKEY_GOST01 */
    TLSEXT_SIGALG_gostr34102012_256_intrinsic, /* SSL_PKEY_GOST12_256 */
    TLSEXT_SIGALG_gostr34102012_512_intrinsic, /* SSL_PKEY_GOST12_512 */
    0, /* SSL_PKEY_ED25519 */
    0, /* SSL_PKEY_ED448 */
};

int ssl_setup_sig_algs(SSL_CTX *ctx)
{
    size_t i;
    const SIGALG_LOOKUP *lu;
    SIGALG_LOOKUP *cache
        = OPENSSL_malloc(sizeof(*lu) * OSSL_NELEM(sigalg_lookup_tbl));
    EVP_PKEY *tmpkey = EVP_PKEY_new();
    int ret = 0;

    if (cache == NULL || tmpkey == NULL)
        goto err;

    ERR_set_mark();
    for (i = 0, lu = sigalg_lookup_tbl;
         i < OSSL_NELEM(sigalg_lookup_tbl); lu++, i++) {
        EVP_PKEY_CTX *pctx;

        cache[i] = *lu;

        /*
         * Check hash is available.
         * This test is not perfect. A provider could have support
         * for a signature scheme, but not a particular hash. However the hash
         * could be available from some other loaded provider. In that case it
         * could be that the signature is available, and the hash is available
         * independently - but not as a combination. We ignore this for now.
         */
        if (lu->hash != NID_undef
                && ctx->ssl_digest_methods[lu->hash_idx] == NULL) {
            cache[i].enabled = 0;
            continue;
        }

        if (!EVP_PKEY_set_type(tmpkey, lu->sig)) {
            cache[i].enabled = 0;
            continue;
        }
        pctx = EVP_PKEY_CTX_new_from_pkey(ctx->libctx, tmpkey, ctx->propq);
        /* If unable to create pctx we assume the sig algorithm is unavailable */
        if (pctx == NULL)
            cache[i].enabled = 0;
        EVP_PKEY_CTX_free(pctx);
    }
    ERR_pop_to_mark();
    ctx->sigalg_lookup_cache = cache;
    cache = NULL;

    ret = 1;
 err:
    OPENSSL_free(cache);
    EVP_PKEY_free(tmpkey);
    return ret;
}

/* Lookup TLS signature algorithm */
static const SIGALG_LOOKUP *tls1_lookup_sigalg(const SSL *s, uint16_t sigalg)
{
    size_t i;
    const SIGALG_LOOKUP *lu;

    for (i = 0, lu = s->ctx->sigalg_lookup_cache;
         /* cache should have the same number of elements as sigalg_lookup_tbl */
         i < OSSL_NELEM(sigalg_lookup_tbl);
         lu++, i++) {
        if (lu->sigalg == sigalg) {
            if (!lu->enabled)
                return NULL;
            return lu;
        }
    }
    return NULL;
}
/* Lookup hash: return 0 if invalid or not enabled */
int tls1_lookup_md(SSL_CTX *ctx, const SIGALG_LOOKUP *lu, const EVP_MD **pmd)
{
    const EVP_MD *md;
    if (lu == NULL)
        return 0;
    /* lu->hash == NID_undef means no associated digest */
    if (lu->hash == NID_undef) {
        md = NULL;
    } else {
        md = ssl_md(ctx, lu->hash_idx);
        if (md == NULL)
            return 0;
    }
    if (pmd)
        *pmd = md;
    return 1;
}

/*
 * Check if key is large enough to generate RSA-PSS signature.
 *
 * The key must greater than or equal to 2 * hash length + 2.
 * SHA512 has a hash length of 64 bytes, which is incompatible
 * with a 128 byte (1024 bit) key.
 */
#define RSA_PSS_MINIMUM_KEY_SIZE(md) (2 * EVP_MD_get_size(md) + 2)
static int rsa_pss_check_min_key_size(SSL_CTX *ctx, const EVP_PKEY *pkey,
                                      const SIGALG_LOOKUP *lu)
{
    const EVP_MD *md;

    if (pkey == NULL)
        return 0;
    if (!tls1_lookup_md(ctx, lu, &md) || md == NULL)
        return 0;
    if (EVP_PKEY_get_size(pkey) < RSA_PSS_MINIMUM_KEY_SIZE(md))
        return 0;
    return 1;
}

/*
 * Returns a signature algorithm when the peer did not send a list of supported
 * signature algorithms. The signature algorithm is fixed for the certificate
 * type. |idx| is a certificate type index (SSL_PKEY_*). When |idx| is -1 the
 * certificate type from |s| will be used.
 * Returns the signature algorithm to use, or NULL on error.
 */
static const SIGALG_LOOKUP *tls1_get_legacy_sigalg(const SSL *s, int idx)
{
    if (idx == -1) {
        if (s->server) {
            size_t i;

            /* Work out index corresponding to ciphersuite */
            for (i = 0; i < SSL_PKEY_NUM; i++) {
                const SSL_CERT_LOOKUP *clu = ssl_cert_lookup_by_idx(i);

                if (clu == NULL)
                    continue;
                if (clu->amask & s->s3.tmp.new_cipher->algorithm_auth) {
                    idx = i;
                    break;
                }
            }

            /*
             * Some GOST ciphersuites allow more than one signature algorithms
             * */
            if (idx == SSL_PKEY_GOST01 && s->s3.tmp.new_cipher->algorithm_auth != SSL_aGOST01) {
                int real_idx;

                for (real_idx = SSL_PKEY_GOST12_512; real_idx >= SSL_PKEY_GOST01;
                     real_idx--) {
                    if (s->cert->pkeys[real_idx].privatekey != NULL) {
                        idx = real_idx;
                        break;
                    }
                }
            }
            /*
             * As both SSL_PKEY_GOST12_512 and SSL_PKEY_GOST12_256 indices can be used
             * with new (aGOST12-only) ciphersuites, we should find out which one is available really.
             */
            else if (idx == SSL_PKEY_GOST12_256) {
                int real_idx;

                for (real_idx = SSL_PKEY_GOST12_512; real_idx >= SSL_PKEY_GOST12_256;
                     real_idx--) {
                     if (s->cert->pkeys[real_idx].privatekey != NULL) {
                         idx = real_idx;
                         break;
                     }
                }
            }
        } else {
            idx = s->cert->key - s->cert->pkeys;
        }
    }
    if (idx < 0 || idx >= (int)OSSL_NELEM(tls_default_sigalg))
        return NULL;
    if (SSL_USE_SIGALGS(s) || idx != SSL_PKEY_RSA) {
        const SIGALG_LOOKUP *lu = tls1_lookup_sigalg(s, tls_default_sigalg[idx]);

        if (lu == NULL)
            return NULL;
        if (!tls1_lookup_md(s->ctx, lu, NULL))
            return NULL;
        if (!tls12_sigalg_allowed(s, SSL_SECOP_SIGALG_SUPPORTED, lu))
            return NULL;
        return lu;
    }
    if (!tls12_sigalg_allowed(s, SSL_SECOP_SIGALG_SUPPORTED, &legacy_rsa_sigalg))
        return NULL;
    return &legacy_rsa_sigalg;
}
/* Set peer sigalg based key type */
int tls1_set_peer_legacy_sigalg(SSL *s, const EVP_PKEY *pkey)
{
    size_t idx;
    const SIGALG_LOOKUP *lu;

    if (ssl_cert_lookup_by_pkey(pkey, &idx) == NULL)
        return 0;
    lu = tls1_get_legacy_sigalg(s, idx);
    if (lu == NULL)
        return 0;
    s->s3.tmp.peer_sigalg = lu;
    return 1;
}

size_t tls12_get_psigalgs(SSL *s, int sent, const uint16_t **psigs)
{
    /*
     * If Suite B mode use Suite B sigalgs only, ignore any other
     * preferences.
     */
    switch (tls1_suiteb(s)) {
    case SSL_CERT_FLAG_SUITEB_128_LOS:
        *psigs = suiteb_sigalgs;
        return OSSL_NELEM(suiteb_sigalgs);

    case SSL_CERT_FLAG_SUITEB_128_LOS_ONLY:
        *psigs = suiteb_sigalgs;
        return 1;

    case SSL_CERT_FLAG_SUITEB_192_LOS:
        *psigs = suiteb_sigalgs + 1;
        return 1;
    }
    /*
     *  We use client_sigalgs (if not NULL) if we're a server
     *  and sending a certificate request or if we're a client and
     *  determining which shared algorithm to use.
     */
    if ((s->server == sent) && s->cert->client_sigalgs != NULL) {
        *psigs = s->cert->client_sigalgs;
        return s->cert->client_sigalgslen;
    } else if (s->cert->conf_sigalgs) {
        *psigs = s->cert->conf_sigalgs;
        return s->cert->conf_sigalgslen;
    } else {
        *psigs = tls12_sigalgs;
        return OSSL_NELEM(tls12_sigalgs);
    }
}

/*
 * Called by servers only. Checks that we have a sig alg that supports the
 * specified EC curve.
 */
int tls_check_sigalg_curve(const SSL *s, int curve)
{
   const uint16_t *sigs;
   size_t siglen, i;

    if (s->cert->conf_sigalgs) {
        sigs = s->cert->conf_sigalgs;
        siglen = s->cert->conf_sigalgslen;
    } else {
        sigs = tls12_sigalgs;
        siglen = OSSL_NELEM(tls12_sigalgs);
    }

    for (i = 0; i < siglen; i++) {
        const SIGALG_LOOKUP *lu = tls1_lookup_sigalg(s, sigs[i]);

        if (lu == NULL)
            continue;
        if (lu->sig == EVP_PKEY_EC
                && lu->curve != NID_undef
                && curve == lu->curve)
            return 1;
    }

    return 0;
}

/*
 * Return the number of security bits for the signature algorithm, or 0 on
 * error.
 */
static int sigalg_security_bits(SSL_CTX *ctx, const SIGALG_LOOKUP *lu)
{
    const EVP_MD *md = NULL;
    int secbits = 0;

    if (!tls1_lookup_md(ctx, lu, &md))
        return 0;
    if (md != NULL)
    {
        int md_type = EVP_MD_get_type(md);

        /* Security bits: half digest bits */
        secbits = EVP_MD_get_size(md) * 4;
        /*
         * SHA1 and MD5 are known to be broken. Reduce security bits so that
         * they're no longer accepted at security level 1. The real values don't
         * really matter as long as they're lower than 80, which is our
         * security level 1.
         * https://eprint.iacr.org/2020/014 puts a chosen-prefix attack for
         * SHA1 at 2^63.4 and MD5+SHA1 at 2^67.2
         * https://documents.epfl.ch/users/l/le/lenstra/public/papers/lat.pdf
         * puts a chosen-prefix attack for MD5 at 2^39.
         */
        if (md_type == NID_sha1)
            secbits = 64;
        else if (md_type == NID_md5_sha1)
            secbits = 67;
        else if (md_type == NID_md5)
            secbits = 39;
    } else {
        /* Values from https://tools.ietf.org/html/rfc8032#section-8.5 */
        if (lu->sigalg == TLSEXT_SIGALG_ed25519)
            secbits = 128;
        else if (lu->sigalg == TLSEXT_SIGALG_ed448)
            secbits = 224;
    }
    return secbits;
}

/*
 * Check signature algorithm is consistent with sent supported signature
 * algorithms and if so set relevant digest and signature scheme in
 * s.
 */
int tls12_check_peer_sigalg(SSL *s, uint16_t sig, EVP_PKEY *pkey)
{
    const uint16_t *sent_sigs;
    const EVP_MD *md = NULL;
    char sigalgstr[2];
    size_t sent_sigslen, i, cidx;
    int pkeyid = -1;
    const SIGALG_LOOKUP *lu;
    int secbits = 0;

    pkeyid = EVP_PKEY_get_id(pkey);
    /* Should never happen */
    if (pkeyid == -1)
        return -1;
    if (SSL_IS_TLS13(s)) {
        /* Disallow DSA for TLS 1.3 */
        if (pkeyid == EVP_PKEY_DSA) {
            SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER, SSL_R_WRONG_SIGNATURE_TYPE);
            return 0;
        }
        /* Only allow PSS for TLS 1.3 */
        if (pkeyid == EVP_PKEY_RSA)
            pkeyid = EVP_PKEY_RSA_PSS;
    }
    lu = tls1_lookup_sigalg(s, sig);
    /*
     * Check sigalgs is known. Disallow SHA1/SHA224 with TLS 1.3. Check key type
     * is consistent with signature: RSA keys can be used for RSA-PSS
     */
    if (lu == NULL
        || (SSL_IS_TLS13(s) && (lu->hash == NID_sha1 || lu->hash == NID_sha224))
        || (pkeyid != lu->sig
        && (lu->sig != EVP_PKEY_RSA_PSS || pkeyid != EVP_PKEY_RSA))) {
        SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER, SSL_R_WRONG_SIGNATURE_TYPE);
        return 0;
    }
    /* Check the sigalg is consistent with the key OID */
    if (!ssl_cert_lookup_by_nid(EVP_PKEY_get_id(pkey), &cidx)
            || lu->sig_idx != (int)cidx) {
        SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER, SSL_R_WRONG_SIGNATURE_TYPE);
        return 0;
    }

    if (pkeyid == EVP_PKEY_EC) {

        /* Check point compression is permitted */
        if (!tls1_check_pkey_comp(s, pkey)) {
            SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER,
                     SSL_R_ILLEGAL_POINT_COMPRESSION);
            return 0;
        }

        /* For TLS 1.3 or Suite B check curve matches signature algorithm */
        if (SSL_IS_TLS13(s) || tls1_suiteb(s)) {
            int curve = ssl_get_EC_curve_nid(pkey);

            if (lu->curve != NID_undef && curve != lu->curve) {
                SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER, SSL_R_WRONG_CURVE);
                return 0;
            }
        }
        if (!SSL_IS_TLS13(s)) {
            /* Check curve matches extensions */
            if (!tls1_check_group_id(s, tls1_get_group_id(pkey), 1)) {
                SSLfatal(s, SSL_AD_ILLEGAL_PARAMETER, SSL_R_WRONG_CURVE);
                return 0;
            }
            if (tls1_suiteb(s)) {
                /* Check sigalg matches a permissible Suite B value */
                if (sig != TLSEXT_SIGALG_ecdsa_secp256r1_sha256
                    && sig != TLSEXT_SIGALG_ecdsa_secp384r1_sha384) {
                    SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE,
                             SSL_R_WRONG_SIGNATURE_TYPE);
                    return 0;
                }
            }
        }
    } else if (tls1_suiteb(s)) {
        SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE, SSL_R_WRONG_SIGNATURE_TYPE);
        return 0;
    }

    /* Check signature matches a type we sent */
    sent_sigslen = tls12_get_psigalgs(s, 1, &sent_sigs);
    for (i = 0; i < sent_sigslen; i++, sent_sigs++) {
        if (sig == *sent_sigs)
            break;
    }
    /* Allow fallback to SHA1 if not strict mode */
    if (i == sent_sigslen && (lu->hash != NID_sha1
        || s->cert->cert_flags & SSL_CERT_FLAGS_CHECK_TLS_STRICT)) {
        SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE, SSL_R_WRONG_SIGNATURE_TYPE);
        return 0;
    }
    if (!tls1_lookup_md(s->ctx, lu, &md)) {
        SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE, SSL_R_UNKNOWN_DIGEST);
        return 0;
    }
    /*
     * Make sure security callback allows algorithm. For historical
     * reasons we have to pass the sigalg as a two byte char array.
     */
    sigalgstr[0] = (sig >> 8) & 0xff;
    sigalgstr[1] = sig & 0xff;
    secbits = sigalg_security_bits(s->ctx, lu);
    if (secbits == 0 ||
        !ssl_security(s, SSL_SECOP_SIGALG_CHECK, secbits,
                      md != NULL ? EVP_MD_get_type(md) : NID_undef,
                      (void *)sigalgstr)) {
        SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE, SSL_R_WRONG_SIGNATURE_TYPE);
        return 0;
    }
    /* Store the sigalg the peer uses */
    s->s3.tmp.peer_sigalg = lu;
    return 1;
}

int SSL_get_peer_signature_type_nid(const SSL *s, int *pnid)
{
    if (s->s3.tmp.peer_sigalg == NULL)
        return 0;
    *pnid = s->s3.tmp.peer_sigalg->sig;
    return 1;
}

int SSL_get_signature_type_nid(const SSL *s, int *pnid)
{
    if (s->s3.tmp.sigalg == NULL)
        return 0;
    *pnid = s->s3.tmp.sigalg->sig;
    return 1;
}

/*
 * Set a mask of disabled algorithms: an algorithm is disabled if it isn't
 * supported, doesn't appear in supported signature algorithms, isn't supported
 * by the enabled protocol versions or by the security level.
 *
 * This function should only be used for checking which ciphers are supported
 * by the client.
 *
 * Call ssl_cipher_disabled() to check that it's enabled or not.
 */
int ssl_set_client_disabled(SSL *s)
{
    s->s3.tmp.mask_a = 0;
    s->s3.tmp.mask_k = 0;
    ssl_set_sig_mask(&s->s3.tmp.mask_a, s, SSL_SECOP_SIGALG_MASK);
    if (ssl_get_min_max_version(s, &s->s3.tmp.min_ver,
                                &s->s3.tmp.max_ver, NULL) != 0)
        return 0;
#ifndef OPENSSL_NO_PSK
    /* with PSK there must be client callback set */
    if (!s->psk_client_callback) {
        s->s3.tmp.mask_a |= SSL_aPSK;
        s->s3.tmp.mask_k |= SSL_PSK;
    }
#endif                          /* OPENSSL_NO_PSK */
#ifndef OPENSSL_NO_SRP
    if (!(s->srp_ctx.srp_Mask & SSL_kSRP)) {
        s->s3.tmp.mask_a |= SSL_aSRP;
        s->s3.tmp.mask_k |= SSL_kSRP;
    }
#endif
    return 1;
}

/*
 * ssl_cipher_disabled - check that a cipher is disabled or not
 * @s: SSL connection that you want to use the cipher on
 * @c: cipher to check
 * @op: Security check that you want to do
 * @ecdhe: If set to 1 then TLSv1 ECDHE ciphers are also allowed in SSLv3
 *
 * Returns 1 when it's disabled, 0 when enabled.
 */
int ssl_cipher_disabled(const SSL *s, const SSL_CIPHER *c, int op, int ecdhe)
{
    if (c->algorithm_mkey & s->s3.tmp.mask_k
        || c->algorithm_auth & s->s3.tmp.mask_a)
        return 1;
    if (s->s3.tmp.max_ver == 0)
        return 1;
    if (!SSL_IS_DTLS(s)) {
        int min_tls = c->min_tls;

        /*
         * For historical reasons we will allow ECHDE to be selected by a server
         * in SSLv3 if we are a client
         */
        if (min_tls == TLS1_VERSION && ecdhe
                && (c->algorithm_mkey & (SSL_kECDHE | SSL_kECDHEPSK)) != 0)
            min_tls = SSL3_VERSION;

        if ((min_tls > s->s3.tmp.max_ver) || (c->max_tls < s->s3.tmp.min_ver))
            return 1;
    }
    if (SSL_IS_DTLS(s) && (DTLS_VERSION_GT(c->min_dtls, s->s3.tmp.max_ver)
                           || DTLS_VERSION_LT(c->max_dtls, s->s3.tmp.min_ver)))
        return 1;

    return !ssl_security(s, op, c->strength_bits, 0, (void *)c);
}

int tls_use_ticket(SSL *s)
{
    if ((s->options & SSL_OP_NO_TICKET))
        return 0;
    return ssl_security(s, SSL_SECOP_TICKET, 0, 0, NULL);
}

int tls1_set_server_sigalgs(SSL *s)
{
    size_t i;

    /* Clear any shared signature algorithms */
    OPENSSL_free(s->shared_sigalgs);
    s->shared_sigalgs = NULL;
    s->shared_sigalgslen = 0;
    /* Clear certificate validity flags */
    for (i = 0; i < SSL_PKEY_NUM; i++)
        s->s3.tmp.valid_flags[i] = 0;
    /*
     * If peer sent no signature algorithms check to see if we support
     * the default algorithm for each certificate type
     */
    if (s->s3.tmp.peer_cert_sigalgs == NULL
            && s->s3.tmp.peer_sigalgs == NULL) {
        const uint16_t *sent_sigs;
        size_t sent_sigslen = tls12_get_psigalgs(s, 1, &sent_sigs);

        for (i = 0; i < SSL_PKEY_NUM; i++) {
            const SIGALG_LOOKUP *lu = tls1_get_legacy_sigalg(s, i);
            size_t j;

            if (lu == NULL)
                continue;
            /* Check default matches a type we sent */
            for (j = 0; j < sent_sigslen; j++) {
                if (lu->sigalg == sent_sigs[j]) {
                        s->s3.tmp.valid_flags[i] = CERT_PKEY_SIGN;
                        break;
                }
            }
        }
        return 1;
    }

    if (!tls1_process_sigalgs(s)) {
        SSLfatal(s, SSL_AD_INTERNAL_ERROR, ERR_R_INTERNAL_ERROR);
        return 0;
    }
    if (s->shared_sigalgs != NULL)
        return 1;

    /* Fatal error if no shared signature algorithms */
    SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE,
             SSL_R_NO_SHARED_SIGNATURE_ALGORITHMS);
    return 0;
}

/*-
 * Gets the ticket information supplied by the client if any.
 *
 *   hello: The parsed ClientHello data
 *   ret: (output) on return, if a ticket was decrypted, then this is set to
 *       point to the resulting session.
 */
SSL_TICKET_STATUS tls_get_ticket_from_client(SSL *s, CLIENTHELLO_MSG *hello,
                                             SSL_SESSION **ret)
{
    size_t size;
    RAW_EXTENSION *ticketext;

    *ret = NULL;
    s->ext.ticket_expected = 0;

    /*
     * If tickets disabled or not supported by the protocol version
     * (e.g. TLSv1.3) behave as if no ticket present to permit stateful
     * resumption.
     */
    if (s->version <= SSL3_VERSION || !tls_use_ticket(s))
        return SSL_TICKET_NONE;

    ticketext = &hello->pre_proc_exts[TLSEXT_IDX_session_ticket];
    if (!ticketext->present)
        return SSL_TICKET_NONE;

    size = PACKET_remaining(&ticketext->data);

    return tls_decrypt_ticket(s, PACKET_data(&ticketext->data), size,
                              hello->session_id, hello->session_id_len, ret);
}

/*-
 * tls_decrypt_ticket attempts to decrypt a session ticket.
 *
 * If s->tls_session_secret_cb is set and we're not doing TLSv1.3 then we are
 * expecting a pre-shared key ciphersuite, in which case we have no use for
 * session tickets and one will never be decrypted, nor will
 * s->ext.ticket_expected be set to 1.
 *
 * Side effects:
 *   Sets s->ext.ticket_expected to 1 if the server will have to issue
 *   a new session ticket to the client because the client indicated support
 *   (and s->tls_session_secret_cb is NULL) but the client either doesn't have
 *   a session ticket or we couldn't use the one it gave us, or if
 *   s->ctx->ext.ticket_key_cb asked to renew the client's ticket.
 *   Otherwise, s->ext.ticket_expected is set to 0.
 *
 *   etick: points to the body of the session ticket extension.
 *   eticklen: the length of the session tickets extension.
 *   sess_id: points at the session ID.
 *   sesslen: the length of the session ID.
 *   psess: (output) on return, if a ticket was decrypted, then this is set to
 *       point to the resulting session.
 */
SSL_TICKET_STATUS tls_decrypt_ticket(SSL *s, const unsigned char *etick,
                                     size_t eticklen, const unsigned char *sess_id,
                                     size_t sesslen, SSL_SESSION **psess)
{
    SSL_SESSION *sess = NULL;
    unsigned char *sdec;
    const unsigned char *p;
    int slen, ivlen, renew_ticket = 0, declen;
    SSL_TICKET_STATUS ret = SSL_TICKET_FATAL_ERR_OTHER;
    size_t mlen;
    unsigned char tick_hmac[EVP_MAX_MD_SIZE];
    SSL_HMAC *hctx = NULL;
    EVP_CIPHER_CTX *ctx = NULL;
    SSL_CTX *tctx = s->session_ctx;

    if (eticklen == 0) {
        /*
         * The client will accept a ticket but doesn't currently have
         * one (TLSv1.2 and below), or treated as a fatal error in TLSv1.3
         */
        ret = SSL_TICKET_EMPTY;
        goto end;
    }
    if (!SSL_IS_TLS13(s) && s->ext.session_secret_cb) {
        /*
         * Indicate that the ticket couldn't be decrypted rather than
         * generating the session from ticket now, trigger
         * abbreviated handshake based on external mechanism to
         * calculate the master secret later.
         */
        ret = SSL_TICKET_NO_DECRYPT;
        goto end;
    }

    /* Need at least keyname + iv */
    if (eticklen < TLSEXT_KEYNAME_LENGTH + EVP_MAX_IV_LENGTH) {
        ret = SSL_TICKET_NO_DECRYPT;
        goto end;
    }

    /* Initialize session ticket encryption and HMAC contexts */
    hctx = ssl_hmac_new(tctx);
    if (hctx == NULL) {
        ret = SSL_TICKET_FATAL_ERR_MALLOC;
        goto end;
    }
    ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        ret = SSL_TICKET_FATAL_ERR_MALLOC;
        goto end;
    }
#ifndef OPENSSL_NO_DEPRECATED_3_0
    if (tctx->ext.ticket_key_evp_cb != NULL || tctx->ext.ticket_key_cb != NULL)
#else
    if (tctx->ext.ticket_key_evp_cb != NULL)
#endif
    {
        unsigned char *nctick = (unsigned char *)etick;
        int rv = 0;

        if (tctx->ext.ticket_key_evp_cb != NULL)
            rv = tctx->ext.ticket_key_evp_cb(s, nctick,
                                             nctick + TLSEXT_KEYNAME_LENGTH,
                                             ctx,
                                             ssl_hmac_get0_EVP_MAC_CTX(hctx),
                                             0);
#ifndef OPENSSL_NO_DEPRECATED_3_0
        else if (tctx->ext.ticket_key_cb != NULL)
            /* if 0 is returned, write an empty ticket */
            rv = tctx->ext.ticket_key_cb(s, nctick,
                                         nctick + TLSEXT_KEYNAME_LENGTH,
                                         ctx, ssl_hmac_get0_HMAC_CTX(hctx), 0);
#endif
        if (rv < 0) {
            ret = SSL_TICKET_FATAL_ERR_OTHER;
            goto end;
        }
        if (rv == 0) {
            ret = SSL_TICKET_NO_DECRYPT;
            goto end;
        }
        if (rv == 2)
            renew_ticket = 1;
    } else {
        EVP_CIPHER *aes256cbc = NULL;

        /* Check key name matches */
        if (memcmp(etick, tctx->ext.tick_key_name,
                   TLSEXT_KEYNAME_LENGTH) != 0) {
            ret = SSL_TICKET_NO_DECRYPT;
            goto end;
        }

        aes256cbc = EVP_CIPHER_fetch(s->ctx->libctx, "AES-256-CBC",
                                     s->ctx->propq);
        if (aes256cbc == NULL
            || ssl_hmac_init(hctx, tctx->ext.secure->tick_hmac_key,
                             sizeof(tctx->ext.secure->tick_hmac_key),
                             "SHA256") <= 0
            || EVP_DecryptInit_ex(ctx, aes256cbc, NULL,
                                  tctx->ext.secure->tick_aes_key,
                                  etick + TLSEXT_KEYNAME_LENGTH) <= 0) {
            EVP_CIPHER_free(aes256cbc);
            ret = SSL_TICKET_FATAL_ERR_OTHER;
            goto end;
        }
        EVP_CIPHER_free(aes256cbc);
        if (SSL_IS_TLS13(s))
            renew_ticket = 1;
    }
    /*
     * Attempt to process session ticket, first conduct sanity and integrity
     * checks on ticket.
     */
    mlen = ssl_hmac_size(hctx);
    if (mlen == 0) {
        ret = SSL_TICKET_FATAL_ERR_OTHER;
        goto end;
    }

    ivlen = EVP_CIPHER_CTX_get_iv_length(ctx);
    if (ivlen < 0) {
        ret = SSL_TICKET_FATAL_ERR_OTHER;
        goto end;
    }

    /* Sanity check ticket length: must exceed keyname + IV + HMAC */
    if (eticklen <= TLSEXT_KEYNAME_LENGTH + ivlen + mlen) {
        ret = SSL_TICKET_NO_DECRYPT;
        goto end;
    }
    eticklen -= mlen;
    /* Check HMAC of encrypted ticket */
    if (ssl_hmac_update(hctx, etick, eticklen) <= 0
        || ssl_hmac_final(hctx, tick_hmac, NULL, sizeof(tick_hmac)) <= 0) {
        ret = SSL_TICKET_FATAL_ERR_OTHER;
        goto end;
    }

    if (CRYPTO_memcmp(tick_hmac, etick + eticklen, mlen)) {
        ret = SSL_TICKET_NO_DECRYPT;
        goto end;
    }
    /* Attempt to decrypt session data */
    /* Move p after IV to start of encrypted ticket, update length */
    p = etick + TLSEXT_KEYNAME_LENGTH + ivlen;
    eticklen -= TLSEXT_KEYNAME_LENGTH + ivlen;
    sdec = OPENSSL_malloc(eticklen);
    if (sdec == NULL || EVP_DecryptUpdate(ctx, sdec, &slen, p,
                                          (int)eticklen) <= 0) {
        OPENSSL_free(sdec);
        ret = SSL_TICKET_FATAL_ERR_OTHER;
        goto end;
    }
    if (EVP_DecryptFinal(ctx, sdec + slen, &declen) <= 0) {
        OPENSSL_free(sdec);
        ret = SSL_TICKET_NO_DECRYPT;
        goto end;
    }
    slen += declen;
    p = sdec;

    sess = d2i_SSL_SESSION(NULL, &p, slen);
    slen -= p - sdec;
    OPENSSL_free(sdec);
    if (sess) {
        /* Some additional consistency checks */
        if (slen != 0) {
            SSL_SESSION_free(sess);
            sess = NULL;
            ret = SSL_TICKET_NO_DECRYPT;
            goto end;
        }
        /*
         * The session ID, if non-empty, is used by some clients to detect
         * that the ticket has been accepted. So we copy it to the session
         * structure. If it is empty set length to zero as required by
         * standard.
         */
        if (sesslen) {
            memcpy(sess->session_id, sess_id, sesslen);
            sess->session_id_length = sesslen;
        }
        if (renew_ticket)
            ret = SSL_TICKET_SUCCESS_RENEW;
        else
            ret = SSL_TICKET_SUCCESS;
        goto end;
    }
    ERR_clear_error();
    /*
     * For session parse failure, indicate that we need to send a new ticket.
     */
    ret = SSL_TICKET_NO_DECRYPT;

 end:
    EVP_CIPHER_CTX_free(ctx);
    ssl_hmac_free(hctx);

    /*
     * If set, the decrypt_ticket_cb() is called unless a fatal error was
     * detected above. The callback is responsible for checking |ret| before it
     * performs any action
     */
    if (s->session_ctx->decrypt_ticket_cb != NULL
            && (ret == SSL_TICKET_EMPTY
                || ret == SSL_TICKET_NO_DECRYPT
                || ret == SSL_TICKET_SUCCESS
                || ret == SSL_TICKET_SUCCESS_RENEW)) {
        size_t keyname_len = eticklen;
        int retcb;

        if (keyname_len > TLSEXT_KEYNAME_LENGTH)
            keyname_len = TLSEXT_KEYNAME_LENGTH;
        retcb = s->session_ctx->decrypt_ticket_cb(s, sess, etick, keyname_len,
                                                  ret,
                                                  s->session_ctx->ticket_cb_data);
        switch (retcb) {
        case SSL_TICKET_RETURN_ABORT:
            ret = SSL_TICKET_FATAL_ERR_OTHER;
            break;

        case SSL_TICKET_RETURN_IGNORE:
            ret = SSL_TICKET_NONE;
            SSL_SESSION_free(sess);
            sess = NULL;
            break;

        case SSL_TICKET_RETURN_IGNORE_RENEW:
            if (ret != SSL_TICKET_EMPTY && ret != SSL_TICKET_NO_DECRYPT)
                ret = SSL_TICKET_NO_DECRYPT;
            /* else the value of |ret| will already do the right thing */
            SSL_SESSION_free(sess);
            sess = NULL;
            break;

        case SSL_TICKET_RETURN_USE:
        case SSL_TICKET_RETURN_USE_RENEW:
            if (ret != SSL_TICKET_SUCCESS
                    && ret != SSL_TICKET_SUCCESS_RENEW)
                ret = SSL_TICKET_FATAL_ERR_OTHER;
            else if (retcb == SSL_TICKET_RETURN_USE)
                ret = SSL_TICKET_SUCCESS;
            else
                ret = SSL_TICKET_SUCCESS_RENEW;
            break;

        default:
            ret = SSL_TICKET_FATAL_ERR_OTHER;
        }
    }

    if (s->ext.session_secret_cb == NULL || SSL_IS_TLS13(s)) {
        switch (ret) {
        case SSL_TICKET_NO_DECRYPT:
        case SSL_TICKET_SUCCESS_RENEW:
        case SSL_TICKET_EMPTY:
            s->ext.ticket_expected = 1;
        }
    }

    *psess = sess;

    return ret;
}

/* Check to see if a signature algorithm is allowed */
static int tls12_sigalg_allowed(const SSL *s, int op, const SIGALG_LOOKUP *lu)
{
    unsigned char sigalgstr[2];
    int secbits;

    if (lu == NULL || !lu->enabled)
        return 0;
    /* DSA is not allowed in TLS 1.3 */
    if (SSL_IS_TLS13(s) && lu->sig == EVP_PKEY_DSA)
        return 0;
    /*
     * At some point we should fully axe DSA/etc. in ClientHello as per TLS 1.3
     * spec
     */
    if (!s->server && !SSL_IS_DTLS(s) && s->s3.tmp.min_ver >= TLS1_3_VERSION
        && (lu->sig == EVP_PKEY_DSA || lu->hash_idx == SSL_MD_SHA1_IDX
            || lu->hash_idx == SSL_MD_MD5_IDX
            || lu->hash_idx == SSL_MD_SHA224_IDX))
        return 0;

    /* See if public key algorithm allowed */
    if (ssl_cert_is_disabled(s->ctx, lu->sig_idx))
        return 0;

    if (lu->sig == NID_id_GostR3410_2012_256
            || lu->sig == NID_id_GostR3410_2012_512
            || lu->sig == NID_id_GostR3410_2001) {
        /* We never allow GOST sig algs on the server with TLSv1.3 */
        if (s->server && SSL_IS_TLS13(s))
            return 0;
        if (!s->server
                && s->method->version == TLS_ANY_VERSION
                && s->s3.tmp.max_ver >= TLS1_3_VERSION) {
            int i, num;
            STACK_OF(SSL_CIPHER) *sk;

            /*
             * We're a client that could negotiate TLSv1.3. We only allow GOST
             * sig algs if we could negotiate TLSv1.2 or below and we have GOST
             * ciphersuites enabled.
             */

            if (s->s3.tmp.min_ver >= TLS1_3_VERSION)
                return 0;

            sk = SSL_get_ciphers(s);
            num = sk != NULL ? sk_SSL_CIPHER_num(sk) : 0;
            for (i = 0; i < num; i++) {
                const SSL_CIPHER *c;

                c = sk_SSL_CIPHER_value(sk, i);
                /* Skip disabled ciphers */
                if (ssl_cipher_disabled(s, c, SSL_SECOP_CIPHER_SUPPORTED, 0))
                    continue;

                if ((c->algorithm_mkey & (SSL_kGOST | SSL_kGOST18)) != 0)
                    break;
            }
            if (i == num)
                return 0;
        }
    }

    /* Finally see if security callback allows it */
    secbits = sigalg_security_bits(s->ctx, lu);
    sigalgstr[0] = (lu->sigalg >> 8) & 0xff;
    sigalgstr[1] = lu->sigalg & 0xff;
    return ssl_security(s, op, secbits, lu->hash, (void *)sigalgstr);
}

/*
 * Get a mask of disabled public key algorithms based on supported signature
 * algorithms. For example if no signature algorithm supports RSA then RSA is
 * disabled.
 */

void ssl_set_sig_mask(uint32_t *pmask_a, SSL *s, int op)
{
    const uint16_t *sigalgs;
    size_t i, sigalgslen;
    uint32_t disabled_mask = SSL_aRSA | SSL_aDSS | SSL_aECDSA;
    /*
     * Go through all signature algorithms seeing if we support any
     * in disabled_mask.
     */
    sigalgslen = tls12_get_psigalgs(s, 1, &sigalgs);
    for (i = 0; i < sigalgslen; i++, sigalgs++) {
        const SIGALG_LOOKUP *lu = tls1_lookup_sigalg(s, *sigalgs);
        const SSL_CERT_LOOKUP *clu;

        if (lu == NULL)
            continue;

        clu = ssl_cert_lookup_by_idx(lu->sig_idx);
        if (clu == NULL)
                continue;

        /* If algorithm is disabled see if we can enable it */
        if ((clu->amask & disabled_mask) != 0
                && tls12_sigalg_allowed(s, op, lu))
            disabled_mask &= ~clu->amask;
    }
    *pmask_a |= disabled_mask;
}

int tls12_copy_sigalgs(SSL *s, WPACKET *pkt,
                       const uint16_t *psig, size_t psiglen)
{
    size_t i;
    int rv = 0;

    for (i = 0; i < psiglen; i++, psig++) {
        const SIGALG_LOOKUP *lu = tls1_lookup_sigalg(s, *psig);

        if (lu == NULL
                || !tls12_sigalg_allowed(s, SSL_SECOP_SIGALG_SUPPORTED, lu))
            continue;
        if (!WPACKET_put_bytes_u16(pkt, *psig))
            return 0;
        /*
         * If TLS 1.3 must have at least one valid TLS 1.3 message
         * signing algorithm: i.e. neither RSA nor SHA1/SHA224
         */
        if (rv == 0 && (!SSL_IS_TLS13(s)
            || (lu->sig != EVP_PKEY_RSA
                && lu->hash != NID_sha1
                && lu->hash != NID_sha224)))
            rv = 1;
    }
    if (rv == 0)
        ERR_raise(ERR_LIB_SSL, SSL_R_NO_SUITABLE_SIGNATURE_ALGORITHM);
    return rv;
}

/* Given preference and allowed sigalgs set shared sigalgs */
static size_t tls12_shared_sigalgs(SSL *s, const SIGALG_LOOKUP **shsig,
                                   const uint16_t *pref, size_t preflen,
                                   const uint16_t *allow, size_t allowlen)
{
    const uint16_t *ptmp, *atmp;
    size_t i, j, nmatch = 0;
    for (i = 0, ptmp = pref; i < preflen; i++, ptmp++) {
        const SIGALG_LOOKUP *lu = tls1_lookup_sigalg(s, *ptmp);

        /* Skip disabled hashes or signature algorithms */
        if (lu == NULL
                || !tls12_sigalg_allowed(s, SSL_SECOP_SIGALG_SHARED, lu))
            continue;
        for (j = 0, atmp = allow; j < allowlen; j++, atmp++) {
            if (*ptmp == *atmp) {
                nmatch++;
                if (shsig)
                    *shsig++ = lu;
                break;
            }
        }
    }
    return nmatch;
}

/* Set shared signature algorithms for SSL structures */
static int tls1_set_shared_sigalgs(SSL *s)
{
    const uint16_t *pref, *allow, *conf;
    size_t preflen, allowlen, conflen;
    size_t nmatch;
    const SIGALG_LOOKUP **salgs = NULL;
    CERT *c = s->cert;
    unsigned int is_suiteb = tls1_suiteb(s);

    OPENSSL_free(s->shared_sigalgs);
    s->shared_sigalgs = NULL;
    s->shared_sigalgslen = 0;
    /* If client use client signature algorithms if not NULL */
    if (!s->server && c->client_sigalgs && !is_suiteb) {
        conf = c->client_sigalgs;
        conflen = c->client_sigalgslen;
    } else if (c->conf_sigalgs && !is_suiteb) {
        conf = c->conf_sigalgs;
        conflen = c->conf_sigalgslen;
    } else
        conflen = tls12_get_psigalgs(s, 0, &conf);
    if (s->options & SSL_OP_CIPHER_SERVER_PREFERENCE || is_suiteb) {
        pref = conf;
        preflen = conflen;
        allow = s->s3.tmp.peer_sigalgs;
        allowlen = s->s3.tmp.peer_sigalgslen;
    } else {
        allow = conf;
        allowlen = conflen;
        pref = s->s3.tmp.peer_sigalgs;
        preflen = s->s3.tmp.peer_sigalgslen;
    }
    nmatch = tls12_shared_sigalgs(s, NULL, pref, preflen, allow, allowlen);
    if (nmatch) {
        if ((salgs = OPENSSL_malloc(nmatch * sizeof(*salgs))) == NULL) {
            ERR_raise(ERR_LIB_SSL, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        nmatch = tls12_shared_sigalgs(s, salgs, pref, preflen, allow, allowlen);
    } else {
        salgs = NULL;
    }
    s->shared_sigalgs = salgs;
    s->shared_sigalgslen = nmatch;
    return 1;
}

int tls1_save_u16(PACKET *pkt, uint16_t **pdest, size_t *pdestlen)
{
    unsigned int stmp;
    size_t size, i;
    uint16_t *buf;

    size = PACKET_remaining(pkt);

    /* Invalid data length */
    if (size == 0 || (size & 1) != 0)
        return 0;

    size >>= 1;

    if ((buf = OPENSSL_malloc(size * sizeof(*buf))) == NULL)  {
        ERR_raise(ERR_LIB_SSL, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    for (i = 0; i < size && PACKET_get_net_2(pkt, &stmp); i++)
        buf[i] = stmp;

    if (i != size) {
        OPENSSL_free(buf);
        return 0;
    }

    OPENSSL_free(*pdest);
    *pdest = buf;
    *pdestlen = size;

    return 1;
}

int tls1_save_sigalgs(SSL *s, PACKET *pkt, int cert)
{
    /* Extension ignored for inappropriate versions */
    if (!SSL_USE_SIGALGS(s))
        return 1;
    /* Should never happen */
    if (s->cert == NULL)
        return 0;

    if (cert)
        return tls1_save_u16(pkt, &s->s3.tmp.peer_cert_sigalgs,
                             &s->s3.tmp.peer_cert_sigalgslen);
    else
        return tls1_save_u16(pkt, &s->s3.tmp.peer_sigalgs,
                             &s->s3.tmp.peer_sigalgslen);

}

/* Set preferred digest for each key type */

int tls1_process_sigalgs(SSL *s)
{
    size_t i;
    uint32_t *pvalid = s->s3.tmp.valid_flags;

    if (!tls1_set_shared_sigalgs(s))
        return 0;

    for (i = 0; i < SSL_PKEY_NUM; i++)
        pvalid[i] = 0;

    for (i = 0; i < s->shared_sigalgslen; i++) {
        const SIGALG_LOOKUP *sigptr = s->shared_sigalgs[i];
        int idx = sigptr->sig_idx;

        /* Ignore PKCS1 based sig algs in TLSv1.3 */
        if (SSL_IS_TLS13(s) && sigptr->sig == EVP_PKEY_RSA)
            continue;
        /* If not disabled indicate we can explicitly sign */
        if (pvalid[idx] == 0 && !ssl_cert_is_disabled(s->ctx, idx))
            pvalid[idx] = CERT_PKEY_EXPLICIT_SIGN | CERT_PKEY_SIGN;
    }
    return 1;
}

int SSL_get_sigalgs(SSL *s, int idx,
                    int *psign, int *phash, int *psignhash,
                    unsigned char *rsig, unsigned char *rhash)
{
    uint16_t *psig = s->s3.tmp.peer_sigalgs;
    size_t numsigalgs = s->s3.tmp.peer_sigalgslen;
    if (psig == NULL || numsigalgs > INT_MAX)
        return 0;
    if (idx >= 0) {
        const SIGALG_LOOKUP *lu;

        if (idx >= (int)numsigalgs)
            return 0;
        psig += idx;
        if (rhash != NULL)
            *rhash = (unsigned char)((*psig >> 8) & 0xff);
        if (rsig != NULL)
            *rsig = (unsigned char)(*psig & 0xff);
        lu = tls1_lookup_sigalg(s, *psig);
        if (psign != NULL)
            *psign = lu != NULL ? lu->sig : NID_undef;
        if (phash != NULL)
            *phash = lu != NULL ? lu->hash : NID_undef;
        if (psignhash != NULL)
            *psignhash = lu != NULL ? lu->sigandhash : NID_undef;
    }
    return (int)numsigalgs;
}

int SSL_get_shared_sigalgs(SSL *s, int idx,
                           int *psign, int *phash, int *psignhash,
                           unsigned char *rsig, unsigned char *rhash)
{
    const SIGALG_LOOKUP *shsigalgs;
    if (s->shared_sigalgs == NULL
        || idx < 0
        || idx >= (int)s->shared_sigalgslen
        || s->shared_sigalgslen > INT_MAX)
        return 0;
    shsigalgs = s->shared_sigalgs[idx];
    if (phash != NULL)
        *phash = shsigalgs->hash;
    if (psign != NULL)
        *psign = shsigalgs->sig;
    if (psignhash != NULL)
        *psignhash = shsigalgs->sigandhash;
    if (rsig != NULL)
        *rsig = (unsigned char)(shsigalgs->sigalg & 0xff);
    if (rhash != NULL)
        *rhash = (unsigned char)((shsigalgs->sigalg >> 8) & 0xff);
    return (int)s->shared_sigalgslen;
}

/* Maximum possible number of unique entries in sigalgs array */
#define TLS_MAX_SIGALGCNT (OSSL_NELEM(sigalg_lookup_tbl) * 2)

typedef struct {
    size_t sigalgcnt;
    /* TLSEXT_SIGALG_XXX values */
    uint16_t sigalgs[TLS_MAX_SIGALGCNT];
} sig_cb_st;

static void get_sigorhash(int *psig, int *phash, const char *str)
{
    if (strcmp(str, "RSA") == 0) {
        *psig = EVP_PKEY_RSA;
    } else if (strcmp(str, "RSA-PSS") == 0 || strcmp(str, "PSS") == 0) {
        *psig = EVP_PKEY_RSA_PSS;
    } else if (strcmp(str, "DSA") == 0) {
        *psig = EVP_PKEY_DSA;
    } else if (strcmp(str, "ECDSA") == 0) {
        *psig = EVP_PKEY_EC;
    } else {
        *phash = OBJ_sn2nid(str);
        if (*phash == NID_undef)
            *phash = OBJ_ln2nid(str);
    }
}
/* Maximum length of a signature algorithm string component */
#define TLS_MAX_SIGSTRING_LEN   40

static int sig_cb(const char *elem, int len, void *arg)
{
    sig_cb_st *sarg = arg;
    size_t i;
    const SIGALG_LOOKUP *s;
    char etmp[TLS_MAX_SIGSTRING_LEN], *p;
    int sig_alg = NID_undef, hash_alg = NID_undef;
    if (elem == NULL)
        return 0;
    if (sarg->sigalgcnt == TLS_MAX_SIGALGCNT)
        return 0;
    if (len > (int)(sizeof(etmp) - 1))
        return 0;
    memcpy(etmp, elem, len);
    etmp[len] = 0;
    p = strchr(etmp, '+');
    /*
     * We only allow SignatureSchemes listed in the sigalg_lookup_tbl;
     * if there's no '+' in the provided name, look for the new-style combined
     * name.  If not, match both sig+hash to find the needed SIGALG_LOOKUP.
     * Just sig+hash is not unique since TLS 1.3 adds rsa_pss_pss_* and
     * rsa_pss_rsae_* that differ only by public key OID; in such cases
     * we will pick the _rsae_ variant, by virtue of them appearing earlier
     * in the table.
     */
    if (p == NULL) {
        for (i = 0, s = sigalg_lookup_tbl; i < OSSL_NELEM(sigalg_lookup_tbl);
             i++, s++) {
            if (s->name != NULL && strcmp(etmp, s->name) == 0) {
                sarg->sigalgs[sarg->sigalgcnt++] = s->sigalg;
                break;
            }
        }
        if (i == OSSL_NELEM(sigalg_lookup_tbl))
            return 0;
    } else {
        *p = 0;
        p++;
        if (*p == 0)
            return 0;
        get_sigorhash(&sig_alg, &hash_alg, etmp);
        get_sigorhash(&sig_alg, &hash_alg, p);
        if (sig_alg == NID_undef || hash_alg == NID_undef)
            return 0;
        for (i = 0, s = sigalg_lookup_tbl; i < OSSL_NELEM(sigalg_lookup_tbl);
             i++, s++) {
            if (s->hash == hash_alg && s->sig == sig_alg) {
                sarg->sigalgs[sarg->sigalgcnt++] = s->sigalg;
                break;
            }
        }
        if (i == OSSL_NELEM(sigalg_lookup_tbl))
            return 0;
    }

    /* Reject duplicates */
    for (i = 0; i < sarg->sigalgcnt - 1; i++) {
        if (sarg->sigalgs[i] == sarg->sigalgs[sarg->sigalgcnt - 1]) {
            sarg->sigalgcnt--;
            return 0;
        }
    }
    return 1;
}

/*
 * Set supported signature algorithms based on a colon separated list of the
 * form sig+hash e.g. RSA+SHA512:DSA+SHA512
 */
int tls1_set_sigalgs_list(CERT *c, const char *str, int client)
{
    sig_cb_st sig;
    sig.sigalgcnt = 0;
    if (!CONF_parse_list(str, ':', 1, sig_cb, &sig))
        return 0;
    if (c == NULL)
        return 1;
    return tls1_set_raw_sigalgs(c, sig.sigalgs, sig.sigalgcnt, client);
}

int tls1_set_raw_sigalgs(CERT *c, const uint16_t *psigs, size_t salglen,
                     int client)
{
    uint16_t *sigalgs;

    if ((sigalgs = OPENSSL_malloc(salglen * sizeof(*sigalgs))) == NULL) {
        ERR_raise(ERR_LIB_SSL, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    memcpy(sigalgs, psigs, salglen * sizeof(*sigalgs));

    if (client) {
        OPENSSL_free(c->client_sigalgs);
        c->client_sigalgs = sigalgs;
        c->client_sigalgslen = salglen;
    } else {
        OPENSSL_free(c->conf_sigalgs);
        c->conf_sigalgs = sigalgs;
        c->conf_sigalgslen = salglen;
    }

    return 1;
}

int tls1_set_sigalgs(CERT *c, const int *psig_nids, size_t salglen, int client)
{
    uint16_t *sigalgs, *sptr;
    size_t i;

    if (salglen & 1)
        return 0;
    if ((sigalgs = OPENSSL_malloc((salglen / 2) * sizeof(*sigalgs))) == NULL) {
        ERR_raise(ERR_LIB_SSL, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    for (i = 0, sptr = sigalgs; i < salglen; i += 2) {
        size_t j;
        const SIGALG_LOOKUP *curr;
        int md_id = *psig_nids++;
        int sig_id = *psig_nids++;

        for (j = 0, curr = sigalg_lookup_tbl; j < OSSL_NELEM(sigalg_lookup_tbl);
             j++, curr++) {
            if (curr->hash == md_id && curr->sig == sig_id) {
                *sptr++ = curr->sigalg;
                break;
            }
        }

        if (j == OSSL_NELEM(sigalg_lookup_tbl))
            goto err;
    }

    if (client) {
        OPENSSL_free(c->client_sigalgs);
        c->client_sigalgs = sigalgs;
        c->client_sigalgslen = salglen / 2;
    } else {
        OPENSSL_free(c->conf_sigalgs);
        c->conf_sigalgs = sigalgs;
        c->conf_sigalgslen = salglen / 2;
    }

    return 1;

 err:
    OPENSSL_free(sigalgs);
    return 0;
}

static int tls1_check_sig_alg(SSL *s, X509 *x, int default_nid)
{
    int sig_nid, use_pc_sigalgs = 0;
    size_t i;
    const SIGALG_LOOKUP *sigalg;
    size_t sigalgslen;
    if (default_nid == -1)
        return 1;
    sig_nid = X509_get_signature_nid(x);
    if (default_nid)
        return sig_nid == default_nid ? 1 : 0;

    if (SSL_IS_TLS13(s) && s->s3.tmp.peer_cert_sigalgs != NULL) {
        /*
         * If we're in TLSv1.3 then we only get here if we're checking the
         * chain. If the peer has specified peer_cert_sigalgs then we use them
         * otherwise we default to normal sigalgs.
         */
        sigalgslen = s->s3.tmp.peer_cert_sigalgslen;
        use_pc_sigalgs = 1;
    } else {
        sigalgslen = s->shared_sigalgslen;
    }
    for (i = 0; i < sigalgslen; i++) {
        sigalg = use_pc_sigalgs
                 ? tls1_lookup_sigalg(s, s->s3.tmp.peer_cert_sigalgs[i])
                 : s->shared_sigalgs[i];
        if (sigalg != NULL && sig_nid == sigalg->sigandhash)
            return 1;
    }
    return 0;
}

/* Check to see if a certificate issuer name matches list of CA names */
static int ssl_check_ca_name(STACK_OF(X509_NAME) *names, X509 *x)
{
    const X509_NAME *nm;
    int i;
    nm = X509_get_issuer_name(x);
    for (i = 0; i < sk_X509_NAME_num(names); i++) {
        if (!X509_NAME_cmp(nm, sk_X509_NAME_value(names, i)))
            return 1;
    }
    return 0;
}

/*
 * Check certificate chain is consistent with TLS extensions and is usable by
 * server. This servers two purposes: it allows users to check chains before
 * passing them to the server and it allows the server to check chains before
 * attempting to use them.
 */

/* Flags which need to be set for a certificate when strict mode not set */

#define CERT_PKEY_VALID_FLAGS \
        (CERT_PKEY_EE_SIGNATURE|CERT_PKEY_EE_PARAM)
/* Strict mode flags */
#define CERT_PKEY_STRICT_FLAGS \
         (CERT_PKEY_VALID_FLAGS|CERT_PKEY_CA_SIGNATURE|CERT_PKEY_CA_PARAM \
         | CERT_PKEY_ISSUER_NAME|CERT_PKEY_CERT_TYPE)

int tls1_check_chain(SSL *s, X509 *x, EVP_PKEY *pk, STACK_OF(X509) *chain,
                     int idx)
{
    int i;
    int rv = 0;
    int check_flags = 0, strict_mode;
    CERT_PKEY *cpk = NULL;
    CERT *c = s->cert;
    uint32_t *pvalid;
    unsigned int suiteb_flags = tls1_suiteb(s);
    /* idx == -1 means checking server chains */
    if (idx != -1) {
        /* idx == -2 means checking client certificate chains */
        if (idx == -2) {
            cpk = c->key;
            idx = (int)(cpk - c->pkeys);
        } else
            cpk = c->pkeys + idx;
        pvalid = s->s3.tmp.valid_flags + idx;
        x = cpk->x509;
        pk = cpk->privatekey;
        chain = cpk->chain;
        strict_mode = c->cert_flags & SSL_CERT_FLAGS_CHECK_TLS_STRICT;
        /* If no cert or key, forget it */
        if (!x || !pk)
            goto end;
    } else {
        size_t certidx;

        if (!x || !pk)
            return 0;

        if (ssl_cert_lookup_by_pkey(pk, &certidx) == NULL)
            return 0;
        idx = certidx;
        pvalid = s->s3.tmp.valid_flags + idx;

        if (c->cert_flags & SSL_CERT_FLAGS_CHECK_TLS_STRICT)
            check_flags = CERT_PKEY_STRICT_FLAGS;
        else
            check_flags = CERT_PKEY_VALID_FLAGS;
        strict_mode = 1;
    }

    if (suiteb_flags) {
        int ok;
        if (check_flags)
            check_flags |= CERT_PKEY_SUITEB;
        ok = X509_chain_check_suiteb(NULL, x, chain, suiteb_flags);
        if (ok == X509_V_OK)
            rv |= CERT_PKEY_SUITEB;
        else if (!check_flags)
            goto end;
    }

    /*
     * Check all signature algorithms are consistent with signature
     * algorithms extension if TLS 1.2 or later and strict mode.
     */
    if (TLS1_get_version(s) >= TLS1_2_VERSION && strict_mode) {
        int default_nid;
        int rsign = 0;
        if (s->s3.tmp.peer_cert_sigalgs != NULL
                || s->s3.tmp.peer_sigalgs != NULL) {
            default_nid = 0;
        /* If no sigalgs extension use defaults from RFC5246 */
        } else {
            switch (idx) {
            case SSL_PKEY_RSA:
                rsign = EVP_PKEY_RSA;
                default_nid = NID_sha1WithRSAEncryption;
                break;

            case SSL_PKEY_DSA_SIGN:
                rsign = EVP_PKEY_DSA;
                default_nid = NID_dsaWithSHA1;
                break;

            case SSL_PKEY_ECC:
                rsign = EVP_PKEY_EC;
                default_nid = NID_ecdsa_with_SHA1;
                break;

            case SSL_PKEY_GOST01:
                rsign = NID_id_GostR3410_2001;
                default_nid = NID_id_GostR3411_94_with_GostR3410_2001;
                break;

            case SSL_PKEY_GOST12_256:
                rsign = NID_id_GostR3410_2012_256;
                default_nid = NID_id_tc26_signwithdigest_gost3410_2012_256;
                break;

            case SSL_PKEY_GOST12_512:
                rsign = NID_id_GostR3410_2012_512;
                default_nid = NID_id_tc26_signwithdigest_gost3410_2012_512;
                break;

            default:
                default_nid = -1;
                break;
            }
        }
        /*
         * If peer sent no signature algorithms extension and we have set
         * preferred signature algorithms check we support sha1.
         */
        if (default_nid > 0 && c->conf_sigalgs) {
            size_t j;
            const uint16_t *p = c->conf_sigalgs;
            for (j = 0; j < c->conf_sigalgslen; j++, p++) {
                const SIGALG_LOOKUP *lu = tls1_lookup_sigalg(s, *p);

                if (lu != NULL && lu->hash == NID_sha1 && lu->sig == rsign)
                    break;
            }
            if (j == c->conf_sigalgslen) {
                if (check_flags)
                    goto skip_sigs;
                else
                    goto end;
            }
        }
        /* Check signature algorithm of each cert in chain */
        if (SSL_IS_TLS13(s)) {
            /*
             * We only get here if the application has called SSL_check_chain(),
             * so check_flags is always set.
             */
            if (find_sig_alg(s, x, pk) != NULL)
                rv |= CERT_PKEY_EE_SIGNATURE;
        } else if (!tls1_check_sig_alg(s, x, default_nid)) {
            if (!check_flags)
                goto end;
        } else
            rv |= CERT_PKEY_EE_SIGNATURE;
        rv |= CERT_PKEY_CA_SIGNATURE;
        for (i = 0; i < sk_X509_num(chain); i++) {
            if (!tls1_check_sig_alg(s, sk_X509_value(chain, i), default_nid)) {
                if (check_flags) {
                    rv &= ~CERT_PKEY_CA_SIGNATURE;
                    break;
                } else
                    goto end;
            }
        }
    }
    /* Else not TLS 1.2, so mark EE and CA signing algorithms OK */
    else if (check_flags)
        rv |= CERT_PKEY_EE_SIGNATURE | CERT_PKEY_CA_SIGNATURE;
 skip_sigs:
    /* Check cert parameters are consistent */
    if (tls1_check_cert_param(s, x, 1))
        rv |= CERT_PKEY_EE_PARAM;
    else if (!check_flags)
        goto end;
    if (!s->server)
        rv |= CERT_PKEY_CA_PARAM;
    /* In strict mode check rest of chain too */
    else if (strict_mode) {
        rv |= CERT_PKEY_CA_PARAM;
        for (i = 0; i < sk_X509_num(chain); i++) {
            X509 *ca = sk_X509_value(chain, i);
            if (!tls1_check_cert_param(s, ca, 0)) {
                if (check_flags) {
                    rv &= ~CERT_PKEY_CA_PARAM;
                    break;
                } else
                    goto end;
            }
        }
    }
    if (!s->server && strict_mode) {
        STACK_OF(X509_NAME) *ca_dn;
        int check_type = 0;

        if (EVP_PKEY_is_a(pk, "RSA"))
            check_type = TLS_CT_RSA_SIGN;
        else if (EVP_PKEY_is_a(pk, "DSA"))
            check_type = TLS_CT_DSS_SIGN;
        else if (EVP_PKEY_is_a(pk, "EC"))
            check_type = TLS_CT_ECDSA_SIGN;

        if (check_type) {
            const uint8_t *ctypes = s->s3.tmp.ctype;
            size_t j;

            for (j = 0; j < s->s3.tmp.ctype_len; j++, ctypes++) {
                if (*ctypes == check_type) {
                    rv |= CERT_PKEY_CERT_TYPE;
                    break;
                }
            }
            if (!(rv & CERT_PKEY_CERT_TYPE) && !check_flags)
                goto end;
        } else {
            rv |= CERT_PKEY_CERT_TYPE;
        }

        ca_dn = s->s3.tmp.peer_ca_names;

        if (ca_dn == NULL
            || sk_X509_NAME_num(ca_dn) == 0
            || ssl_check_ca_name(ca_dn, x))
            rv |= CERT_PKEY_ISSUER_NAME;
        else
            for (i = 0; i < sk_X509_num(chain); i++) {
                X509 *xtmp = sk_X509_value(chain, i);

                if (ssl_check_ca_name(ca_dn, xtmp)) {
                    rv |= CERT_PKEY_ISSUER_NAME;
                    break;
                }
            }

        if (!check_flags && !(rv & CERT_PKEY_ISSUER_NAME))
            goto end;
    } else
        rv |= CERT_PKEY_ISSUER_NAME | CERT_PKEY_CERT_TYPE;

    if (!check_flags || (rv & check_flags) == check_flags)
        rv |= CERT_PKEY_VALID;

 end:

    if (TLS1_get_version(s) >= TLS1_2_VERSION)
        rv |= *pvalid & (CERT_PKEY_EXPLICIT_SIGN | CERT_PKEY_SIGN);
    else
        rv |= CERT_PKEY_SIGN | CERT_PKEY_EXPLICIT_SIGN;

    /*
     * When checking a CERT_PKEY structure all flags are irrelevant if the
     * chain is invalid.
     */
    if (!check_flags) {
        if (rv & CERT_PKEY_VALID) {
            *pvalid = rv;
        } else {
            /* Preserve sign and explicit sign flag, clear rest */
            *pvalid &= CERT_PKEY_EXPLICIT_SIGN | CERT_PKEY_SIGN;
            return 0;
        }
    }
    return rv;
}

/* Set validity of certificates in an SSL structure */
void tls1_set_cert_validity(SSL *s)
{
    tls1_check_chain(s, NULL, NULL, NULL, SSL_PKEY_RSA);
    tls1_check_chain(s, NULL, NULL, NULL, SSL_PKEY_RSA_PSS_SIGN);
    tls1_check_chain(s, NULL, NULL, NULL, SSL_PKEY_DSA_SIGN);
    tls1_check_chain(s, NULL, NULL, NULL, SSL_PKEY_ECC);
    tls1_check_chain(s, NULL, NULL, NULL, SSL_PKEY_GOST01);
    tls1_check_chain(s, NULL, NULL, NULL, SSL_PKEY_GOST12_256);
    tls1_check_chain(s, NULL, NULL, NULL, SSL_PKEY_GOST12_512);
    tls1_check_chain(s, NULL, NULL, NULL, SSL_PKEY_ED25519);
    tls1_check_chain(s, NULL, NULL, NULL, SSL_PKEY_ED448);
}

/* User level utility function to check a chain is suitable */
int SSL_check_chain(SSL *s, X509 *x, EVP_PKEY *pk, STACK_OF(X509) *chain)
{
    return tls1_check_chain(s, x, pk, chain, -1);
}

EVP_PKEY *ssl_get_auto_dh(SSL *s)
{
    EVP_PKEY *dhp = NULL;
    BIGNUM *p;
    int dh_secbits = 80, sec_level_bits;
    EVP_PKEY_CTX *pctx = NULL;
    OSSL_PARAM_BLD *tmpl = NULL;
    OSSL_PARAM *params = NULL;

    if (s->cert->dh_tmp_auto != 2) {
        if (s->s3.tmp.new_cipher->algorithm_auth & (SSL_aNULL | SSL_aPSK)) {
            if (s->s3.tmp.new_cipher->strength_bits == 256)
                dh_secbits = 128;
            else
                dh_secbits = 80;
        } else {
            if (s->s3.tmp.cert == NULL)
                return NULL;
            dh_secbits = EVP_PKEY_get_security_bits(s->s3.tmp.cert->privatekey);
        }
    }

    /* Do not pick a prime that is too weak for the current security level */
    sec_level_bits = ssl_get_security_level_bits(s, NULL, NULL);
    if (dh_secbits < sec_level_bits)
        dh_secbits = sec_level_bits;

    if (dh_secbits >= 192)
        p = BN_get_rfc3526_prime_8192(NULL);
    else if (dh_secbits >= 152)
        p = BN_get_rfc3526_prime_4096(NULL);
    else if (dh_secbits >= 128)
        p = BN_get_rfc3526_prime_3072(NULL);
    else if (dh_secbits >= 112)
        p = BN_get_rfc3526_prime_2048(NULL);
    else
        p = BN_get_rfc2409_prime_1024(NULL);
    if (p == NULL)
        goto err;

    pctx = EVP_PKEY_CTX_new_from_name(s->ctx->libctx, "DH", s->ctx->propq);
    if (pctx == NULL
            || EVP_PKEY_fromdata_init(pctx) != 1)
        goto err;

    tmpl = OSSL_PARAM_BLD_new();
    if (tmpl == NULL
            || !OSSL_PARAM_BLD_push_BN(tmpl, OSSL_PKEY_PARAM_FFC_P, p)
            || !OSSL_PARAM_BLD_push_uint(tmpl, OSSL_PKEY_PARAM_FFC_G, 2))
        goto err;

    params = OSSL_PARAM_BLD_to_param(tmpl);
    if (params == NULL
            || EVP_PKEY_fromdata(pctx, &dhp, EVP_PKEY_KEY_PARAMETERS, params) != 1)
        goto err;

err:
    OSSL_PARAM_free(params);
    OSSL_PARAM_BLD_free(tmpl);
    EVP_PKEY_CTX_free(pctx);
    BN_free(p);
    return dhp;
}

static int ssl_security_cert_key(SSL *s, SSL_CTX *ctx, X509 *x, int op)
{
    int secbits = -1;
    EVP_PKEY *pkey = X509_get0_pubkey(x);
    if (pkey) {
        /*
         * If no parameters this will return -1 and fail using the default
         * security callback for any non-zero security level. This will
         * reject keys which omit parameters but this only affects DSA and
         * omission of parameters is never (?) done in practice.
         */
        secbits = EVP_PKEY_get_security_bits(pkey);
    }
    if (s)
        return ssl_security(s, op, secbits, 0, x);
    else
        return ssl_ctx_security(ctx, op, secbits, 0, x);
}

static int ssl_security_cert_sig(SSL *s, SSL_CTX *ctx, X509 *x, int op)
{
    /* Lookup signature algorithm digest */
    int secbits, nid, pknid;
    /* Don't check signature if self signed */
    if ((X509_get_extension_flags(x) & EXFLAG_SS) != 0)
        return 1;
    if (!X509_get_signature_info(x, &nid, &pknid, &secbits, NULL))
        secbits = -1;
    /* If digest NID not defined use signature NID */
    if (nid == NID_undef)
        nid = pknid;
    if (s)
        return ssl_security(s, op, secbits, nid, x);
    else
        return ssl_ctx_security(ctx, op, secbits, nid, x);
}

int ssl_security_cert(SSL *s, SSL_CTX *ctx, X509 *x, int vfy, int is_ee)
{
    if (vfy)
        vfy = SSL_SECOP_PEER;
    if (is_ee) {
        if (!ssl_security_cert_key(s, ctx, x, SSL_SECOP_EE_KEY | vfy))
            return SSL_R_EE_KEY_TOO_SMALL;
    } else {
        if (!ssl_security_cert_key(s, ctx, x, SSL_SECOP_CA_KEY | vfy))
            return SSL_R_CA_KEY_TOO_SMALL;
    }
    if (!ssl_security_cert_sig(s, ctx, x, SSL_SECOP_CA_MD | vfy))
        return SSL_R_CA_MD_TOO_WEAK;
    return 1;
}

/*
 * Check security of a chain, if |sk| includes the end entity certificate then
 * |x| is NULL. If |vfy| is 1 then we are verifying a peer chain and not sending
 * one to the peer. Return values: 1 if ok otherwise error code to use
 */

int ssl_security_cert_chain(SSL *s, STACK_OF(X509) *sk, X509 *x, int vfy)
{
    int rv, start_idx, i;
    if (x == NULL) {
        x = sk_X509_value(sk, 0);
        if (x == NULL)
            return ERR_R_INTERNAL_ERROR;
        start_idx = 1;
    } else
        start_idx = 0;

    rv = ssl_security_cert(s, NULL, x, vfy, 1);
    if (rv != 1)
        return rv;

    for (i = start_idx; i < sk_X509_num(sk); i++) {
        x = sk_X509_value(sk, i);
        rv = ssl_security_cert(s, NULL, x, vfy, 0);
        if (rv != 1)
            return rv;
    }
    return 1;
}

/*
 * For TLS 1.2 servers check if we have a certificate which can be used
 * with the signature algorithm "lu" and return index of certificate.
 */

static int tls12_get_cert_sigalg_idx(const SSL *s, const SIGALG_LOOKUP *lu)
{
    int sig_idx = lu->sig_idx;
    const SSL_CERT_LOOKUP *clu = ssl_cert_lookup_by_idx(sig_idx);

    /* If not recognised or not supported by cipher mask it is not suitable */
    if (clu == NULL
            || (clu->amask & s->s3.tmp.new_cipher->algorithm_auth) == 0
            || (clu->nid == EVP_PKEY_RSA_PSS
                && (s->s3.tmp.new_cipher->algorithm_mkey & SSL_kRSA) != 0))
        return -1;

    return s->s3.tmp.valid_flags[sig_idx] & CERT_PKEY_VALID ? sig_idx : -1;
}

/*
 * Checks the given cert against signature_algorithm_cert restrictions sent by
 * the peer (if any) as well as whether the hash from the sigalg is usable with
 * the key.
 * Returns true if the cert is usable and false otherwise.
 */
static int check_cert_usable(SSL *s, const SIGALG_LOOKUP *sig, X509 *x,
                             EVP_PKEY *pkey)
{
    const SIGALG_LOOKUP *lu;
    int mdnid, pknid, supported;
    size_t i;
    const char *mdname = NULL;

    /*
     * If the given EVP_PKEY cannot support signing with this digest,
     * the answer is simply 'no'.
     */
    if (sig->hash != NID_undef)
        mdname = OBJ_nid2sn(sig->hash);
    supported = EVP_PKEY_digestsign_supports_digest(pkey, s->ctx->libctx,
                                                    mdname,
                                                    s->ctx->propq);
    if (supported <= 0)
        return 0;

    /*
     * The TLS 1.3 signature_algorithms_cert extension places restrictions
     * on the sigalg with which the certificate was signed (by its issuer).
     */
    if (s->s3.tmp.peer_cert_sigalgs != NULL) {
        if (!X509_get_signature_info(x, &mdnid, &pknid, NULL, NULL))
            return 0;
        for (i = 0; i < s->s3.tmp.peer_cert_sigalgslen; i++) {
            lu = tls1_lookup_sigalg(s, s->s3.tmp.peer_cert_sigalgs[i]);
            if (lu == NULL)
                continue;

            /*
             * This does not differentiate between the
             * rsa_pss_pss_* and rsa_pss_rsae_* schemes since we do not
             * have a chain here that lets us look at the key OID in the
             * signing certificate.
             */
            if (mdnid == lu->hash && pknid == lu->sig)
                return 1;
        }
        return 0;
    }

    /*
     * Without signat_algorithms_cert, any certificate for which we have
     * a viable public key is permitted.
     */
    return 1;
}

/*
 * Returns true if |s| has a usable certificate configured for use
 * with signature scheme |sig|.
 * "Usable" includes a check for presence as well as applying
 * the signature_algorithm_cert restrictions sent by the peer (if any).
 * Returns false if no usable certificate is found.
 */
static int has_usable_cert(SSL *s, const SIGALG_LOOKUP *sig, int idx)
{
    /* TLS 1.2 callers can override sig->sig_idx, but not TLS 1.3 callers. */
    if (idx == -1)
        idx = sig->sig_idx;
    if (!ssl_has_cert(s, idx))
        return 0;

    return check_cert_usable(s, sig, s->cert->pkeys[idx].x509,
                             s->cert->pkeys[idx].privatekey);
}

/*
 * Returns true if the supplied cert |x| and key |pkey| is usable with the
 * specified signature scheme |sig|, or false otherwise.
 */
static int is_cert_usable(SSL *s, const SIGALG_LOOKUP *sig, X509 *x,
                          EVP_PKEY *pkey)
{
    size_t idx;

    if (ssl_cert_lookup_by_pkey(pkey, &idx) == NULL)
        return 0;

    /* Check the key is consistent with the sig alg */
    if ((int)idx != sig->sig_idx)
        return 0;

    return check_cert_usable(s, sig, x, pkey);
}

/*
 * Find a signature scheme that works with the supplied certificate |x| and key
 * |pkey|. |x| and |pkey| may be NULL in which case we additionally look at our
 * available certs/keys to find one that works.
 */
static const SIGALG_LOOKUP *find_sig_alg(SSL *s, X509 *x, EVP_PKEY *pkey)
{
    const SIGALG_LOOKUP *lu = NULL;
    size_t i;
    int curve = -1;
    EVP_PKEY *tmppkey;

    /* Look for a shared sigalgs matching possible certificates */
    for (i = 0; i < s->shared_sigalgslen; i++) {
        lu = s->shared_sigalgs[i];

        /* Skip SHA1, SHA224, DSA and RSA if not PSS */
        if (lu->hash == NID_sha1
            || lu->hash == NID_sha224
            || lu->sig == EVP_PKEY_DSA
            || lu->sig == EVP_PKEY_RSA)
            continue;
        /* Check that we have a cert, and signature_algorithms_cert */
        if (!tls1_lookup_md(s->ctx, lu, NULL))
            continue;
        if ((pkey == NULL && !has_usable_cert(s, lu, -1))
                || (pkey != NULL && !is_cert_usable(s, lu, x, pkey)))
            continue;

        tmppkey = (pkey != NULL) ? pkey
                                 : s->cert->pkeys[lu->sig_idx].privatekey;

        if (lu->sig == EVP_PKEY_EC) {
            if (curve == -1)
                curve = ssl_get_EC_curve_nid(tmppkey);
            if (lu->curve != NID_undef && curve != lu->curve)
                continue;
        } else if (lu->sig == EVP_PKEY_RSA_PSS) {
            /* validate that key is large enough for the signature algorithm */
            if (!rsa_pss_check_min_key_size(s->ctx, tmppkey, lu))
                continue;
        }
        break;
    }

    if (i == s->shared_sigalgslen)
        return NULL;

    return lu;
}

/*
 * Choose an appropriate signature algorithm based on available certificates
 * Sets chosen certificate and signature algorithm.
 *
 * For servers if we fail to find a required certificate it is a fatal error,
 * an appropriate error code is set and a TLS alert is sent.
 *
 * For clients fatalerrs is set to 0. If a certificate is not suitable it is not
 * a fatal error: we will either try another certificate or not present one
 * to the server. In this case no error is set.
 */
int tls_choose_sigalg(SSL *s, int fatalerrs)
{
    const SIGALG_LOOKUP *lu = NULL;
    int sig_idx = -1;

    s->s3.tmp.cert = NULL;
    s->s3.tmp.sigalg = NULL;

    if (SSL_IS_TLS13(s)) {
        lu = find_sig_alg(s, NULL, NULL);
        if (lu == NULL) {
            if (!fatalerrs)
                return 1;
            SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE,
                     SSL_R_NO_SUITABLE_SIGNATURE_ALGORITHM);
            return 0;
        }
    } else {
        /* If ciphersuite doesn't require a cert nothing to do */
        if (!(s->s3.tmp.new_cipher->algorithm_auth & SSL_aCERT))
            return 1;
        if (!s->server && !ssl_has_cert(s, s->cert->key - s->cert->pkeys))
                return 1;

        if (SSL_USE_SIGALGS(s)) {
            size_t i;
            if (s->s3.tmp.peer_sigalgs != NULL) {
                int curve = -1;

                /* For Suite B need to match signature algorithm to curve */
                if (tls1_suiteb(s))
                    curve = ssl_get_EC_curve_nid(s->cert->pkeys[SSL_PKEY_ECC]
                                                 .privatekey);

                /*
                 * Find highest preference signature algorithm matching
                 * cert type
                 */
                for (i = 0; i < s->shared_sigalgslen; i++) {
                    lu = s->shared_sigalgs[i];

                    if (s->server) {
                        if ((sig_idx = tls12_get_cert_sigalg_idx(s, lu)) == -1)
                            continue;
                    } else {
                        int cc_idx = s->cert->key - s->cert->pkeys;

                        sig_idx = lu->sig_idx;
                        if (cc_idx != sig_idx)
                            continue;
                    }
                    /* Check that we have a cert, and sig_algs_cert */
                    if (!has_usable_cert(s, lu, sig_idx))
                        continue;
                    if (lu->sig == EVP_PKEY_RSA_PSS) {
                        /* validate that key is large enough for the signature algorithm */
                        EVP_PKEY *pkey = s->cert->pkeys[sig_idx].privatekey;

                        if (!rsa_pss_check_min_key_size(s->ctx, pkey, lu))
                            continue;
                    }
                    if (curve == -1 || lu->curve == curve)
                        break;
                }
#ifndef OPENSSL_NO_GOST
                /*
                 * Some Windows-based implementations do not send GOST algorithms indication
                 * in supported_algorithms extension, so when we have GOST-based ciphersuite,
                 * we have to assume GOST support.
                 */
                if (i == s->shared_sigalgslen && s->s3.tmp.new_cipher->algorithm_auth & (SSL_aGOST01 | SSL_aGOST12)) {
                  if ((lu = tls1_get_legacy_sigalg(s, -1)) == NULL) {
                    if (!fatalerrs)
                      return 1;
                    SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE,
                             SSL_R_NO_SUITABLE_SIGNATURE_ALGORITHM);
                    return 0;
                  } else {
                    i = 0;
                    sig_idx = lu->sig_idx;
                  }
                }
#endif
                if (i == s->shared_sigalgslen) {
                    if (!fatalerrs)
                        return 1;
                    SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE,
                             SSL_R_NO_SUITABLE_SIGNATURE_ALGORITHM);
                    return 0;
                }
            } else {
                /*
                 * If we have no sigalg use defaults
                 */
                const uint16_t *sent_sigs;
                size_t sent_sigslen;

                if ((lu = tls1_get_legacy_sigalg(s, -1)) == NULL) {
                    if (!fatalerrs)
                        return 1;
                    SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE,
                             SSL_R_NO_SUITABLE_SIGNATURE_ALGORITHM);
                    return 0;
                }

                /* Check signature matches a type we sent */
                sent_sigslen = tls12_get_psigalgs(s, 1, &sent_sigs);
                for (i = 0; i < sent_sigslen; i++, sent_sigs++) {
                    if (lu->sigalg == *sent_sigs
                            && has_usable_cert(s, lu, lu->sig_idx))
                        break;
                }
                if (i == sent_sigslen) {
                    if (!fatalerrs)
                        return 1;
                    SSLfatal(s, SSL_AD_HANDSHAKE_FAILURE,
                             SSL_R_WRONG_SIGNATURE_TYPE);
                    return 0;
                }
            }
        } else {
            if ((lu = tls1_get_legacy_sigalg(s, -1)) == NULL) {
                if (!fatalerrs)
                    return 1;
                SSLfatal(s, SSL_AD_INTERNAL_ERROR,
                         SSL_R_NO_SUITABLE_SIGNATURE_ALGORITHM);
                return 0;
            }
        }
    }
    if (sig_idx == -1)
        sig_idx = lu->sig_idx;
    s->s3.tmp.cert = &s->cert->pkeys[sig_idx];
    s->cert->key = s->s3.tmp.cert;
    s->s3.tmp.sigalg = lu;
    return 1;
}

int SSL_CTX_set_tlsext_max_fragment_length(SSL_CTX *ctx, uint8_t mode)
{
    if (mode != TLSEXT_max_fragment_length_DISABLED
            && !IS_MAX_FRAGMENT_LENGTH_EXT_VALID(mode)) {
        ERR_raise(ERR_LIB_SSL, SSL_R_SSL3_EXT_INVALID_MAX_FRAGMENT_LENGTH);
        return 0;
    }

    ctx->ext.max_fragment_len_mode = mode;
    return 1;
}

int SSL_set_tlsext_max_fragment_length(SSL *ssl, uint8_t mode)
{
    if (mode != TLSEXT_max_fragment_length_DISABLED
            && !IS_MAX_FRAGMENT_LENGTH_EXT_VALID(mode)) {
        ERR_raise(ERR_LIB_SSL, SSL_R_SSL3_EXT_INVALID_MAX_FRAGMENT_LENGTH);
        return 0;
    }

    ssl->ext.max_fragment_len_mode = mode;
    return 1;
}

uint8_t SSL_SESSION_get_max_fragment_length(const SSL_SESSION *session)
{
    return session->ext.max_fragment_len_mode;
}

/*
 * Helper functions for HMAC access with legacy support included.
 */
SSL_HMAC *ssl_hmac_new(const SSL_CTX *ctx)
{
    SSL_HMAC *ret = OPENSSL_zalloc(sizeof(*ret));
    EVP_MAC *mac = NULL;

    if (ret == NULL)
        return NULL;
#ifndef OPENSSL_NO_DEPRECATED_3_0
    if (ctx->ext.ticket_key_evp_cb == NULL
            && ctx->ext.ticket_key_cb != NULL) {
        if (!ssl_hmac_old_new(ret))
            goto err;
        return ret;
    }
#endif
    mac = EVP_MAC_fetch(ctx->libctx, "HMAC", ctx->propq);
    if (mac == NULL || (ret->ctx = EVP_MAC_CTX_new(mac)) == NULL)
        goto err;
    EVP_MAC_free(mac);
    return ret;
 err:
    EVP_MAC_CTX_free(ret->ctx);
    EVP_MAC_free(mac);
    OPENSSL_free(ret);
    return NULL;
}

void ssl_hmac_free(SSL_HMAC *ctx)
{
    if (ctx != NULL) {
        EVP_MAC_CTX_free(ctx->ctx);
#ifndef OPENSSL_NO_DEPRECATED_3_0
        ssl_hmac_old_free(ctx);
#endif
        OPENSSL_free(ctx);
    }
}

EVP_MAC_CTX *ssl_hmac_get0_EVP_MAC_CTX(SSL_HMAC *ctx)
{
    return ctx->ctx;
}

int ssl_hmac_init(SSL_HMAC *ctx, void *key, size_t len, char *md)
{
    OSSL_PARAM params[2], *p = params;

    if (ctx->ctx != NULL) {
        *p++ = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, md, 0);
        *p = OSSL_PARAM_construct_end();
        if (EVP_MAC_init(ctx->ctx, key, len, params))
            return 1;
    }
#ifndef OPENSSL_NO_DEPRECATED_3_0
    if (ctx->old_ctx != NULL)
        return ssl_hmac_old_init(ctx, key, len, md);
#endif
    return 0;
}

int ssl_hmac_update(SSL_HMAC *ctx, const unsigned char *data, size_t len)
{
    if (ctx->ctx != NULL)
        return EVP_MAC_update(ctx->ctx, data, len);
#ifndef OPENSSL_NO_DEPRECATED_3_0
    if (ctx->old_ctx != NULL)
        return ssl_hmac_old_update(ctx, data, len);
#endif
    return 0;
}

int ssl_hmac_final(SSL_HMAC *ctx, unsigned char *md, size_t *len,
                   size_t max_size)
{
    if (ctx->ctx != NULL)
        return EVP_MAC_final(ctx->ctx, md, len, max_size);
#ifndef OPENSSL_NO_DEPRECATED_3_0
    if (ctx->old_ctx != NULL)
        return ssl_hmac_old_final(ctx, md, len);
#endif
    return 0;
}

size_t ssl_hmac_size(const SSL_HMAC *ctx)
{
    if (ctx->ctx != NULL)
        return EVP_MAC_CTX_get_mac_size(ctx->ctx);
#ifndef OPENSSL_NO_DEPRECATED_3_0
    if (ctx->old_ctx != NULL)
        return ssl_hmac_old_size(ctx);
#endif
    return 0;
}

int ssl_get_EC_curve_nid(const EVP_PKEY *pkey)
{
    char gname[OSSL_MAX_NAME_SIZE];

    if (EVP_PKEY_get_group_name(pkey, gname, sizeof(gname), NULL) > 0)
        return OBJ_txt2nid(gname);

    return NID_undef;
}

__owur int tls13_set_encoded_pub_key(EVP_PKEY *pkey,
                                     const unsigned char *enckey,
                                     size_t enckeylen)
{
    if (EVP_PKEY_is_a(pkey, "DH")) {
        int bits = EVP_PKEY_get_bits(pkey);

        if (bits <= 0 || enckeylen != (size_t)bits / 8)
            /* the encoded key must be padded to the length of the p */
            return 0;
    } else if (EVP_PKEY_is_a(pkey, "EC")) {
        if (enckeylen < 3 /* point format and at least 1 byte for x and y */
            || enckey[0] != 0x04)
            return 0;
    }

    return EVP_PKEY_set1_encoded_public_key(pkey, enckey, enckeylen);
}
