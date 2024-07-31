/*
 * Copyright 1999-2024 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * HMAC low level APIs are deprecated for public use, but still ok for internal
 * use.
 */
#include "internal/deprecated.h"

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/crypto.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/pkcs12.h>
#include "p12_local.h"

int PKCS12_mac_present(const PKCS12 *p12)
{
    return p12->mac ? 1 : 0;
}

void PKCS12_get0_mac(const ASN1_OCTET_STRING **pmac,
                     const X509_ALGOR **pmacalg,
                     const ASN1_OCTET_STRING **psalt,
                     const ASN1_INTEGER **piter,
                     const PKCS12 *p12)
{
    if (p12->mac) {
        X509_SIG_get0(p12->mac->dinfo, pmacalg, pmac);
        if (psalt)
            *psalt = p12->mac->salt;
        if (piter)
            *piter = p12->mac->iter;
    } else {
        if (pmac)
            *pmac = NULL;
        if (pmacalg)
            *pmacalg = NULL;
        if (psalt)
            *psalt = NULL;
        if (piter)
            *piter = NULL;
    }
}

#define TK26_MAC_KEY_LEN 32

static int pkcs12_gen_gost_mac_key(const char *pass, int passlen,
                                   const unsigned char *salt, int saltlen,
                                   int iter, int keylen, unsigned char *key,
                                   const EVP_MD *digest)
{
    unsigned char out[96];

    if (keylen != TK26_MAC_KEY_LEN) {
        return 0;
    }

    if (!PKCS5_PBKDF2_HMAC(pass, passlen, salt, saltlen, iter,
                           digest, sizeof(out), out)) {
        return 0;
    }
    memcpy(key, out + sizeof(out) - TK26_MAC_KEY_LEN, TK26_MAC_KEY_LEN);
    OPENSSL_cleanse(out, sizeof(out));
    return 1;
}

/* Generate a MAC */
static int pkcs12_gen_mac(PKCS12 *p12, const char *pass, int passlen,
                          unsigned char *mac, unsigned int *maclen,
                          int (*pkcs12_key_gen)(const char *pass, int passlen,
                                                unsigned char *salt, int slen,
                                                int id, int iter, int n,
                                                unsigned char *out,
                                                const EVP_MD *md_type))
{
    int ret = 0;
    const EVP_MD *md;
    EVP_MD *md_fetch;
    HMAC_CTX *hmac = NULL;
    unsigned char key[EVP_MAX_MD_SIZE], *salt;
    int saltlen, iter;
    char md_name[80];
    int md_size = 0;
    int md_nid;
    const X509_ALGOR *macalg;
    const ASN1_OBJECT *macoid;

    if (!PKCS7_type_is_data(p12->authsafes)) {
        ERR_raise(ERR_LIB_PKCS12, PKCS12_R_CONTENT_TYPE_NOT_DATA);
        return 0;
    }

    if (p12->authsafes->d.data == NULL) {
        ERR_raise(ERR_LIB_PKCS12, PKCS12_R_DECODE_ERROR);
        return 0;
    }

    salt = p12->mac->salt->data;
    saltlen = p12->mac->salt->length;
    if (p12->mac->iter == NULL)
        iter = 1;
    else
        iter = ASN1_INTEGER_get(p12->mac->iter);
    X509_SIG_get0(p12->mac->dinfo, &macalg, NULL);
    X509_ALGOR_get0(&macoid, NULL, NULL, macalg);
    if (OBJ_obj2txt(md_name, sizeof(md_name), macoid, 0) < 0)
        return 0;

    (void)ERR_set_mark();
    md = md_fetch = EVP_MD_fetch(p12->authsafes->ctx.libctx, md_name,
                                 p12->authsafes->ctx.propq);
    if (md == NULL)
        md = EVP_get_digestbynid(OBJ_obj2nid(macoid));

    if (md == NULL) {
        (void)ERR_clear_last_mark();
        ERR_raise(ERR_LIB_PKCS12, PKCS12_R_UNKNOWN_DIGEST_ALGORITHM);
        return 0;
    }
    (void)ERR_pop_to_mark();

    md_size = EVP_MD_get_size(md);
    md_nid = EVP_MD_get_type(md);
    if (md_size < 0)
        goto err;
    if ((md_nid == NID_id_GostR3411_94
         || md_nid == NID_id_GostR3411_2012_256
         || md_nid == NID_id_GostR3411_2012_512)
        && ossl_safe_getenv("LEGACY_GOST_PKCS12") == NULL) {
        md_size = TK26_MAC_KEY_LEN;
        if (!pkcs12_gen_gost_mac_key(pass, passlen, salt, saltlen, iter,
                                     md_size, key, md)) {
            ERR_raise(ERR_LIB_PKCS12, PKCS12_R_KEY_GEN_ERROR);
            goto err;
        }
    } else {
        if (pkcs12_key_gen != NULL) {
            if (!(*pkcs12_key_gen)(pass, passlen, salt, saltlen, PKCS12_MAC_ID,
                                   iter, md_size, key, md)) {
                ERR_raise(ERR_LIB_PKCS12, PKCS12_R_KEY_GEN_ERROR);
                goto err;
            }
        } else {
            /* Default to UTF-8 password */
            if (!PKCS12_key_gen_utf8_ex(pass, passlen, salt, saltlen, PKCS12_MAC_ID,
                                       iter, md_size, key, md,
                                       p12->authsafes->ctx.libctx,
                                       p12->authsafes->ctx.propq)) {
                ERR_raise(ERR_LIB_PKCS12, PKCS12_R_KEY_GEN_ERROR);
                goto err;
            }
        }
    }
    if ((hmac = HMAC_CTX_new()) == NULL
        || !HMAC_Init_ex(hmac, key, md_size, md, NULL)
        || !HMAC_Update(hmac, p12->authsafes->d.data->data,
                        p12->authsafes->d.data->length)
        || !HMAC_Final(hmac, mac, maclen)) {
        goto err;
    }
    ret = 1;

err:
    OPENSSL_cleanse(key, sizeof(key));
    HMAC_CTX_free(hmac);
    EVP_MD_free(md_fetch);
    return ret;
}

int PKCS12_gen_mac(PKCS12 *p12, const char *pass, int passlen,
                   unsigned char *mac, unsigned int *maclen)
{
    return pkcs12_gen_mac(p12, pass, passlen, mac, maclen, NULL);
}

/* Verify the mac */
int PKCS12_verify_mac(PKCS12 *p12, const char *pass, int passlen)
{
    unsigned char mac[EVP_MAX_MD_SIZE];
    unsigned int maclen;
    const ASN1_OCTET_STRING *macoct;

    if (p12->mac == NULL) {
        ERR_raise(ERR_LIB_PKCS12, PKCS12_R_MAC_ABSENT);
        return 0;
    }
    if (!pkcs12_gen_mac(p12, pass, passlen, mac, &maclen, NULL)) {
        ERR_raise(ERR_LIB_PKCS12, PKCS12_R_MAC_GENERATION_ERROR);
        return 0;
    }
    X509_SIG_get0(p12->mac->dinfo, NULL, &macoct);
    if ((maclen != (unsigned int)ASN1_STRING_length(macoct))
        || CRYPTO_memcmp(mac, ASN1_STRING_get0_data(macoct), maclen) != 0)
        return 0;

    return 1;
}

/* Set a mac */

int PKCS12_set_mac(PKCS12 *p12, const char *pass, int passlen,
                   unsigned char *salt, int saltlen, int iter,
                   const EVP_MD *md_type)
{
    unsigned char mac[EVP_MAX_MD_SIZE];
    unsigned int maclen;
    ASN1_OCTET_STRING *macoct;

    if (md_type == NULL)
        /* No need to do a fetch as the md_type is used only to get a NID */
        md_type = EVP_sha256();
    if (!iter)
        iter = PKCS12_DEFAULT_ITER;
    if (PKCS12_setup_mac(p12, iter, salt, saltlen, md_type) == PKCS12_ERROR) {
        ERR_raise(ERR_LIB_PKCS12, PKCS12_R_MAC_SETUP_ERROR);
        return 0;
    }
    /*
     * Note that output mac is forced to UTF-8...
     */
    if (!pkcs12_gen_mac(p12, pass, passlen, mac, &maclen, NULL)) {
        ERR_raise(ERR_LIB_PKCS12, PKCS12_R_MAC_GENERATION_ERROR);
        return 0;
    }
    X509_SIG_getm(p12->mac->dinfo, NULL, &macoct);
    if (!ASN1_OCTET_STRING_set(macoct, mac, maclen)) {
        ERR_raise(ERR_LIB_PKCS12, PKCS12_R_MAC_STRING_SET_ERROR);
        return 0;
    }
    return 1;
}

/* Set up a mac structure */
int PKCS12_setup_mac(PKCS12 *p12, int iter, unsigned char *salt, int saltlen,
                     const EVP_MD *md_type)
{
    X509_ALGOR *macalg;

    PKCS12_MAC_DATA_free(p12->mac);
    p12->mac = NULL;

    if ((p12->mac = PKCS12_MAC_DATA_new()) == NULL)
        return PKCS12_ERROR;
    if (iter > 1) {
        if ((p12->mac->iter = ASN1_INTEGER_new()) == NULL) {
            ERR_raise(ERR_LIB_PKCS12, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        if (!ASN1_INTEGER_set(p12->mac->iter, iter)) {
            ERR_raise(ERR_LIB_PKCS12, ERR_R_MALLOC_FAILURE);
            return 0;
        }
    }
    if (saltlen == 0)
        saltlen = PKCS12_SALT_LEN;
    else if (saltlen < 0)
        return 0;
    if ((p12->mac->salt->data = OPENSSL_malloc(saltlen)) == NULL) {
        ERR_raise(ERR_LIB_PKCS12, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    p12->mac->salt->length = saltlen;
    if (salt == NULL) {
        if (RAND_bytes_ex(p12->authsafes->ctx.libctx, p12->mac->salt->data,
                          (size_t)saltlen, 0) <= 0)
            return 0;
    } else {
        memcpy(p12->mac->salt->data, salt, saltlen);
    }
    X509_SIG_getm(p12->mac->dinfo, &macalg, NULL);
    if (!X509_ALGOR_set0(macalg, OBJ_nid2obj(EVP_MD_get_type(md_type)),
                         V_ASN1_NULL, NULL)) {
        ERR_raise(ERR_LIB_PKCS12, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    return 1;
}
