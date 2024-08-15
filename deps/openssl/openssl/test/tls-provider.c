/*
 * Copyright 2019-2024 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <string.h>
#include <openssl/core_names.h>
#include <openssl/core_dispatch.h>
#include <openssl/rand.h>
#include <openssl/params.h>
/* For TLS1_3_VERSION */
#include <openssl/ssl.h>
#include "internal/nelem.h"

static OSSL_FUNC_keymgmt_import_fn xor_import;
static OSSL_FUNC_keymgmt_import_types_fn xor_import_types;
static OSSL_FUNC_keymgmt_export_fn xor_export;
static OSSL_FUNC_keymgmt_export_types_fn xor_export_types;

int tls_provider_init(const OSSL_CORE_HANDLE *handle,
                      const OSSL_DISPATCH *in,
                      const OSSL_DISPATCH **out,
                      void **provctx);

#define XOR_KEY_SIZE 32

/*
 * Top secret. This algorithm only works if no one knows what this number is.
 * Please don't tell anyone what it is.
 * 
 * This algorithm is for testing only - don't really use it!
 */
static const unsigned char private_constant[XOR_KEY_SIZE] = {
    0xd3, 0x6b, 0x54, 0xec, 0x5b, 0xac, 0x89, 0x96, 0x8c, 0x2c, 0x66, 0xa5,
    0x67, 0x0d, 0xe3, 0xdd, 0x43, 0x69, 0xbc, 0x83, 0x3d, 0x60, 0xc7, 0xb8,
    0x2b, 0x1c, 0x5a, 0xfd, 0xb5, 0xcd, 0xd0, 0xf8
};

typedef struct xorkey_st {
    unsigned char privkey[XOR_KEY_SIZE];
    unsigned char pubkey[XOR_KEY_SIZE];
    int hasprivkey;
    int haspubkey;
} XORKEY;


/* Key Management for the dummy XOR KEX and KEM algorithms */

static OSSL_FUNC_keymgmt_new_fn xor_newdata;
static OSSL_FUNC_keymgmt_free_fn xor_freedata;
static OSSL_FUNC_keymgmt_has_fn xor_has;
static OSSL_FUNC_keymgmt_dup_fn xor_dup;
static OSSL_FUNC_keymgmt_gen_init_fn xor_gen_init;
static OSSL_FUNC_keymgmt_gen_set_params_fn xor_gen_set_params;
static OSSL_FUNC_keymgmt_gen_settable_params_fn xor_gen_settable_params;
static OSSL_FUNC_keymgmt_gen_fn xor_gen;
static OSSL_FUNC_keymgmt_gen_cleanup_fn xor_gen_cleanup;
static OSSL_FUNC_keymgmt_get_params_fn xor_get_params;
static OSSL_FUNC_keymgmt_gettable_params_fn xor_gettable_params;
static OSSL_FUNC_keymgmt_set_params_fn xor_set_params;
static OSSL_FUNC_keymgmt_settable_params_fn xor_settable_params;

/*
 * Dummy "XOR" Key Exchange algorithm. We just xor the private and public keys
 * together. Don't use this!
 */

static OSSL_FUNC_keyexch_newctx_fn xor_newctx;
static OSSL_FUNC_keyexch_init_fn xor_init;
static OSSL_FUNC_keyexch_set_peer_fn xor_set_peer;
static OSSL_FUNC_keyexch_derive_fn xor_derive;
static OSSL_FUNC_keyexch_freectx_fn xor_freectx;
static OSSL_FUNC_keyexch_dupctx_fn xor_dupctx;

/*
 * Dummy "XOR" Key Encapsulation Method. We just build a KEM over the xor KEX.
 * Don't use this!
 */

static OSSL_FUNC_kem_newctx_fn xor_newctx;
static OSSL_FUNC_kem_freectx_fn xor_freectx;
static OSSL_FUNC_kem_dupctx_fn xor_dupctx;
static OSSL_FUNC_kem_encapsulate_init_fn xor_init;
static OSSL_FUNC_kem_encapsulate_fn xor_encapsulate;
static OSSL_FUNC_kem_decapsulate_init_fn xor_init;
static OSSL_FUNC_kem_decapsulate_fn xor_decapsulate;


/*
 * We define 2 dummy TLS groups called "xorgroup" and "xorkemgroup" for test
 * purposes
 */
struct tls_group_st {
    unsigned int group_id; /* for "tls-group-id", see provider-base(7) */
    unsigned int secbits;
    unsigned int mintls;
    unsigned int maxtls;
    unsigned int mindtls;
    unsigned int maxdtls;
    unsigned int is_kem; /* boolean */
};

#define XORGROUP_NAME "xorgroup"
#define XORGROUP_NAME_INTERNAL "xorgroup-int"
static struct tls_group_st xor_group = {
    0,                  /* group_id, set by randomize_tls_group_id() */
    128,                /* secbits */
    TLS1_3_VERSION,     /* mintls */
    0,                  /* maxtls */
    -1,                 /* mindtls */
    -1,                 /* maxdtls */
    0                   /* is_kem */
};

#define XORKEMGROUP_NAME "xorkemgroup"
#define XORKEMGROUP_NAME_INTERNAL "xorkemgroup-int"
static struct tls_group_st xor_kemgroup = {
    0,                  /* group_id, set by randomize_tls_group_id() */
    128,                /* secbits */
    TLS1_3_VERSION,     /* mintls */
    0,                  /* maxtls */
    -1,                 /* mindtls */
    -1,                 /* maxdtls */
    1                   /* is_kem */
};

#define ALGORITHM "XOR"

static const OSSL_PARAM xor_group_params[] = {
    OSSL_PARAM_utf8_string(OSSL_CAPABILITY_TLS_GROUP_NAME,
                           XORGROUP_NAME, sizeof(XORGROUP_NAME)),
    OSSL_PARAM_utf8_string(OSSL_CAPABILITY_TLS_GROUP_NAME_INTERNAL,
                           XORGROUP_NAME_INTERNAL,
                           sizeof(XORGROUP_NAME_INTERNAL)),
    OSSL_PARAM_utf8_string(OSSL_CAPABILITY_TLS_GROUP_ALG, ALGORITHM,
                           sizeof(ALGORITHM)),
    OSSL_PARAM_uint(OSSL_CAPABILITY_TLS_GROUP_ID, &xor_group.group_id),
    OSSL_PARAM_uint(OSSL_CAPABILITY_TLS_GROUP_SECURITY_BITS,
                    &xor_group.secbits),
    OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_MIN_TLS, &xor_group.mintls),
    OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_MAX_TLS, &xor_group.maxtls),
    OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_MIN_DTLS, &xor_group.mindtls),
    OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_MAX_DTLS, &xor_group.maxdtls),
    OSSL_PARAM_uint(OSSL_CAPABILITY_TLS_GROUP_IS_KEM, &xor_group.is_kem),
    OSSL_PARAM_END
};

static const OSSL_PARAM xor_kemgroup_params[] = {
    OSSL_PARAM_utf8_string(OSSL_CAPABILITY_TLS_GROUP_NAME,
                           XORKEMGROUP_NAME, sizeof(XORKEMGROUP_NAME)),
    OSSL_PARAM_utf8_string(OSSL_CAPABILITY_TLS_GROUP_NAME_INTERNAL,
                           XORKEMGROUP_NAME_INTERNAL,
                           sizeof(XORKEMGROUP_NAME_INTERNAL)),
    OSSL_PARAM_utf8_string(OSSL_CAPABILITY_TLS_GROUP_ALG, ALGORITHM,
                           sizeof(ALGORITHM)),
    OSSL_PARAM_uint(OSSL_CAPABILITY_TLS_GROUP_ID, &xor_kemgroup.group_id),
    OSSL_PARAM_uint(OSSL_CAPABILITY_TLS_GROUP_SECURITY_BITS,
                    &xor_kemgroup.secbits),
    OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_MIN_TLS, &xor_kemgroup.mintls),
    OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_MAX_TLS, &xor_kemgroup.maxtls),
    OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_MIN_DTLS, &xor_kemgroup.mindtls),
    OSSL_PARAM_int(OSSL_CAPABILITY_TLS_GROUP_MAX_DTLS, &xor_kemgroup.maxdtls),
    OSSL_PARAM_uint(OSSL_CAPABILITY_TLS_GROUP_IS_KEM, &xor_kemgroup.is_kem),
    OSSL_PARAM_END
};

#define NUM_DUMMY_GROUPS 50
static char *dummy_group_names[NUM_DUMMY_GROUPS];

static int tls_prov_get_capabilities(void *provctx, const char *capability,
                                     OSSL_CALLBACK *cb, void *arg)
{
    int ret;
    int i;
    const char *dummy_base = "dummy";
    const size_t dummy_name_max_size = strlen(dummy_base) + 3;

    if (strcmp(capability, "TLS-GROUP") != 0) {
        /* We don't support this capability */
        return 0;
    }

    /* Register our 2 groups */
    OPENSSL_assert(xor_group.group_id >= 65024
                   && xor_group.group_id < 65279 - NUM_DUMMY_GROUPS);
    ret = cb(xor_group_params, arg);
    ret &= cb(xor_kemgroup_params, arg);

    /*
     * Now register some dummy groups > GROUPLIST_INCREMENT (== 40) as defined
     * in ssl/t1_lib.c, to make sure we exercise the code paths for registering
     * large numbers of groups.
     */

    for (i = 0; i < NUM_DUMMY_GROUPS; i++) {
        OSSL_PARAM dummygroup[OSSL_NELEM(xor_group_params)];
        unsigned int dummygroup_id;

        memcpy(dummygroup, xor_group_params, sizeof(xor_group_params));

        /* Give the dummy group a unique name */
        if (dummy_group_names[i] == NULL) {
            dummy_group_names[i] = OPENSSL_zalloc(dummy_name_max_size);
            if (dummy_group_names[i] == NULL)
                return 0;
            BIO_snprintf(dummy_group_names[i],
                         dummy_name_max_size,
                         "%s%d", dummy_base, i);
        }
        dummygroup[0].data = dummy_group_names[i];
        dummygroup[0].data_size = strlen(dummy_group_names[i]) + 1;
        /* assign unique group IDs also to dummy groups for registration */
        dummygroup_id = 65279 - NUM_DUMMY_GROUPS + i;
        dummygroup[3].data = (unsigned char*)&dummygroup_id;
        ret &= cb(dummygroup, arg);
    }

    return ret;
}

/*
 * Dummy "XOR" Key Exchange algorithm. We just xor the private and public keys
 * together. Don't use this!
 */

typedef struct {
    XORKEY *key;
    XORKEY *peerkey;
    void *provctx;
} PROV_XOR_CTX;

static void *xor_newctx(void *provctx)
{
    PROV_XOR_CTX *pxorctx = OPENSSL_zalloc(sizeof(PROV_XOR_CTX));

    if (pxorctx == NULL)
        return NULL;

    pxorctx->provctx = provctx;

    return pxorctx;
}

static int xor_init(void *vpxorctx, void *vkey,
                    ossl_unused const OSSL_PARAM params[])
{
    PROV_XOR_CTX *pxorctx = (PROV_XOR_CTX *)vpxorctx;

    if (pxorctx == NULL || vkey == NULL)
        return 0;
    pxorctx->key = vkey;
    return 1;
}

static int xor_set_peer(void *vpxorctx, void *vpeerkey)
{
    PROV_XOR_CTX *pxorctx = (PROV_XOR_CTX *)vpxorctx;

    if (pxorctx == NULL || vpeerkey == NULL)
        return 0;
    pxorctx->peerkey = vpeerkey;
    return 1;
}

static int xor_derive(void *vpxorctx, unsigned char *secret, size_t *secretlen,
                      size_t outlen)
{
    PROV_XOR_CTX *pxorctx = (PROV_XOR_CTX *)vpxorctx;
    int i;

    if (pxorctx->key == NULL || pxorctx->peerkey == NULL)
        return 0;

    *secretlen = XOR_KEY_SIZE;
    if (secret == NULL)
        return 1;

    if (outlen < XOR_KEY_SIZE)
        return 0;

    for (i = 0; i < XOR_KEY_SIZE; i++)
        secret[i] = pxorctx->key->privkey[i] ^ pxorctx->peerkey->pubkey[i];

    return 1;
}

static void xor_freectx(void *pxorctx)
{
    OPENSSL_free(pxorctx);
}

static void *xor_dupctx(void *vpxorctx)
{
    PROV_XOR_CTX *srcctx = (PROV_XOR_CTX *)vpxorctx;
    PROV_XOR_CTX *dstctx;

    dstctx = OPENSSL_zalloc(sizeof(*srcctx));
    if (dstctx == NULL)
        return NULL;

    *dstctx = *srcctx;

    return dstctx;
}

static const OSSL_DISPATCH xor_keyexch_functions[] = {
    { OSSL_FUNC_KEYEXCH_NEWCTX, (void (*)(void))xor_newctx },
    { OSSL_FUNC_KEYEXCH_INIT, (void (*)(void))xor_init },
    { OSSL_FUNC_KEYEXCH_DERIVE, (void (*)(void))xor_derive },
    { OSSL_FUNC_KEYEXCH_SET_PEER, (void (*)(void))xor_set_peer },
    { OSSL_FUNC_KEYEXCH_FREECTX, (void (*)(void))xor_freectx },
    { OSSL_FUNC_KEYEXCH_DUPCTX, (void (*)(void))xor_dupctx },
    { 0, NULL }
};

static const OSSL_ALGORITHM tls_prov_keyexch[] = {
    /*
     * Obviously this is not FIPS approved, but in order to test in conjuction
     * with the FIPS provider we pretend that it is.
     */
    { "XOR", "provider=tls-provider,fips=yes", xor_keyexch_functions },
    { NULL, NULL, NULL }
};

/*
 * Dummy "XOR" Key Encapsulation Method. We just build a KEM over the xor KEX.
 * Don't use this!
 */

static int xor_encapsulate(void *vpxorctx,
                           unsigned char *ct, size_t *ctlen,
                           unsigned char *ss, size_t *sslen)
{
    /*
     * We are building this around a KEX:
     *
     * 1. we generate ephemeral keypair
     * 2. we encode our ephemeral pubkey as the outgoing ct
     * 3. we derive using our ephemeral privkey in combination with the peer
     *    pubkey from the ctx; the result is our ss.
     */
    int rv = 0;
    void *genctx = NULL, *derivectx = NULL;
    XORKEY *ourkey = NULL;
    PROV_XOR_CTX *pxorctx = vpxorctx;

    if (ct == NULL || ss == NULL) {
        /* Just return sizes */

        if (ctlen == NULL && sslen == NULL)
            return 0;
        if (ctlen != NULL)
            *ctlen = XOR_KEY_SIZE;
        if (sslen != NULL)
            *sslen = XOR_KEY_SIZE;
        return 1;
    }

    /* 1. Generate keypair */
    genctx = xor_gen_init(pxorctx->provctx, OSSL_KEYMGMT_SELECT_KEYPAIR, NULL);
    if (genctx == NULL)
        goto end;
    ourkey = xor_gen(genctx, NULL, NULL);
    if (ourkey == NULL)
        goto end;

    /* 2. Encode ephemeral pubkey as ct */
    memcpy(ct, ourkey->pubkey, XOR_KEY_SIZE);
    *ctlen = XOR_KEY_SIZE;

    /* 3. Derive ss via KEX */
    derivectx = xor_newctx(pxorctx->provctx);
    if (derivectx == NULL
            || !xor_init(derivectx, ourkey, NULL)
            || !xor_set_peer(derivectx, pxorctx->key)
            || !xor_derive(derivectx, ss, sslen, XOR_KEY_SIZE))
        goto end;

    rv = 1;

 end:
    xor_gen_cleanup(genctx);
    xor_freedata(ourkey);
    xor_freectx(derivectx);
    return rv;
}

static int xor_decapsulate(void *vpxorctx,
                           unsigned char *ss, size_t *sslen,
                           const unsigned char *ct, size_t ctlen)
{
    /*
     * We are building this around a KEX:
     *
     * - ct is our peer's pubkey
     * - decapsulate is just derive.
     */
    int rv = 0;
    void *derivectx = NULL;
    XORKEY *peerkey = NULL;
    PROV_XOR_CTX *pxorctx = vpxorctx;

    if (ss == NULL) {
        /* Just return size */
        if (sslen == NULL)
            return 0;
        *sslen = XOR_KEY_SIZE;
        return 1;
    }

    if (ctlen != XOR_KEY_SIZE)
        return 0;
    peerkey = xor_newdata(pxorctx->provctx);
    if (peerkey == NULL)
        goto end;
    memcpy(peerkey->pubkey, ct, XOR_KEY_SIZE);

    /* Derive ss via KEX */
    derivectx = xor_newctx(pxorctx->provctx);
    if (derivectx == NULL
            || !xor_init(derivectx, pxorctx->key, NULL)
            || !xor_set_peer(derivectx, peerkey)
            || !xor_derive(derivectx, ss, sslen, XOR_KEY_SIZE))
        goto end;

    rv = 1;

 end:
    xor_freedata(peerkey);
    xor_freectx(derivectx);
    return rv;
}

static const OSSL_DISPATCH xor_kem_functions[] = {
    { OSSL_FUNC_KEM_NEWCTX, (void (*)(void))xor_newctx },
    { OSSL_FUNC_KEM_FREECTX, (void (*)(void))xor_freectx },
    { OSSL_FUNC_KEM_DUPCTX, (void (*)(void))xor_dupctx },
    { OSSL_FUNC_KEM_ENCAPSULATE_INIT, (void (*)(void))xor_init },
    { OSSL_FUNC_KEM_ENCAPSULATE, (void (*)(void))xor_encapsulate },
    { OSSL_FUNC_KEM_DECAPSULATE_INIT, (void (*)(void))xor_init },
    { OSSL_FUNC_KEM_DECAPSULATE, (void (*)(void))xor_decapsulate },
    { 0, NULL }
};

static const OSSL_ALGORITHM tls_prov_kem[] = {
    /*
     * Obviously this is not FIPS approved, but in order to test in conjuction
     * with the FIPS provider we pretend that it is.
     */
    { "XOR", "provider=tls-provider,fips=yes", xor_kem_functions },
    { NULL, NULL, NULL }
};

/* Key Management for the dummy XOR key exchange algorithm */

static void *xor_newdata(void *provctx)
{
    return OPENSSL_zalloc(sizeof(XORKEY));
}

static void xor_freedata(void *keydata)
{
    OPENSSL_free(keydata);
}

static int xor_has(const void *vkey, int selection)
{
    const XORKEY *key = vkey;
    int ok = 0;

    if (key != NULL) {
        ok = 1;

        if ((selection & OSSL_KEYMGMT_SELECT_PUBLIC_KEY) != 0)
            ok = ok && key->haspubkey;
        if ((selection & OSSL_KEYMGMT_SELECT_PRIVATE_KEY) != 0)
            ok = ok && key->hasprivkey;
    }
    return ok;
}

static void *xor_dup(const void *vfromkey, int selection)
{
    XORKEY *tokey = xor_newdata(NULL);
    const XORKEY *fromkey = vfromkey;
    int ok = 0;

    if (tokey != NULL && fromkey != NULL) {
        ok = 1;

        if ((selection & OSSL_KEYMGMT_SELECT_PUBLIC_KEY) != 0) {
            if (fromkey->haspubkey) {
                memcpy(tokey->pubkey, fromkey->pubkey, XOR_KEY_SIZE);
                tokey->haspubkey = 1;
            } else {
                tokey->haspubkey = 0;
            }
        }
        if ((selection & OSSL_KEYMGMT_SELECT_PRIVATE_KEY) != 0) {
            if (fromkey->hasprivkey) {
                memcpy(tokey->privkey, fromkey->privkey, XOR_KEY_SIZE);
                tokey->hasprivkey = 1;
            } else {
                tokey->hasprivkey = 0;
            }
        }
    }
    if (!ok) {
        xor_freedata(tokey);
        tokey = NULL;
    }
    return tokey;
}

static ossl_inline int xor_get_params(void *vkey, OSSL_PARAM params[])
{
    XORKEY *key = vkey;
    OSSL_PARAM *p;

    if ((p = OSSL_PARAM_locate(params, OSSL_PKEY_PARAM_BITS)) != NULL
        && !OSSL_PARAM_set_int(p, XOR_KEY_SIZE))
        return 0;

    if ((p = OSSL_PARAM_locate(params, OSSL_PKEY_PARAM_SECURITY_BITS)) != NULL
        && !OSSL_PARAM_set_int(p, xor_group.secbits))
        return 0;

    if ((p = OSSL_PARAM_locate(params,
                               OSSL_PKEY_PARAM_ENCODED_PUBLIC_KEY)) != NULL) {
        if (p->data_type != OSSL_PARAM_OCTET_STRING)
            return 0;
        p->return_size = XOR_KEY_SIZE;
        if (p->data != NULL && p->data_size >= XOR_KEY_SIZE)
            memcpy(p->data, key->pubkey, XOR_KEY_SIZE);
    }

    return 1;
}

static const OSSL_PARAM xor_params[] = {
    OSSL_PARAM_int(OSSL_PKEY_PARAM_BITS, NULL),
    OSSL_PARAM_int(OSSL_PKEY_PARAM_SECURITY_BITS, NULL),
    OSSL_PARAM_octet_string(OSSL_PKEY_PARAM_ENCODED_PUBLIC_KEY, NULL, 0),
    OSSL_PARAM_END
};

static const OSSL_PARAM *xor_gettable_params(void *provctx)
{
    return xor_params;
}

static int xor_set_params(void *vkey, const OSSL_PARAM params[])
{
    XORKEY *key = vkey;
    const OSSL_PARAM *p;

    p = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_ENCODED_PUBLIC_KEY);
    if (p != NULL) {
        if (p->data_type != OSSL_PARAM_OCTET_STRING
                || p->data_size != XOR_KEY_SIZE)
            return 0;
        memcpy(key->pubkey, p->data, XOR_KEY_SIZE);
        key->haspubkey = 1;
    }

    return 1;
}

static const OSSL_PARAM xor_known_settable_params[] = {
    OSSL_PARAM_octet_string(OSSL_PKEY_PARAM_ENCODED_PUBLIC_KEY, NULL, 0),
    OSSL_PARAM_END
};

static const OSSL_PARAM *xor_settable_params(void *provctx)
{
    return xor_known_settable_params;
}

struct xor_gen_ctx {
    int selection;
    OSSL_LIB_CTX *libctx;
};

static void *xor_gen_init(void *provctx, int selection,
                          const OSSL_PARAM params[])
{
    struct xor_gen_ctx *gctx = NULL;

    if ((selection & (OSSL_KEYMGMT_SELECT_KEYPAIR
                      | OSSL_KEYMGMT_SELECT_DOMAIN_PARAMETERS)) == 0)
        return NULL;

    if ((gctx = OPENSSL_zalloc(sizeof(*gctx))) != NULL)
        gctx->selection = selection;

    /* Our provctx is really just an OSSL_LIB_CTX */
    gctx->libctx = (OSSL_LIB_CTX *)provctx;

    if (!xor_gen_set_params(gctx, params)) {
        OPENSSL_free(gctx);
        return NULL;
    }
    return gctx;
}

static int xor_gen_set_params(void *genctx, const OSSL_PARAM params[])
{
    struct xor_gen_ctx *gctx = genctx;
    const OSSL_PARAM *p;

    if (gctx == NULL)
        return 0;

    p = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_GROUP_NAME);
    if (p != NULL) {
        if (p->data_type != OSSL_PARAM_UTF8_STRING
                || (strcmp(p->data, XORGROUP_NAME_INTERNAL) != 0
                    &&  strcmp(p->data, XORKEMGROUP_NAME_INTERNAL) != 0))
            return 0;
    }

    return 1;
}

static const OSSL_PARAM *xor_gen_settable_params(ossl_unused void *genctx,
                                                 ossl_unused void *provctx)
{
    static OSSL_PARAM settable[] = {
        OSSL_PARAM_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, NULL, 0),
        OSSL_PARAM_END
    };
    return settable;
}

static void *xor_gen(void *genctx, OSSL_CALLBACK *osslcb, void *cbarg)
{
    struct xor_gen_ctx *gctx = genctx;
    XORKEY *key = OPENSSL_zalloc(sizeof(*key));
    size_t i;

    if (key == NULL)
        return NULL;

    if ((gctx->selection & OSSL_KEYMGMT_SELECT_KEYPAIR) != 0) {
        if (RAND_bytes_ex(gctx->libctx, key->privkey, XOR_KEY_SIZE, 0) <= 0) {
            OPENSSL_free(key);
            return NULL;
        }
        for (i = 0; i < XOR_KEY_SIZE; i++)
            key->pubkey[i] = key->privkey[i] ^ private_constant[i];
        key->hasprivkey = 1;
        key->haspubkey = 1;
    }

    return key;
}

/* IMPORT + EXPORT */

static int xor_import(void *vkey, int select, const OSSL_PARAM params[])
{
    XORKEY *key = vkey;
    const OSSL_PARAM *param_priv_key, *param_pub_key;
    unsigned char privkey[XOR_KEY_SIZE];
    unsigned char pubkey[XOR_KEY_SIZE];
    void *pprivkey = privkey, *ppubkey = pubkey;
    size_t priv_len = 0, pub_len = 0;
    int res = 0;

    if (key == NULL || (select & OSSL_KEYMGMT_SELECT_KEYPAIR) == 0)
        return 0;

    memset(privkey, 0, sizeof(privkey));
    memset(pubkey, 0, sizeof(pubkey));
    param_priv_key = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_PRIV_KEY);
    param_pub_key = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_PUB_KEY);

    if ((param_priv_key != NULL
         && !OSSL_PARAM_get_octet_string(param_priv_key, &pprivkey,
                                         sizeof(privkey), &priv_len))
        || (param_pub_key != NULL
            && !OSSL_PARAM_get_octet_string(param_pub_key, &ppubkey,
                                            sizeof(pubkey), &pub_len)))
        goto err;

    if (priv_len > 0) {
        memcpy(key->privkey, privkey, priv_len);
        key->hasprivkey = 1;
    }
    if (pub_len > 0) {
        memcpy(key->pubkey, pubkey, pub_len);
        key->haspubkey = 1;
    }
    res = 1;
 err:
    return res;
}

static int xor_export(void *vkey, int select, OSSL_CALLBACK *param_cb,
                      void *cbarg)
{
    XORKEY *key = vkey;
    OSSL_PARAM params[3], *p = params;

    if (key == NULL || (select & OSSL_KEYMGMT_SELECT_KEYPAIR) == 0)
        return 0;

    *p++ = OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PRIV_KEY,
                                             key->privkey,
                                             sizeof(key->privkey));
    *p++ = OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY,
                                             key->pubkey, sizeof(key->pubkey));
    *p++ = OSSL_PARAM_construct_end();

    return param_cb(params, cbarg);
}

static const OSSL_PARAM xor_key_types[] = {
    OSSL_PARAM_BN(OSSL_PKEY_PARAM_PUB_KEY, NULL, 0),
    OSSL_PARAM_BN(OSSL_PKEY_PARAM_PRIV_KEY, NULL, 0),
    OSSL_PARAM_END
};

static const OSSL_PARAM *xor_import_types(int select)
{
    return (select & OSSL_KEYMGMT_SELECT_KEYPAIR) != 0 ? xor_key_types : NULL;
}

static const OSSL_PARAM *xor_export_types(int select)
{
    return (select & OSSL_KEYMGMT_SELECT_KEYPAIR) != 0 ? xor_key_types : NULL;
}

static void xor_gen_cleanup(void *genctx)
{
    OPENSSL_free(genctx);
}

static const OSSL_DISPATCH xor_keymgmt_functions[] = {
    { OSSL_FUNC_KEYMGMT_NEW, (void (*)(void))xor_newdata },
    { OSSL_FUNC_KEYMGMT_GEN_INIT, (void (*)(void))xor_gen_init },
    { OSSL_FUNC_KEYMGMT_GEN_SET_PARAMS, (void (*)(void))xor_gen_set_params },
    { OSSL_FUNC_KEYMGMT_GEN_SETTABLE_PARAMS,
      (void (*)(void))xor_gen_settable_params },
    { OSSL_FUNC_KEYMGMT_GEN, (void (*)(void))xor_gen },
    { OSSL_FUNC_KEYMGMT_GEN_CLEANUP, (void (*)(void))xor_gen_cleanup },
    { OSSL_FUNC_KEYMGMT_GET_PARAMS, (void (*) (void))xor_get_params },
    { OSSL_FUNC_KEYMGMT_GETTABLE_PARAMS, (void (*) (void))xor_gettable_params },
    { OSSL_FUNC_KEYMGMT_SET_PARAMS, (void (*) (void))xor_set_params },
    { OSSL_FUNC_KEYMGMT_SETTABLE_PARAMS, (void (*) (void))xor_settable_params },
    { OSSL_FUNC_KEYMGMT_HAS, (void (*)(void))xor_has },
    { OSSL_FUNC_KEYMGMT_DUP, (void (*)(void))xor_dup },
    { OSSL_FUNC_KEYMGMT_FREE, (void (*)(void))xor_freedata },
    { OSSL_FUNC_KEYMGMT_IMPORT, (void (*)(void))xor_import },
    { OSSL_FUNC_KEYMGMT_IMPORT_TYPES, (void (*)(void))xor_import_types },
    { OSSL_FUNC_KEYMGMT_EXPORT, (void (*)(void))xor_export },
    { OSSL_FUNC_KEYMGMT_EXPORT_TYPES, (void (*)(void))xor_export_types },
    { 0, NULL }
};

static const OSSL_ALGORITHM tls_prov_keymgmt[] = {
    /*
     * Obviously this is not FIPS approved, but in order to test in conjuction
     * with the FIPS provider we pretend that it is.
     */
    { "XOR", "provider=tls-provider,fips=yes", xor_keymgmt_functions },
    { NULL, NULL, NULL }
};

static const OSSL_ALGORITHM *tls_prov_query(void *provctx, int operation_id,
                                            int *no_cache)
{
    *no_cache = 0;
    switch (operation_id) {
    case OSSL_OP_KEYMGMT:
        return tls_prov_keymgmt;
    case OSSL_OP_KEYEXCH:
        return tls_prov_keyexch;
    case OSSL_OP_KEM:
        return tls_prov_kem;
    }
    return NULL;
}

static void tls_prov_teardown(void *provctx)
{
    int i;

    OSSL_LIB_CTX_free(provctx);

    for (i = 0; i < NUM_DUMMY_GROUPS; i++) {
        OPENSSL_free(dummy_group_names[i]);
        dummy_group_names[i] = NULL;
    }
}

/* Functions we provide to the core */
static const OSSL_DISPATCH tls_prov_dispatch_table[] = {
    { OSSL_FUNC_PROVIDER_TEARDOWN, (void (*)(void))tls_prov_teardown },
    { OSSL_FUNC_PROVIDER_QUERY_OPERATION, (void (*)(void))tls_prov_query },
    { OSSL_FUNC_PROVIDER_GET_CAPABILITIES, (void (*)(void))tls_prov_get_capabilities },
    { 0, NULL }
};

static
unsigned int randomize_tls_group_id(OSSL_LIB_CTX *libctx)
{
    /*
     * Randomise the group_id we're going to use to ensure we don't interoperate
     * with anything but ourselves.
     */
    unsigned int group_id;
    static unsigned int mem[10] = { 0 };
    static int in_mem = 0;
    int i;

 retry:
    if (RAND_bytes_ex(libctx, (unsigned char *)&group_id, sizeof(group_id), 0) <= 0)
        return 0;
    /*
     * Ensure group_id is within the IANA Reserved for private use range
     * (65024-65279).
     * Carve out NUM_DUMMY_GROUPS ids for properly registering those.
     */
    group_id %= 65279 - NUM_DUMMY_GROUPS - 65024;
    group_id += 65024;

    /* Ensure we did not already issue this group_id */
    for (i = 0; i < in_mem; i++)
        if (mem[i] == group_id)
            goto retry;

    /* Add this group_id to the list of ids issued by this function */
    mem[in_mem++] = group_id;

    return group_id;
}

int tls_provider_init(const OSSL_CORE_HANDLE *handle,
                      const OSSL_DISPATCH *in,
                      const OSSL_DISPATCH **out,
                      void **provctx)
{
    OSSL_LIB_CTX *libctx = OSSL_LIB_CTX_new();

    if (libctx == NULL)
        return 0;

    *provctx = libctx;

    /*
     * Randomise the group_id we're going to use to ensure we don't interoperate
     * with anything but ourselves.
     */
    xor_group.group_id = randomize_tls_group_id(libctx);
    xor_kemgroup.group_id = randomize_tls_group_id(libctx);

    *out = tls_prov_dispatch_table;
    return 1;
}
