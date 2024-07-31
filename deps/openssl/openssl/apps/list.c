/*
 * Copyright 1995-2022 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* We need to use some deprecated APIs */
#define OPENSSL_SUPPRESS_DEPRECATED

#include <string.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/provider.h>
#include <openssl/safestack.h>
#include <openssl/kdf.h>
#include <openssl/encoder.h>
#include <openssl/decoder.h>
#include <openssl/store.h>
#include <openssl/core_names.h>
#include <openssl/rand.h>
#include "apps.h"
#include "app_params.h"
#include "progs.h"
#include "opt.h"
#include "names.h"

static int verbose = 0;
static const char *select_name = NULL;

/* Checks to see if algorithms are fetchable */
#define IS_FETCHABLE(type, TYPE)                                \
    static int is_ ## type ## _fetchable(const TYPE *alg)       \
    {                                                           \
        TYPE *impl;                                             \
        const char *propq = app_get0_propq();                   \
        OSSL_LIB_CTX *libctx = app_get0_libctx();               \
        const char *name = TYPE ## _get0_name(alg);             \
                                                                \
        ERR_set_mark();                                         \
        impl = TYPE ## _fetch(libctx, name, propq);             \
        ERR_pop_to_mark();                                      \
        if (impl == NULL)                                       \
            return 0;                                           \
        TYPE ## _free(impl);                                    \
        return 1;                                               \
    }
IS_FETCHABLE(cipher, EVP_CIPHER)
IS_FETCHABLE(digest, EVP_MD)
IS_FETCHABLE(mac, EVP_MAC)
IS_FETCHABLE(kdf, EVP_KDF)
IS_FETCHABLE(rand, EVP_RAND)
IS_FETCHABLE(keymgmt, EVP_KEYMGMT)
IS_FETCHABLE(signature, EVP_SIGNATURE)
IS_FETCHABLE(kem, EVP_KEM)
IS_FETCHABLE(asym_cipher, EVP_ASYM_CIPHER)
IS_FETCHABLE(keyexch, EVP_KEYEXCH)
IS_FETCHABLE(decoder, OSSL_DECODER)
IS_FETCHABLE(encoder, OSSL_ENCODER)

#ifndef OPENSSL_NO_DEPRECATED_3_0
static int include_legacy(void)
{
    return app_get0_propq() == NULL;
}

static void legacy_cipher_fn(const EVP_CIPHER *c,
                             const char *from, const char *to, void *arg)
{
    if (select_name != NULL
        && (c == NULL
            || OPENSSL_strcasecmp(select_name,  EVP_CIPHER_get0_name(c)) != 0))
        return;
    if (c != NULL) {
        BIO_printf(arg, "  %s\n", EVP_CIPHER_get0_name(c));
    } else {
        if (from == NULL)
            from = "<undefined>";
        if (to == NULL)
            to = "<undefined>";
        BIO_printf(arg, "  %s => %s\n", from, to);
    }
}
#endif

DEFINE_STACK_OF(EVP_CIPHER)
static int cipher_cmp(const EVP_CIPHER * const *a,
                      const EVP_CIPHER * const *b)
{
    return strcmp(OSSL_PROVIDER_get0_name(EVP_CIPHER_get0_provider(*a)),
                  OSSL_PROVIDER_get0_name(EVP_CIPHER_get0_provider(*b)));
}

static void collect_ciphers(EVP_CIPHER *cipher, void *stack)
{
    STACK_OF(EVP_CIPHER) *cipher_stack = stack;

    if (is_cipher_fetchable(cipher)
            && sk_EVP_CIPHER_push(cipher_stack, cipher) > 0)
        EVP_CIPHER_up_ref(cipher);
}

static void list_ciphers(void)
{
    STACK_OF(EVP_CIPHER) *ciphers = sk_EVP_CIPHER_new(cipher_cmp);
    int i;

    if (ciphers == NULL) {
        BIO_printf(bio_err, "ERROR: Memory allocation\n");
        return;
    }
#ifndef OPENSSL_NO_DEPRECATED_3_0
    if (include_legacy()) {
        BIO_printf(bio_out, "Legacy:\n");
        EVP_CIPHER_do_all_sorted(legacy_cipher_fn, bio_out);
    }
#endif

    BIO_printf(bio_out, "Provided:\n");
    EVP_CIPHER_do_all_provided(app_get0_libctx(), collect_ciphers, ciphers);
    sk_EVP_CIPHER_sort(ciphers);
    for (i = 0; i < sk_EVP_CIPHER_num(ciphers); i++) {
        const EVP_CIPHER *c = sk_EVP_CIPHER_value(ciphers, i);
        STACK_OF(OPENSSL_CSTRING) *names = NULL;

        if (select_name != NULL && !EVP_CIPHER_is_a(c, select_name))
            continue;

        names = sk_OPENSSL_CSTRING_new(name_cmp);
        if (names != NULL && EVP_CIPHER_names_do_all(c, collect_names, names)) {
            BIO_printf(bio_out, "  ");
            print_names(bio_out, names);

            BIO_printf(bio_out, " @ %s\n",
                       OSSL_PROVIDER_get0_name(EVP_CIPHER_get0_provider(c)));

            if (verbose) {
                const char *desc = EVP_CIPHER_get0_description(c);

                if (desc != NULL)
                    BIO_printf(bio_out, "    description: %s\n", desc);
                print_param_types("retrievable algorithm parameters",
                                  EVP_CIPHER_gettable_params(c), 4);
                print_param_types("retrievable operation parameters",
                                  EVP_CIPHER_gettable_ctx_params(c), 4);
                print_param_types("settable operation parameters",
                                  EVP_CIPHER_settable_ctx_params(c), 4);
            }
        }
        sk_OPENSSL_CSTRING_free(names);
    }
    sk_EVP_CIPHER_pop_free(ciphers, EVP_CIPHER_free);
}

#ifndef OPENSSL_NO_DEPRECATED_3_0
static void legacy_md_fn(const EVP_MD *m,
                       const char *from, const char *to, void *arg)
{
    if (m != NULL) {
        BIO_printf(arg, "  %s\n", EVP_MD_get0_name(m));
    } else {
        if (from == NULL)
            from = "<undefined>";
        if (to == NULL)
            to = "<undefined>";
        BIO_printf((BIO *)arg, "  %s => %s\n", from, to);
    }
}
#endif

DEFINE_STACK_OF(EVP_MD)
static int md_cmp(const EVP_MD * const *a, const EVP_MD * const *b)
{
    return strcmp(OSSL_PROVIDER_get0_name(EVP_MD_get0_provider(*a)),
                  OSSL_PROVIDER_get0_name(EVP_MD_get0_provider(*b)));
}

static void collect_digests(EVP_MD *digest, void *stack)
{
    STACK_OF(EVP_MD) *digest_stack = stack;

    if (is_digest_fetchable(digest)
            && sk_EVP_MD_push(digest_stack, digest) > 0)
        EVP_MD_up_ref(digest);
}

static void list_digests(void)
{
    STACK_OF(EVP_MD) *digests = sk_EVP_MD_new(md_cmp);
    int i;

    if (digests == NULL) {
        BIO_printf(bio_err, "ERROR: Memory allocation\n");
        return;
    }
#ifndef OPENSSL_NO_DEPRECATED_3_0
    if (include_legacy()) {
        BIO_printf(bio_out, "Legacy:\n");
        EVP_MD_do_all_sorted(legacy_md_fn, bio_out);
    }
#endif

    BIO_printf(bio_out, "Provided:\n");
    EVP_MD_do_all_provided(app_get0_libctx(), collect_digests, digests);
    sk_EVP_MD_sort(digests);
    for (i = 0; i < sk_EVP_MD_num(digests); i++) {
        const EVP_MD *m = sk_EVP_MD_value(digests, i);
        STACK_OF(OPENSSL_CSTRING) *names = NULL;

        if (select_name != NULL && !EVP_MD_is_a(m, select_name))
            continue;

        names = sk_OPENSSL_CSTRING_new(name_cmp);
        if (names != NULL && EVP_MD_names_do_all(m, collect_names, names)) {
            BIO_printf(bio_out, "  ");
            print_names(bio_out, names);

            BIO_printf(bio_out, " @ %s\n",
                       OSSL_PROVIDER_get0_name(EVP_MD_get0_provider(m)));

            if (verbose) {
                const char *desc = EVP_MD_get0_description(m);

                if (desc != NULL)
                    BIO_printf(bio_out, "    description: %s\n", desc);
                print_param_types("retrievable algorithm parameters",
                                EVP_MD_gettable_params(m), 4);
                print_param_types("retrievable operation parameters",
                                EVP_MD_gettable_ctx_params(m), 4);
                print_param_types("settable operation parameters",
                                EVP_MD_settable_ctx_params(m), 4);
            }
        }
        sk_OPENSSL_CSTRING_free(names);
    }
    sk_EVP_MD_pop_free(digests, EVP_MD_free);
}

DEFINE_STACK_OF(EVP_MAC)
static int mac_cmp(const EVP_MAC * const *a, const EVP_MAC * const *b)
{
    return strcmp(OSSL_PROVIDER_get0_name(EVP_MAC_get0_provider(*a)),
                  OSSL_PROVIDER_get0_name(EVP_MAC_get0_provider(*b)));
}

static void collect_macs(EVP_MAC *mac, void *stack)
{
    STACK_OF(EVP_MAC) *mac_stack = stack;

    if (is_mac_fetchable(mac)
            && sk_EVP_MAC_push(mac_stack, mac) > 0)
        EVP_MAC_up_ref(mac);
}

static void list_macs(void)
{
    STACK_OF(EVP_MAC) *macs = sk_EVP_MAC_new(mac_cmp);
    int i;

    if (macs == NULL) {
        BIO_printf(bio_err, "ERROR: Memory allocation\n");
        return;
    }
    BIO_printf(bio_out, "Provided MACs:\n");
    EVP_MAC_do_all_provided(app_get0_libctx(), collect_macs, macs);
    sk_EVP_MAC_sort(macs);
    for (i = 0; i < sk_EVP_MAC_num(macs); i++) {
        const EVP_MAC *m = sk_EVP_MAC_value(macs, i);
        STACK_OF(OPENSSL_CSTRING) *names = NULL;

        if (select_name != NULL && !EVP_MAC_is_a(m, select_name))
            continue;

        names = sk_OPENSSL_CSTRING_new(name_cmp);
        if (names != NULL && EVP_MAC_names_do_all(m, collect_names, names)) {
            BIO_printf(bio_out, "  ");
            print_names(bio_out, names);

            BIO_printf(bio_out, " @ %s\n",
                       OSSL_PROVIDER_get0_name(EVP_MAC_get0_provider(m)));

            if (verbose) {
                const char *desc = EVP_MAC_get0_description(m);

                if (desc != NULL)
                    BIO_printf(bio_out, "    description: %s\n", desc);
                print_param_types("retrievable algorithm parameters",
                                EVP_MAC_gettable_params(m), 4);
                print_param_types("retrievable operation parameters",
                                EVP_MAC_gettable_ctx_params(m), 4);
                print_param_types("settable operation parameters",
                                EVP_MAC_settable_ctx_params(m), 4);
            }
        }
        sk_OPENSSL_CSTRING_free(names);
    }
    sk_EVP_MAC_pop_free(macs, EVP_MAC_free);
}

/*
 * KDFs and PRFs
 */
DEFINE_STACK_OF(EVP_KDF)
static int kdf_cmp(const EVP_KDF * const *a, const EVP_KDF * const *b)
{
    return strcmp(OSSL_PROVIDER_get0_name(EVP_KDF_get0_provider(*a)),
                  OSSL_PROVIDER_get0_name(EVP_KDF_get0_provider(*b)));
}

static void collect_kdfs(EVP_KDF *kdf, void *stack)
{
    STACK_OF(EVP_KDF) *kdf_stack = stack;

    if (is_kdf_fetchable(kdf)
            && sk_EVP_KDF_push(kdf_stack, kdf) > 0)
        EVP_KDF_up_ref(kdf);
}

static void list_kdfs(void)
{
    STACK_OF(EVP_KDF) *kdfs = sk_EVP_KDF_new(kdf_cmp);
    int i;

    if (kdfs == NULL) {
        BIO_printf(bio_err, "ERROR: Memory allocation\n");
        return;
    }
    BIO_printf(bio_out, "Provided KDFs and PDFs:\n");
    EVP_KDF_do_all_provided(app_get0_libctx(), collect_kdfs, kdfs);
    sk_EVP_KDF_sort(kdfs);
    for (i = 0; i < sk_EVP_KDF_num(kdfs); i++) {
        const EVP_KDF *k = sk_EVP_KDF_value(kdfs, i);
        STACK_OF(OPENSSL_CSTRING) *names = NULL;

        if (select_name != NULL && !EVP_KDF_is_a(k, select_name))
            continue;

        names = sk_OPENSSL_CSTRING_new(name_cmp);
        if (names != NULL && EVP_KDF_names_do_all(k, collect_names, names)) {
            BIO_printf(bio_out, "  ");
            print_names(bio_out, names);

            BIO_printf(bio_out, " @ %s\n",
                       OSSL_PROVIDER_get0_name(EVP_KDF_get0_provider(k)));

            if (verbose) {
                const char *desc = EVP_KDF_get0_description(k);

                if (desc != NULL)
                    BIO_printf(bio_out, "    description: %s\n", desc);
                print_param_types("retrievable algorithm parameters",
                                EVP_KDF_gettable_params(k), 4);
                print_param_types("retrievable operation parameters",
                                EVP_KDF_gettable_ctx_params(k), 4);
                print_param_types("settable operation parameters",
                                EVP_KDF_settable_ctx_params(k), 4);
            }
        }
        sk_OPENSSL_CSTRING_free(names);
    }
    sk_EVP_KDF_pop_free(kdfs, EVP_KDF_free);
}

/*
 * RANDs
 */
DEFINE_STACK_OF(EVP_RAND)

static int rand_cmp(const EVP_RAND * const *a, const EVP_RAND * const *b)
{
    int ret = OPENSSL_strcasecmp(EVP_RAND_get0_name(*a), EVP_RAND_get0_name(*b));

    if (ret == 0)
        ret = strcmp(OSSL_PROVIDER_get0_name(EVP_RAND_get0_provider(*a)),
                     OSSL_PROVIDER_get0_name(EVP_RAND_get0_provider(*b)));

    return ret;
}

static void collect_rands(EVP_RAND *rand, void *stack)
{
    STACK_OF(EVP_RAND) *rand_stack = stack;

    if (is_rand_fetchable(rand)
            && sk_EVP_RAND_push(rand_stack, rand) > 0)
        EVP_RAND_up_ref(rand);
}

static void list_random_generators(void)
{
    STACK_OF(EVP_RAND) *rands = sk_EVP_RAND_new(rand_cmp);
    int i;

    if (rands == NULL) {
        BIO_printf(bio_err, "ERROR: Memory allocation\n");
        return;
    }
    BIO_printf(bio_out, "Provided RNGs and seed sources:\n");
    EVP_RAND_do_all_provided(app_get0_libctx(), collect_rands, rands);
    sk_EVP_RAND_sort(rands);
    for (i = 0; i < sk_EVP_RAND_num(rands); i++) {
        const EVP_RAND *m = sk_EVP_RAND_value(rands, i);

        if (select_name != NULL
            && OPENSSL_strcasecmp(EVP_RAND_get0_name(m), select_name) != 0)
            continue;
        BIO_printf(bio_out, "  %s", EVP_RAND_get0_name(m));
        BIO_printf(bio_out, " @ %s\n",
                   OSSL_PROVIDER_get0_name(EVP_RAND_get0_provider(m)));

        if (verbose) {
            const char *desc = EVP_RAND_get0_description(m);

            if (desc != NULL)
                BIO_printf(bio_out, "    description: %s\n", desc);
            print_param_types("retrievable algorithm parameters",
                              EVP_RAND_gettable_params(m), 4);
            print_param_types("retrievable operation parameters",
                              EVP_RAND_gettable_ctx_params(m), 4);
            print_param_types("settable operation parameters",
                              EVP_RAND_settable_ctx_params(m), 4);
        }
    }
    sk_EVP_RAND_pop_free(rands, EVP_RAND_free);
}

static void display_random(const char *name, EVP_RAND_CTX *drbg)
{
    EVP_RAND *rand;
    uint64_t u;
    const char *p;
    const OSSL_PARAM *gettables;
    OSSL_PARAM params[2] = { OSSL_PARAM_END, OSSL_PARAM_END };
    unsigned char buf[1000];

    BIO_printf(bio_out, "%s:\n", name);
    if (drbg != NULL) {
        rand = EVP_RAND_CTX_get0_rand(drbg);

        BIO_printf(bio_out, "  %s", EVP_RAND_get0_name(rand));
        BIO_printf(bio_out, " @ %s\n",
                   OSSL_PROVIDER_get0_name(EVP_RAND_get0_provider(rand)));

        switch (EVP_RAND_get_state(drbg)) {
        case EVP_RAND_STATE_UNINITIALISED:
            p = "uninitialised";
            break;
        case EVP_RAND_STATE_READY:
            p = "ready";
            break;
        case EVP_RAND_STATE_ERROR:
            p = "error";
            break;
        default:
            p = "unknown";
            break;
        }
        BIO_printf(bio_out, "  state = %s\n", p);

        gettables = EVP_RAND_gettable_ctx_params(rand);
        if (gettables != NULL)
            for (; gettables->key != NULL; gettables++) {
                /* State has been dealt with already, so ignore */
                if (OPENSSL_strcasecmp(gettables->key, OSSL_RAND_PARAM_STATE) == 0)
                    continue;
                /* Outside of verbose mode, we skip non-string values */
                if (gettables->data_type != OSSL_PARAM_UTF8_STRING
                        && gettables->data_type != OSSL_PARAM_UTF8_PTR
                        && !verbose)
                    continue;
                params->key = gettables->key;
                params->data_type = gettables->data_type;
                if (gettables->data_type == OSSL_PARAM_UNSIGNED_INTEGER
                        || gettables->data_type == OSSL_PARAM_INTEGER) {
                    params->data = &u;
                    params->data_size = sizeof(u);
                } else {
                    params->data = buf;
                    params->data_size = sizeof(buf);
                }
                params->return_size = 0;
                if (EVP_RAND_CTX_get_params(drbg, params))
                    print_param_value(params, 2);
            }
    }
}

static void list_random_instances(void)
{
    display_random("primary", RAND_get0_primary(NULL));
    display_random("public", RAND_get0_public(NULL));
    display_random("private", RAND_get0_private(NULL));
}

/*
 * Encoders
 */
DEFINE_STACK_OF(OSSL_ENCODER)
static int encoder_cmp(const OSSL_ENCODER * const *a,
                       const OSSL_ENCODER * const *b)
{
    return strcmp(OSSL_PROVIDER_get0_name(OSSL_ENCODER_get0_provider(*a)),
                  OSSL_PROVIDER_get0_name(OSSL_ENCODER_get0_provider(*b)));
}

static void collect_encoders(OSSL_ENCODER *encoder, void *stack)
{
    STACK_OF(OSSL_ENCODER) *encoder_stack = stack;

    if (is_encoder_fetchable(encoder)
            && sk_OSSL_ENCODER_push(encoder_stack, encoder) > 0)
        OSSL_ENCODER_up_ref(encoder);
}

static void list_encoders(void)
{
    STACK_OF(OSSL_ENCODER) *encoders;
    int i;

    encoders = sk_OSSL_ENCODER_new(encoder_cmp);
    if (encoders == NULL) {
        BIO_printf(bio_err, "ERROR: Memory allocation\n");
        return;
    }
    BIO_printf(bio_out, "Provided ENCODERs:\n");
    OSSL_ENCODER_do_all_provided(app_get0_libctx(), collect_encoders,
                                 encoders);
    sk_OSSL_ENCODER_sort(encoders);

    for (i = 0; i < sk_OSSL_ENCODER_num(encoders); i++) {
        OSSL_ENCODER *k = sk_OSSL_ENCODER_value(encoders, i);
        STACK_OF(OPENSSL_CSTRING) *names = NULL;

        if (select_name != NULL && !OSSL_ENCODER_is_a(k, select_name))
            continue;

        names = sk_OPENSSL_CSTRING_new(name_cmp);
        if (names != NULL && OSSL_ENCODER_names_do_all(k, collect_names, names)) {
            BIO_printf(bio_out, "  ");
            print_names(bio_out, names);

            BIO_printf(bio_out, " @ %s (%s)\n",
                    OSSL_PROVIDER_get0_name(OSSL_ENCODER_get0_provider(k)),
                    OSSL_ENCODER_get0_properties(k));

            if (verbose) {
                const char *desc = OSSL_ENCODER_get0_description(k);

                if (desc != NULL)
                    BIO_printf(bio_out, "    description: %s\n", desc);
                print_param_types("settable operation parameters",
                                OSSL_ENCODER_settable_ctx_params(k), 4);
            }
        }
        sk_OPENSSL_CSTRING_free(names);
    }
    sk_OSSL_ENCODER_pop_free(encoders, OSSL_ENCODER_free);
}

/*
 * Decoders
 */
DEFINE_STACK_OF(OSSL_DECODER)
static int decoder_cmp(const OSSL_DECODER * const *a,
                       const OSSL_DECODER * const *b)
{
    return strcmp(OSSL_PROVIDER_get0_name(OSSL_DECODER_get0_provider(*a)),
                  OSSL_PROVIDER_get0_name(OSSL_DECODER_get0_provider(*b)));
}

static void collect_decoders(OSSL_DECODER *decoder, void *stack)
{
    STACK_OF(OSSL_DECODER) *decoder_stack = stack;

    if (is_decoder_fetchable(decoder)
            && sk_OSSL_DECODER_push(decoder_stack, decoder) > 0)
        OSSL_DECODER_up_ref(decoder);
}

static void list_decoders(void)
{
    STACK_OF(OSSL_DECODER) *decoders;
    int i;

    decoders = sk_OSSL_DECODER_new(decoder_cmp);
    if (decoders == NULL) {
        BIO_printf(bio_err, "ERROR: Memory allocation\n");
        return;
    }
    BIO_printf(bio_out, "Provided DECODERs:\n");
    OSSL_DECODER_do_all_provided(app_get0_libctx(), collect_decoders,
                                 decoders);
    sk_OSSL_DECODER_sort(decoders);

    for (i = 0; i < sk_OSSL_DECODER_num(decoders); i++) {
        OSSL_DECODER *k = sk_OSSL_DECODER_value(decoders, i);
        STACK_OF(OPENSSL_CSTRING) *names = NULL;

        if (select_name != NULL && !OSSL_DECODER_is_a(k, select_name))
            continue;

        names = sk_OPENSSL_CSTRING_new(name_cmp);
        if (names != NULL && OSSL_DECODER_names_do_all(k, collect_names, names)) {
            BIO_printf(bio_out, "  ");
            print_names(bio_out, names);

            BIO_printf(bio_out, " @ %s (%s)\n",
                       OSSL_PROVIDER_get0_name(OSSL_DECODER_get0_provider(k)),
                       OSSL_DECODER_get0_properties(k));

            if (verbose) {
                const char *desc = OSSL_DECODER_get0_description(k);

                if (desc != NULL)
                    BIO_printf(bio_out, "    description: %s\n", desc);
                print_param_types("settable operation parameters",
                                OSSL_DECODER_settable_ctx_params(k), 4);
            }
        }
        sk_OPENSSL_CSTRING_free(names);
    }
    sk_OSSL_DECODER_pop_free(decoders, OSSL_DECODER_free);
}

DEFINE_STACK_OF(EVP_KEYMGMT)
static int keymanager_cmp(const EVP_KEYMGMT * const *a,
                          const EVP_KEYMGMT * const *b)
{
    return strcmp(OSSL_PROVIDER_get0_name(EVP_KEYMGMT_get0_provider(*a)),
                  OSSL_PROVIDER_get0_name(EVP_KEYMGMT_get0_provider(*b)));
}

static void collect_keymanagers(EVP_KEYMGMT *km, void *stack)
{
    STACK_OF(EVP_KEYMGMT) *km_stack = stack;

    if (is_keymgmt_fetchable(km)
            && sk_EVP_KEYMGMT_push(km_stack, km) > 0)
        EVP_KEYMGMT_up_ref(km);
}

static void list_keymanagers(void)
{
    int i;
    STACK_OF(EVP_KEYMGMT) *km_stack = sk_EVP_KEYMGMT_new(keymanager_cmp);

    EVP_KEYMGMT_do_all_provided(app_get0_libctx(), collect_keymanagers,
                                km_stack);
    sk_EVP_KEYMGMT_sort(km_stack);

    for (i = 0; i < sk_EVP_KEYMGMT_num(km_stack); i++) {
        EVP_KEYMGMT *k = sk_EVP_KEYMGMT_value(km_stack, i);
        STACK_OF(OPENSSL_CSTRING) *names = NULL;

        if (select_name != NULL && !EVP_KEYMGMT_is_a(k, select_name))
            continue;

        names = sk_OPENSSL_CSTRING_new(name_cmp);
        if (names != NULL && EVP_KEYMGMT_names_do_all(k, collect_names, names)) {
            const char *desc = EVP_KEYMGMT_get0_description(k);

            BIO_printf(bio_out, "  Name: ");
            if (desc != NULL)
                BIO_printf(bio_out, "%s", desc);
            else
                BIO_printf(bio_out, "%s", sk_OPENSSL_CSTRING_value(names, 0));
            BIO_printf(bio_out, "\n");
            BIO_printf(bio_out, "    Type: Provider Algorithm\n");
            BIO_printf(bio_out, "    IDs: ");
            print_names(bio_out, names);
            BIO_printf(bio_out, " @ %s\n",
                    OSSL_PROVIDER_get0_name(EVP_KEYMGMT_get0_provider(k)));

            if (verbose) {
                print_param_types("settable key generation parameters",
                                EVP_KEYMGMT_gen_settable_params(k), 4);
                print_param_types("settable operation parameters",
                                EVP_KEYMGMT_settable_params(k), 4);
                print_param_types("retrievable operation parameters",
                                EVP_KEYMGMT_gettable_params(k), 4);
            }
        }
        sk_OPENSSL_CSTRING_free(names);
    }
    sk_EVP_KEYMGMT_pop_free(km_stack, EVP_KEYMGMT_free);
}

DEFINE_STACK_OF(EVP_SIGNATURE)
static int signature_cmp(const EVP_SIGNATURE * const *a,
                         const EVP_SIGNATURE * const *b)
{
    return strcmp(OSSL_PROVIDER_get0_name(EVP_SIGNATURE_get0_provider(*a)),
                  OSSL_PROVIDER_get0_name(EVP_SIGNATURE_get0_provider(*b)));
}

static void collect_signatures(EVP_SIGNATURE *sig, void *stack)
{
    STACK_OF(EVP_SIGNATURE) *sig_stack = stack;

    if (is_signature_fetchable(sig)
            && sk_EVP_SIGNATURE_push(sig_stack, sig) > 0)
        EVP_SIGNATURE_up_ref(sig);
}

static void list_signatures(void)
{
    int i, count = 0;
    STACK_OF(EVP_SIGNATURE) *sig_stack = sk_EVP_SIGNATURE_new(signature_cmp);

    EVP_SIGNATURE_do_all_provided(app_get0_libctx(), collect_signatures,
                                  sig_stack);
    sk_EVP_SIGNATURE_sort(sig_stack);

    for (i = 0; i < sk_EVP_SIGNATURE_num(sig_stack); i++) {
        EVP_SIGNATURE *k = sk_EVP_SIGNATURE_value(sig_stack, i);
        STACK_OF(OPENSSL_CSTRING) *names = NULL;

        if (select_name != NULL && !EVP_SIGNATURE_is_a(k, select_name))
            continue;

        names = sk_OPENSSL_CSTRING_new(name_cmp);
        if (names != NULL && EVP_SIGNATURE_names_do_all(k, collect_names, names)) {
            count++;
            BIO_printf(bio_out, "  ");
            print_names(bio_out, names);

            BIO_printf(bio_out, " @ %s\n",
                    OSSL_PROVIDER_get0_name(EVP_SIGNATURE_get0_provider(k)));

            if (verbose) {
                const char *desc = EVP_SIGNATURE_get0_description(k);

                if (desc != NULL)
                    BIO_printf(bio_out, "    description: %s\n", desc);
                print_param_types("settable operation parameters",
                                EVP_SIGNATURE_settable_ctx_params(k), 4);
                print_param_types("retrievable operation parameters",
                                EVP_SIGNATURE_gettable_ctx_params(k), 4);
            }
        }
        sk_OPENSSL_CSTRING_free(names);
    }
    sk_EVP_SIGNATURE_pop_free(sig_stack, EVP_SIGNATURE_free);
    if (count == 0)
        BIO_printf(bio_out, " -\n");
}

DEFINE_STACK_OF(EVP_KEM)
static int kem_cmp(const EVP_KEM * const *a,
                   const EVP_KEM * const *b)
{
    return strcmp(OSSL_PROVIDER_get0_name(EVP_KEM_get0_provider(*a)),
                  OSSL_PROVIDER_get0_name(EVP_KEM_get0_provider(*b)));
}

static void collect_kem(EVP_KEM *kem, void *stack)
{
    STACK_OF(EVP_KEM) *kem_stack = stack;

    if (is_kem_fetchable(kem)
            && sk_EVP_KEM_push(kem_stack, kem) > 0)
        EVP_KEM_up_ref(kem);
}

static void list_kems(void)
{
    int i, count = 0;
    STACK_OF(EVP_KEM) *kem_stack = sk_EVP_KEM_new(kem_cmp);

    EVP_KEM_do_all_provided(app_get0_libctx(), collect_kem, kem_stack);
    sk_EVP_KEM_sort(kem_stack);

    for (i = 0; i < sk_EVP_KEM_num(kem_stack); i++) {
        EVP_KEM *k = sk_EVP_KEM_value(kem_stack, i);
        STACK_OF(OPENSSL_CSTRING) *names = NULL;

        if (select_name != NULL && !EVP_KEM_is_a(k, select_name))
            continue;

        names = sk_OPENSSL_CSTRING_new(name_cmp);
        if (names != NULL && EVP_KEM_names_do_all(k, collect_names, names)) {
            count++;
            BIO_printf(bio_out, "  ");
            print_names(bio_out, names);

            BIO_printf(bio_out, " @ %s\n",
                       OSSL_PROVIDER_get0_name(EVP_KEM_get0_provider(k)));

            if (verbose) {
                const char *desc = EVP_KEM_get0_description(k);

                if (desc != NULL)
                    BIO_printf(bio_out, "    description: %s\n", desc);
                print_param_types("settable operation parameters",
                                EVP_KEM_settable_ctx_params(k), 4);
                print_param_types("retrievable operation parameters",
                                EVP_KEM_gettable_ctx_params(k), 4);
            }
        }
        sk_OPENSSL_CSTRING_free(names);
    }
    sk_EVP_KEM_pop_free(kem_stack, EVP_KEM_free);
    if (count == 0)
        BIO_printf(bio_out, " -\n");
}

DEFINE_STACK_OF(EVP_ASYM_CIPHER)
static int asymcipher_cmp(const EVP_ASYM_CIPHER * const *a,
                          const EVP_ASYM_CIPHER * const *b)
{
    return strcmp(OSSL_PROVIDER_get0_name(EVP_ASYM_CIPHER_get0_provider(*a)),
                  OSSL_PROVIDER_get0_name(EVP_ASYM_CIPHER_get0_provider(*b)));
}

static void collect_asymciph(EVP_ASYM_CIPHER *asym_cipher, void *stack)
{
    STACK_OF(EVP_ASYM_CIPHER) *asym_cipher_stack = stack;

    if (is_asym_cipher_fetchable(asym_cipher)
            && sk_EVP_ASYM_CIPHER_push(asym_cipher_stack, asym_cipher) > 0)
        EVP_ASYM_CIPHER_up_ref(asym_cipher);
}

static void list_asymciphers(void)
{
    int i, count = 0;
    STACK_OF(EVP_ASYM_CIPHER) *asymciph_stack =
        sk_EVP_ASYM_CIPHER_new(asymcipher_cmp);

    EVP_ASYM_CIPHER_do_all_provided(app_get0_libctx(), collect_asymciph,
                                    asymciph_stack);
    sk_EVP_ASYM_CIPHER_sort(asymciph_stack);

    for (i = 0; i < sk_EVP_ASYM_CIPHER_num(asymciph_stack); i++) {
        EVP_ASYM_CIPHER *k = sk_EVP_ASYM_CIPHER_value(asymciph_stack, i);
        STACK_OF(OPENSSL_CSTRING) *names = NULL;

        if (select_name != NULL && !EVP_ASYM_CIPHER_is_a(k, select_name))
            continue;

        names = sk_OPENSSL_CSTRING_new(name_cmp);
        if (names != NULL
                && EVP_ASYM_CIPHER_names_do_all(k, collect_names, names)) {
            count++;
            BIO_printf(bio_out, "  ");
            print_names(bio_out, names);

            BIO_printf(bio_out, " @ %s\n",
                    OSSL_PROVIDER_get0_name(EVP_ASYM_CIPHER_get0_provider(k)));

            if (verbose) {
                const char *desc = EVP_ASYM_CIPHER_get0_description(k);

                if (desc != NULL)
                    BIO_printf(bio_out, "    description: %s\n", desc);
                print_param_types("settable operation parameters",
                                EVP_ASYM_CIPHER_settable_ctx_params(k), 4);
                print_param_types("retrievable operation parameters",
                                EVP_ASYM_CIPHER_gettable_ctx_params(k), 4);
            }
        }
        sk_OPENSSL_CSTRING_free(names);
    }
    sk_EVP_ASYM_CIPHER_pop_free(asymciph_stack, EVP_ASYM_CIPHER_free);
    if (count == 0)
        BIO_printf(bio_out, " -\n");
}

DEFINE_STACK_OF(EVP_KEYEXCH)
static int kex_cmp(const EVP_KEYEXCH * const *a,
                   const EVP_KEYEXCH * const *b)
{
    return strcmp(OSSL_PROVIDER_get0_name(EVP_KEYEXCH_get0_provider(*a)),
                  OSSL_PROVIDER_get0_name(EVP_KEYEXCH_get0_provider(*b)));
}

static void collect_kex(EVP_KEYEXCH *kex, void *stack)
{
    STACK_OF(EVP_KEYEXCH) *kex_stack = stack;

    if (is_keyexch_fetchable(kex)
            && sk_EVP_KEYEXCH_push(kex_stack, kex) > 0)
        EVP_KEYEXCH_up_ref(kex);
}

static void list_keyexchanges(void)
{
    int i, count = 0;
    STACK_OF(EVP_KEYEXCH) *kex_stack = sk_EVP_KEYEXCH_new(kex_cmp);

    EVP_KEYEXCH_do_all_provided(app_get0_libctx(), collect_kex, kex_stack);
    sk_EVP_KEYEXCH_sort(kex_stack);

    for (i = 0; i < sk_EVP_KEYEXCH_num(kex_stack); i++) {
        EVP_KEYEXCH *k = sk_EVP_KEYEXCH_value(kex_stack, i);
        STACK_OF(OPENSSL_CSTRING) *names = NULL;

        if (select_name != NULL && !EVP_KEYEXCH_is_a(k, select_name))
            continue;

        names = sk_OPENSSL_CSTRING_new(name_cmp);
        if (names != NULL && EVP_KEYEXCH_names_do_all(k, collect_names, names)) {
            count++;
            BIO_printf(bio_out, "  ");
            print_names(bio_out, names);

            BIO_printf(bio_out, " @ %s\n",
                    OSSL_PROVIDER_get0_name(EVP_KEYEXCH_get0_provider(k)));

            if (verbose) {
                const char *desc = EVP_KEYEXCH_get0_description(k);

                if (desc != NULL)
                    BIO_printf(bio_out, "    description: %s\n", desc);
                print_param_types("settable operation parameters",
                                EVP_KEYEXCH_settable_ctx_params(k), 4);
                print_param_types("retrievable operation parameters",
                                EVP_KEYEXCH_gettable_ctx_params(k), 4);
            }
        }
        sk_OPENSSL_CSTRING_free(names);
    }
    sk_EVP_KEYEXCH_pop_free(kex_stack, EVP_KEYEXCH_free);
    if (count == 0)
        BIO_printf(bio_out, " -\n");
}

static void list_objects(void)
{
    int max_nid = OBJ_new_nid(0);
    int i;
    char *oid_buf = NULL;
    int oid_size = 0;

    /* Skip 0, since that's NID_undef */
    for (i = 1; i < max_nid; i++) {
        const ASN1_OBJECT *obj = OBJ_nid2obj(i);
        const char *sn = OBJ_nid2sn(i);
        const char *ln = OBJ_nid2ln(i);
        int n = 0;

        /*
         * If one of the retrieved objects somehow generated an error,
         * we ignore it.  The check for NID_undef below will detect the
         * error and simply skip to the next NID.
         */
        ERR_clear_error();

        if (OBJ_obj2nid(obj) == NID_undef)
            continue;

        if ((n = OBJ_obj2txt(NULL, 0, obj, 1)) == 0) {
            BIO_printf(bio_out, "# None-OID object: %s, %s\n", sn, ln);
            continue;
        }
        if (n < 0)
            break;               /* Error */

        if (n > oid_size) {
            oid_buf = OPENSSL_realloc(oid_buf, n + 1);
            if (oid_buf == NULL) {
                BIO_printf(bio_err, "ERROR: Memory allocation\n");
                break;           /* Error */
            }
            oid_size = n + 1;
        }
        if (OBJ_obj2txt(oid_buf, oid_size, obj, 1) < 0)
            break;               /* Error */
        if (ln == NULL || strcmp(sn, ln) == 0)
            BIO_printf(bio_out, "%s = %s\n", sn, oid_buf);
        else
            BIO_printf(bio_out, "%s = %s, %s\n", sn, ln, oid_buf);
    }

    OPENSSL_free(oid_buf);
}

static void list_options_for_command(const char *command)
{
    const FUNCTION *fp;
    const OPTIONS *o;

    for (fp = functions; fp->name != NULL; fp++)
        if (strcmp(fp->name, command) == 0)
            break;
    if (fp->name == NULL) {
        BIO_printf(bio_err, "Invalid command '%s'; type \"help\" for a list.\n",
                   command);
        return;
    }

    if ((o = fp->help) == NULL)
        return;

    for ( ; o->name != NULL; o++) {
        char c = o->valtype;

        if (o->name == OPT_PARAM_STR)
            break;

        if (o->name == OPT_HELP_STR
                || o->name == OPT_MORE_STR
                || o->name == OPT_SECTION_STR
                || o->name[0] == '\0')
            continue;
        BIO_printf(bio_out, "%s %c\n", o->name, c == '\0' ? '-' : c);
    }
    /* Always output the -- marker since it is sometimes documented. */
    BIO_printf(bio_out, "- -\n");
}

static int is_md_available(const char *name)
{
    EVP_MD *md;
    const char *propq = app_get0_propq();

    /* Look through providers' digests */
    ERR_set_mark();
    md = EVP_MD_fetch(app_get0_libctx(), name, propq);
    ERR_pop_to_mark();
    if (md != NULL) {
        EVP_MD_free(md);
        return 1;
    }

    return propq != NULL || get_digest_from_engine(name) == NULL ? 0 : 1;
}

static int is_cipher_available(const char *name)
{
    EVP_CIPHER *cipher;
    const char *propq = app_get0_propq();

    /* Look through providers' ciphers */
    ERR_set_mark();
    cipher = EVP_CIPHER_fetch(app_get0_libctx(), name, propq);
    ERR_pop_to_mark();
    if (cipher != NULL) {
        EVP_CIPHER_free(cipher);
        return 1;
    }

    return propq != NULL || get_cipher_from_engine(name) == NULL ? 0 : 1;
}

static void list_type(FUNC_TYPE ft, int one)
{
    FUNCTION *fp;
    int i = 0;
    DISPLAY_COLUMNS dc;

    memset(&dc, 0, sizeof(dc));
    if (!one)
        calculate_columns(functions, &dc);

    for (fp = functions; fp->name != NULL; fp++) {
        if (fp->type != ft)
            continue;
        switch (ft) {
        case FT_cipher:
            if (!is_cipher_available(fp->name))
                continue;
            break;
        case FT_md:
            if (!is_md_available(fp->name))
                continue;
            break;
        default:
            break;
        }
        if (one) {
            BIO_printf(bio_out, "%s\n", fp->name);
        } else {
            if (i % dc.columns == 0 && i > 0)
                BIO_printf(bio_out, "\n");
            BIO_printf(bio_out, "%-*s", dc.width, fp->name);
            i++;
        }
    }
    if (!one)
        BIO_printf(bio_out, "\n\n");
}

static void list_pkey(void)
{
#ifndef OPENSSL_NO_DEPRECATED_3_0
    int i;

    if (select_name == NULL && include_legacy()) {
        BIO_printf(bio_out, "Legacy:\n");
        for (i = 0; i < EVP_PKEY_asn1_get_count(); i++) {
            const EVP_PKEY_ASN1_METHOD *ameth;
            int pkey_id, pkey_base_id, pkey_flags;
            const char *pinfo, *pem_str;
            ameth = EVP_PKEY_asn1_get0(i);
            EVP_PKEY_asn1_get0_info(&pkey_id, &pkey_base_id, &pkey_flags,
                                    &pinfo, &pem_str, ameth);
            if (pkey_flags & ASN1_PKEY_ALIAS) {
                BIO_printf(bio_out, " Name: %s\n", OBJ_nid2ln(pkey_id));
                BIO_printf(bio_out, "\tAlias for: %s\n",
                           OBJ_nid2ln(pkey_base_id));
            } else {
                BIO_printf(bio_out, " Name: %s\n", pinfo);
                BIO_printf(bio_out, "\tType: %s Algorithm\n",
                           pkey_flags & ASN1_PKEY_DYNAMIC ?
                           "External" : "Builtin");
                BIO_printf(bio_out, "\tOID: %s\n", OBJ_nid2ln(pkey_id));
                if (pem_str == NULL)
                    pem_str = "(none)";
                BIO_printf(bio_out, "\tPEM string: %s\n", pem_str);
            }
        }
    }
#endif
    BIO_printf(bio_out, "Provided:\n");
    BIO_printf(bio_out, " Key Managers:\n");
    list_keymanagers();
}

static void list_pkey_meth(void)
{
#ifndef OPENSSL_NO_DEPRECATED_3_0
    size_t i;
    size_t meth_count = EVP_PKEY_meth_get_count();

    if (select_name == NULL && include_legacy()) {
        BIO_printf(bio_out, "Legacy:\n");
        for (i = 0; i < meth_count; i++) {
            const EVP_PKEY_METHOD *pmeth = EVP_PKEY_meth_get0(i);
            int pkey_id, pkey_flags;

            EVP_PKEY_meth_get0_info(&pkey_id, &pkey_flags, pmeth);
            BIO_printf(bio_out, " %s\n", OBJ_nid2ln(pkey_id));
            BIO_printf(bio_out, "\tType: %s Algorithm\n",
                       pkey_flags & ASN1_PKEY_DYNAMIC ?  "External" : "Builtin");
        }
    }
#endif
    BIO_printf(bio_out, "Provided:\n");
    BIO_printf(bio_out, " Encryption:\n");
    list_asymciphers();
    BIO_printf(bio_out, " Key Exchange:\n");
    list_keyexchanges();
    BIO_printf(bio_out, " Signatures:\n");
    list_signatures();
    BIO_printf(bio_out, " Key encapsulation:\n");
    list_kems();
}

DEFINE_STACK_OF(OSSL_STORE_LOADER)
static int store_cmp(const OSSL_STORE_LOADER * const *a,
                     const OSSL_STORE_LOADER * const *b)
{
    return strcmp(OSSL_PROVIDER_get0_name(OSSL_STORE_LOADER_get0_provider(*a)),
                  OSSL_PROVIDER_get0_name(OSSL_STORE_LOADER_get0_provider(*b)));
}

static void collect_store_loaders(OSSL_STORE_LOADER *store, void *stack)
{
    STACK_OF(OSSL_STORE_LOADER) *store_stack = stack;

    if (sk_OSSL_STORE_LOADER_push(store_stack, store) > 0)
        OSSL_STORE_LOADER_up_ref(store);
}

static void list_store_loaders(void)
{
    STACK_OF(OSSL_STORE_LOADER) *stores = sk_OSSL_STORE_LOADER_new(store_cmp);
    int i;

    if (stores == NULL) {
        BIO_printf(bio_err, "ERROR: Memory allocation\n");
        return;
    }
    BIO_printf(bio_out, "Provided STORE LOADERs:\n");
    OSSL_STORE_LOADER_do_all_provided(app_get0_libctx(), collect_store_loaders,
                                      stores);
    sk_OSSL_STORE_LOADER_sort(stores);
    for (i = 0; i < sk_OSSL_STORE_LOADER_num(stores); i++) {
        const OSSL_STORE_LOADER *m = sk_OSSL_STORE_LOADER_value(stores, i);
        STACK_OF(OPENSSL_CSTRING) *names = NULL;

        if (select_name != NULL && !OSSL_STORE_LOADER_is_a(m, select_name))
            continue;

        names = sk_OPENSSL_CSTRING_new(name_cmp);
        if (names != NULL && OSSL_STORE_LOADER_names_do_all(m, collect_names,
                                                            names)) {
            BIO_printf(bio_out, "  ");
            print_names(bio_out, names);

            BIO_printf(bio_out, " @ %s\n",
                       OSSL_PROVIDER_get0_name(OSSL_STORE_LOADER_get0_provider(m)));
        }
        sk_OPENSSL_CSTRING_free(names);
    }
    sk_OSSL_STORE_LOADER_pop_free(stores, OSSL_STORE_LOADER_free);
}

DEFINE_STACK_OF(OSSL_PROVIDER)
static int provider_cmp(const OSSL_PROVIDER * const *a,
                        const OSSL_PROVIDER * const *b)
{
    return strcmp(OSSL_PROVIDER_get0_name(*a), OSSL_PROVIDER_get0_name(*b));
}

static int collect_providers(OSSL_PROVIDER *provider, void *stack)
{
    STACK_OF(OSSL_PROVIDER) *provider_stack = stack;
    /*
     * If OK - result is the index of inserted data
     * Error - result is -1 or 0
     */
    return sk_OSSL_PROVIDER_push(provider_stack, provider) > 0 ? 1 : 0;
}

static void list_provider_info(void)
{
    STACK_OF(OSSL_PROVIDER) *providers = sk_OSSL_PROVIDER_new(provider_cmp);
    OSSL_PARAM params[5];
    char *name, *version, *buildinfo;
    int status;
    int i;

    if (providers == NULL) {
        BIO_printf(bio_err, "ERROR: Memory allocation\n");
        return;
    }

    if (OSSL_PROVIDER_do_all(NULL, &collect_providers, providers) != 1) {
        BIO_printf(bio_err, "ERROR: Memory allocation\n");
        return;
    }

    BIO_printf(bio_out, "Providers:\n");
    sk_OSSL_PROVIDER_sort(providers);
    for (i = 0; i < sk_OSSL_PROVIDER_num(providers); i++) {
        const OSSL_PROVIDER *prov = sk_OSSL_PROVIDER_value(providers, i);
        const char *provname = OSSL_PROVIDER_get0_name(prov);

        BIO_printf(bio_out, "  %s\n", provname);

        /* Query the "known" information parameters, the order matches below */
        params[0] = OSSL_PARAM_construct_utf8_ptr(OSSL_PROV_PARAM_NAME,
                                                  &name, 0);
        params[1] = OSSL_PARAM_construct_utf8_ptr(OSSL_PROV_PARAM_VERSION,
                                                  &version, 0);
        params[2] = OSSL_PARAM_construct_int(OSSL_PROV_PARAM_STATUS, &status);
        params[3] = OSSL_PARAM_construct_utf8_ptr(OSSL_PROV_PARAM_BUILDINFO,
                                                  &buildinfo, 0);
        params[4] = OSSL_PARAM_construct_end();
        OSSL_PARAM_set_all_unmodified(params);
        if (!OSSL_PROVIDER_get_params(prov, params)) {
            BIO_printf(bio_err,
                       "WARNING: Unable to query provider parameters for %s\n",
                       provname);
        } else {
            /* Print out the provider information, the params order matches above */
            if (OSSL_PARAM_modified(params))
                BIO_printf(bio_out, "    name: %s\n", name);
            if (OSSL_PARAM_modified(params + 1))
                BIO_printf(bio_out, "    version: %s\n", version);
            if (OSSL_PARAM_modified(params + 2))
                BIO_printf(bio_out, "    status: %sactive\n", status ? "" : "in");
            if (verbose) {
                if (OSSL_PARAM_modified(params + 3))
                    BIO_printf(bio_out, "    build info: %s\n", buildinfo);
                print_param_types("gettable provider parameters",
                                  OSSL_PROVIDER_gettable_params(prov), 4);
            }
        }
    }
    sk_OSSL_PROVIDER_free(providers);
}

#ifndef OPENSSL_NO_DEPRECATED_3_0
static void list_engines(void)
{
# ifndef OPENSSL_NO_ENGINE
    ENGINE *e;

    BIO_puts(bio_out, "Engines:\n");
    e = ENGINE_get_first();
    while (e) {
        BIO_printf(bio_out, "%s\n", ENGINE_get_id(e));
        e = ENGINE_get_next(e);
    }
# else
    BIO_puts(bio_out, "Engine support is disabled.\n");
# endif
}
#endif

static void list_disabled(void)
{
    BIO_puts(bio_out, "Disabled algorithms:\n");
#ifdef OPENSSL_NO_ARIA
    BIO_puts(bio_out, "ARIA\n");
#endif
#ifdef OPENSSL_NO_BF
    BIO_puts(bio_out, "BF\n");
#endif
#ifdef OPENSSL_NO_BLAKE2
    BIO_puts(bio_out, "BLAKE2\n");
#endif
#ifdef OPENSSL_NO_CAMELLIA
    BIO_puts(bio_out, "CAMELLIA\n");
#endif
#ifdef OPENSSL_NO_CAST
    BIO_puts(bio_out, "CAST\n");
#endif
#ifdef OPENSSL_NO_CMAC
    BIO_puts(bio_out, "CMAC\n");
#endif
#ifdef OPENSSL_NO_CMS
    BIO_puts(bio_out, "CMS\n");
#endif
#ifdef OPENSSL_NO_COMP
    BIO_puts(bio_out, "COMP\n");
#endif
#ifdef OPENSSL_NO_DES
    BIO_puts(bio_out, "DES\n");
#endif
#ifdef OPENSSL_NO_DGRAM
    BIO_puts(bio_out, "DGRAM\n");
#endif
#ifdef OPENSSL_NO_DH
    BIO_puts(bio_out, "DH\n");
#endif
#ifdef OPENSSL_NO_DSA
    BIO_puts(bio_out, "DSA\n");
#endif
#if defined(OPENSSL_NO_DTLS)
    BIO_puts(bio_out, "DTLS\n");
#endif
#if defined(OPENSSL_NO_DTLS1)
    BIO_puts(bio_out, "DTLS1\n");
#endif
#if defined(OPENSSL_NO_DTLS1_2)
    BIO_puts(bio_out, "DTLS1_2\n");
#endif
#ifdef OPENSSL_NO_EC
    BIO_puts(bio_out, "EC\n");
#endif
#ifdef OPENSSL_NO_EC2M
    BIO_puts(bio_out, "EC2M\n");
#endif
#if defined(OPENSSL_NO_ENGINE) && !defined(OPENSSL_NO_DEPRECATED_3_0)
    BIO_puts(bio_out, "ENGINE\n");
#endif
#ifdef OPENSSL_NO_GOST
    BIO_puts(bio_out, "GOST\n");
#endif
#ifdef OPENSSL_NO_IDEA
    BIO_puts(bio_out, "IDEA\n");
#endif
#ifdef OPENSSL_NO_MD2
    BIO_puts(bio_out, "MD2\n");
#endif
#ifdef OPENSSL_NO_MD4
    BIO_puts(bio_out, "MD4\n");
#endif
#ifdef OPENSSL_NO_MD5
    BIO_puts(bio_out, "MD5\n");
#endif
#ifdef OPENSSL_NO_MDC2
    BIO_puts(bio_out, "MDC2\n");
#endif
#ifdef OPENSSL_NO_OCB
    BIO_puts(bio_out, "OCB\n");
#endif
#ifdef OPENSSL_NO_OCSP
    BIO_puts(bio_out, "OCSP\n");
#endif
#ifdef OPENSSL_NO_PSK
    BIO_puts(bio_out, "PSK\n");
#endif
#ifdef OPENSSL_NO_RC2
    BIO_puts(bio_out, "RC2\n");
#endif
#ifdef OPENSSL_NO_RC4
    BIO_puts(bio_out, "RC4\n");
#endif
#ifdef OPENSSL_NO_RC5
    BIO_puts(bio_out, "RC5\n");
#endif
#ifdef OPENSSL_NO_RMD160
    BIO_puts(bio_out, "RMD160\n");
#endif
#ifdef OPENSSL_NO_SCRYPT
    BIO_puts(bio_out, "SCRYPT\n");
#endif
#ifdef OPENSSL_NO_SCTP
    BIO_puts(bio_out, "SCTP\n");
#endif
#ifdef OPENSSL_NO_SEED
    BIO_puts(bio_out, "SEED\n");
#endif
#ifdef OPENSSL_NO_SM2
    BIO_puts(bio_out, "SM2\n");
#endif
#ifdef OPENSSL_NO_SM3
    BIO_puts(bio_out, "SM3\n");
#endif
#ifdef OPENSSL_NO_SM4
    BIO_puts(bio_out, "SM4\n");
#endif
#ifdef OPENSSL_NO_SOCK
    BIO_puts(bio_out, "SOCK\n");
#endif
#ifdef OPENSSL_NO_SRP
    BIO_puts(bio_out, "SRP\n");
#endif
#ifdef OPENSSL_NO_SRTP
    BIO_puts(bio_out, "SRTP\n");
#endif
#ifdef OPENSSL_NO_SSL3
    BIO_puts(bio_out, "SSL3\n");
#endif
#ifdef OPENSSL_NO_TLS1
    BIO_puts(bio_out, "TLS1\n");
#endif
#ifdef OPENSSL_NO_TLS1_1
    BIO_puts(bio_out, "TLS1_1\n");
#endif
#ifdef OPENSSL_NO_TLS1_2
    BIO_puts(bio_out, "TLS1_2\n");
#endif
#ifdef OPENSSL_NO_WHIRLPOOL
    BIO_puts(bio_out, "WHIRLPOOL\n");
#endif
#ifndef ZLIB
    BIO_puts(bio_out, "ZLIB\n");
#endif
}

/* Unified enum for help and list commands. */
typedef enum HELPLIST_CHOICE {
    OPT_COMMON,
    OPT_ONE, OPT_VERBOSE,
    OPT_COMMANDS, OPT_DIGEST_COMMANDS, OPT_MAC_ALGORITHMS, OPT_OPTIONS,
    OPT_DIGEST_ALGORITHMS, OPT_CIPHER_COMMANDS, OPT_CIPHER_ALGORITHMS,
    OPT_PK_ALGORITHMS, OPT_PK_METHOD, OPT_DISABLED,
    OPT_KDF_ALGORITHMS, OPT_RANDOM_INSTANCES, OPT_RANDOM_GENERATORS,
    OPT_ENCODERS, OPT_DECODERS, OPT_KEYMANAGERS, OPT_KEYEXCHANGE_ALGORITHMS,
    OPT_KEM_ALGORITHMS, OPT_SIGNATURE_ALGORITHMS, OPT_ASYM_CIPHER_ALGORITHMS,
    OPT_STORE_LOADERS, OPT_PROVIDER_INFO,
    OPT_OBJECTS, OPT_SELECT_NAME,
#ifndef OPENSSL_NO_DEPRECATED_3_0
    OPT_ENGINES, 
#endif
    OPT_PROV_ENUM
} HELPLIST_CHOICE;

const OPTIONS list_options[] = {

    OPT_SECTION("General"),
    {"help", OPT_HELP, '-', "Display this summary"},

    OPT_SECTION("Output"),
    {"1", OPT_ONE, '-', "List in one column"},
    {"verbose", OPT_VERBOSE, '-', "Verbose listing"},
    {"select", OPT_SELECT_NAME, 's', "Select a single algorithm"},
    {"commands", OPT_COMMANDS, '-', "List of standard commands"},
    {"standard-commands", OPT_COMMANDS, '-', "List of standard commands"},
#ifndef OPENSSL_NO_DEPRECATED_3_0
    {"digest-commands", OPT_DIGEST_COMMANDS, '-',
     "List of message digest commands (deprecated)"},
#endif
    {"digest-algorithms", OPT_DIGEST_ALGORITHMS, '-',
     "List of message digest algorithms"},
    {"kdf-algorithms", OPT_KDF_ALGORITHMS, '-',
     "List of key derivation and pseudo random function algorithms"},
    {"random-instances", OPT_RANDOM_INSTANCES, '-',
     "List the primary, public and private random number generator details"},
    {"random-generators", OPT_RANDOM_GENERATORS, '-',
     "List of random number generators"},
    {"mac-algorithms", OPT_MAC_ALGORITHMS, '-',
     "List of message authentication code algorithms"},
#ifndef OPENSSL_NO_DEPRECATED_3_0
    {"cipher-commands", OPT_CIPHER_COMMANDS, '-', 
    "List of cipher commands (deprecated)"},
#endif
    {"cipher-algorithms", OPT_CIPHER_ALGORITHMS, '-',
     "List of symmetric cipher algorithms"},
    {"encoders", OPT_ENCODERS, '-', "List of encoding methods" },
    {"decoders", OPT_DECODERS, '-', "List of decoding methods" },
    {"key-managers", OPT_KEYMANAGERS, '-', "List of key managers" },
    {"key-exchange-algorithms", OPT_KEYEXCHANGE_ALGORITHMS, '-',
     "List of key exchange algorithms" },
    {"kem-algorithms", OPT_KEM_ALGORITHMS, '-',
     "List of key encapsulation mechanism algorithms" },
    {"signature-algorithms", OPT_SIGNATURE_ALGORITHMS, '-',
     "List of signature algorithms" },
    {"asymcipher-algorithms", OPT_ASYM_CIPHER_ALGORITHMS, '-',
      "List of asymmetric cipher algorithms" },
    {"public-key-algorithms", OPT_PK_ALGORITHMS, '-',
     "List of public key algorithms"},
    {"public-key-methods", OPT_PK_METHOD, '-',
     "List of public key methods"},
    {"store-loaders", OPT_STORE_LOADERS, '-',
     "List of store loaders"},
    {"providers", OPT_PROVIDER_INFO, '-',
     "List of provider information"},
#ifndef OPENSSL_NO_DEPRECATED_3_0
    {"engines", OPT_ENGINES, '-',
     "List of loaded engines"},
#endif
    {"disabled", OPT_DISABLED, '-', "List of disabled features"},
    {"options", OPT_OPTIONS, 's',
     "List options for specified command"},
    {"objects", OPT_OBJECTS, '-',
     "List built in objects (OID<->name mappings)"},

    OPT_PROV_OPTIONS,
    {NULL}
};

int list_main(int argc, char **argv)
{
    char *prog;
    HELPLIST_CHOICE o;
    int one = 0, done = 0;
    struct {
        unsigned int commands:1;
        unsigned int random_instances:1;
        unsigned int random_generators:1;
        unsigned int digest_commands:1;
        unsigned int digest_algorithms:1;
        unsigned int kdf_algorithms:1;
        unsigned int mac_algorithms:1;
        unsigned int cipher_commands:1;
        unsigned int cipher_algorithms:1;
        unsigned int encoder_algorithms:1;
        unsigned int decoder_algorithms:1;
        unsigned int keymanager_algorithms:1;
        unsigned int signature_algorithms:1;
        unsigned int keyexchange_algorithms:1;
        unsigned int kem_algorithms:1;
        unsigned int asym_cipher_algorithms:1;
        unsigned int pk_algorithms:1;
        unsigned int pk_method:1;
        unsigned int store_loaders:1;
        unsigned int provider_info:1;
#ifndef OPENSSL_NO_DEPRECATED_3_0
        unsigned int engines:1;
#endif
        unsigned int disabled:1;
        unsigned int objects:1;
        unsigned int options:1;
    } todo = { 0, };

    verbose = 0;                 /* Clear a possible previous call */

    prog = opt_init(argc, argv, list_options);
    while ((o = opt_next()) != OPT_EOF) {
        switch (o) {
        case OPT_EOF:  /* Never hit, but suppresses warning */
        case OPT_ERR:
opthelp:
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            return 1;
        case OPT_HELP:
            opt_help(list_options);
            return 0;
        case OPT_ONE:
            one = 1;
            break;
        case OPT_COMMANDS:
            todo.commands = 1;
            break;
        case OPT_DIGEST_COMMANDS:
            todo.digest_commands = 1;
            break;
        case OPT_DIGEST_ALGORITHMS:
            todo.digest_algorithms = 1;
            break;
        case OPT_KDF_ALGORITHMS:
            todo.kdf_algorithms = 1;
            break;
        case OPT_RANDOM_INSTANCES:
            todo.random_instances = 1;
            break;
        case OPT_RANDOM_GENERATORS:
            todo.random_generators = 1;
            break;
        case OPT_MAC_ALGORITHMS:
            todo.mac_algorithms = 1;
            break;
        case OPT_CIPHER_COMMANDS:
            todo.cipher_commands = 1;
            break;
        case OPT_CIPHER_ALGORITHMS:
            todo.cipher_algorithms = 1;
            break;
        case OPT_ENCODERS:
            todo.encoder_algorithms = 1;
            break;
        case OPT_DECODERS:
            todo.decoder_algorithms = 1;
            break;
        case OPT_KEYMANAGERS:
            todo.keymanager_algorithms = 1;
            break;
        case OPT_SIGNATURE_ALGORITHMS:
            todo.signature_algorithms = 1;
            break;
        case OPT_KEYEXCHANGE_ALGORITHMS:
            todo.keyexchange_algorithms = 1;
            break;
        case OPT_KEM_ALGORITHMS:
            todo.kem_algorithms = 1;
            break;
        case OPT_ASYM_CIPHER_ALGORITHMS:
            todo.asym_cipher_algorithms = 1;
            break;
        case OPT_PK_ALGORITHMS:
            todo.pk_algorithms = 1;
            break;
        case OPT_PK_METHOD:
            todo.pk_method = 1;
            break;
        case OPT_STORE_LOADERS:
            todo.store_loaders = 1;
            break;
        case OPT_PROVIDER_INFO:
            todo.provider_info = 1;
            break;
#ifndef OPENSSL_NO_DEPRECATED_3_0
        case OPT_ENGINES:
            todo.engines = 1;
            break;
#endif
        case OPT_DISABLED:
            todo.disabled = 1;
            break;
        case OPT_OBJECTS:
            todo.objects = 1;
            break;
        case OPT_OPTIONS:
            list_options_for_command(opt_arg());
            break;
        case OPT_VERBOSE:
            verbose = 1;
            break;
        case OPT_SELECT_NAME:
            select_name = opt_arg();
            break;
        case OPT_PROV_CASES:
            if (!opt_provider(o))
                return 1;
            break;
        }
        done = 1;
    }

    /* No extra arguments. */
    if (opt_num_rest() != 0)
        goto opthelp;

    if (todo.commands)
        list_type(FT_general, one);
    if (todo.random_instances)
        list_random_instances();
    if (todo.random_generators)
        list_random_generators();
    if (todo.digest_commands)
        list_type(FT_md, one);
    if (todo.digest_algorithms)
        list_digests();
    if (todo.kdf_algorithms)
        list_kdfs();
    if (todo.mac_algorithms)
        list_macs();
    if (todo.cipher_commands)
        list_type(FT_cipher, one);
    if (todo.cipher_algorithms)
        list_ciphers();
    if (todo.encoder_algorithms)
        list_encoders();
    if (todo.decoder_algorithms)
        list_decoders();
    if (todo.keymanager_algorithms)
        list_keymanagers();
    if (todo.signature_algorithms)
        list_signatures();
    if (todo.asym_cipher_algorithms)
        list_asymciphers();
    if (todo.keyexchange_algorithms)
        list_keyexchanges();
    if (todo.kem_algorithms)
        list_kems();
    if (todo.pk_algorithms)
        list_pkey();
    if (todo.pk_method)
        list_pkey_meth();
    if (todo.store_loaders)
        list_store_loaders();
    if (todo.provider_info)
        list_provider_info();
#ifndef OPENSSL_NO_DEPRECATED_3_0
    if (todo.engines)
        list_engines();
#endif
    if (todo.disabled)
        list_disabled();
    if (todo.objects)
        list_objects();

    if (!done)
        goto opthelp;

    return 0;
}
