/*
 * Copyright 2019-2023 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <assert.h>
#include <openssl/core.h>
#include <openssl/core_dispatch.h>
#include <openssl/core_names.h>
#include <openssl/provider.h>
#include <openssl/params.h>
#include <openssl/opensslv.h>
#include "crypto/cryptlib.h"
#ifndef FIPS_MODULE
#include "crypto/decoder.h" /* ossl_decoder_store_cache_flush */
#include "crypto/encoder.h" /* ossl_encoder_store_cache_flush */
#include "crypto/store.h" /* ossl_store_loader_store_cache_flush */
#endif
#include "crypto/evp.h" /* evp_method_store_cache_flush */
#include "crypto/rand.h"
#include "internal/nelem.h"
#include "internal/thread_once.h"
#include "internal/provider.h"
#include "internal/refcount.h"
#include "internal/bio.h"
#include "internal/core.h"
#include "provider_local.h"
#ifndef FIPS_MODULE
# include <openssl/self_test.h>
#endif

/*
 * This file defines and uses a number of different structures:
 *
 * OSSL_PROVIDER (provider_st): Used to represent all information related to a
 * single instance of a provider.
 *
 * provider_store_st: Holds information about the collection of providers that
 * are available within the current library context (OSSL_LIB_CTX). It also
 * holds configuration information about providers that could be loaded at some
 * future point.
 *
 * OSSL_PROVIDER_CHILD_CB: An instance of this structure holds the callbacks
 * that have been registered for a child library context and the associated
 * provider that registered those callbacks.
 *
 * Where a child library context exists then it has its own instance of the
 * provider store. Each provider that exists in the parent provider store, has
 * an associated child provider in the child library context's provider store.
 * As providers get activated or deactivated this needs to be mirrored in the
 * associated child providers.
 *
 * LOCKING
 * =======
 *
 * There are a number of different locks used in this file and it is important
 * to understand how they should be used in order to avoid deadlocks.
 *
 * Fields within a structure can often be "write once" on creation, and then
 * "read many". Creation of a structure is done by a single thread, and
 * therefore no lock is required for the "write once/read many" fields. It is
 * safe for multiple threads to read these fields without a lock, because they
 * will never be changed.
 *
 * However some fields may be changed after a structure has been created and
 * shared between multiple threads. Where this is the case a lock is required.
 *
 * The locks available are:
 *
 * The provider flag_lock: Used to control updates to the various provider
 * "flags" (flag_initialized, flag_activated, flag_fallback) and associated
 * "counts" (activatecnt).
 *
 * The provider refcnt_lock: Only ever used to control updates to the provider
 * refcnt value.
 *
 * The provider optbits_lock: Used to control access to the provider's
 * operation_bits and operation_bits_sz fields.
 *
 * The store default_path_lock: Used to control access to the provider store's
 * default search path value (default_path)
 *
 * The store lock: Used to control the stack of provider's held within the
 * provider store, as well as the stack of registered child provider callbacks.
 *
 * As a general rule-of-thumb it is best to:
 *  - keep the scope of the code that is protected by a lock to the absolute
 *    minimum possible;
 *  - try to keep the scope of the lock to within a single function (i.e. avoid
 *    making calls to other functions while holding a lock);
 *  - try to only ever hold one lock at a time.
 *
 * Unfortunately, it is not always possible to stick to the above guidelines.
 * Where they are not adhered to there is always a danger of inadvertently
 * introducing the possibility of deadlock. The following rules MUST be adhered
 * to in order to avoid that:
 *  - Holding multiple locks at the same time is only allowed for the
 *    provider store lock, the provider flag_lock and the provider refcnt_lock.
 *  - When holding multiple locks they must be acquired in the following order of
 *    precedence:
 *        1) provider store lock
 *        2) provider flag_lock
 *        3) provider refcnt_lock
 *  - When releasing locks they must be released in the reverse order to which
 *    they were acquired
 *  - No locks may be held when making an upcall. NOTE: Some common functions
 *    can make upcalls as part of their normal operation. If you need to call
 *    some other function while holding a lock make sure you know whether it
 *    will make any upcalls or not. For example ossl_provider_up_ref() can call
 *    ossl_provider_up_ref_parent() which can call the c_prov_up_ref() upcall.
 *  - It is permissible to hold the store and flag locks when calling child
 *    provider callbacks. No other locks may be held during such callbacks.
 */

static OSSL_PROVIDER *provider_new(const char *name,
                                   OSSL_provider_init_fn *init_function,
                                   STACK_OF(INFOPAIR) *parameters);

/*-
 * Provider Object structure
 * =========================
 */

#ifndef FIPS_MODULE
typedef struct {
    OSSL_PROVIDER *prov;
    int (*create_cb)(const OSSL_CORE_HANDLE *provider, void *cbdata);
    int (*remove_cb)(const OSSL_CORE_HANDLE *provider, void *cbdata);
    int (*global_props_cb)(const char *props, void *cbdata);
    void *cbdata;
} OSSL_PROVIDER_CHILD_CB;
DEFINE_STACK_OF(OSSL_PROVIDER_CHILD_CB)
#endif

struct provider_store_st;        /* Forward declaration */

struct ossl_provider_st {
    /* Flag bits */
    unsigned int flag_initialized:1;
    unsigned int flag_activated:1;
    unsigned int flag_fallback:1; /* Can be used as fallback */

    /* Getting and setting the flags require synchronization */
    CRYPTO_RWLOCK *flag_lock;

    /* OpenSSL library side data */
    CRYPTO_REF_COUNT refcnt;
    CRYPTO_RWLOCK *refcnt_lock;  /* For the ref counter */
    int activatecnt;
    char *name;
    char *path;
    DSO *module;
    OSSL_provider_init_fn *init_function;
    STACK_OF(INFOPAIR) *parameters;
    OSSL_LIB_CTX *libctx; /* The library context this instance is in */
    struct provider_store_st *store; /* The store this instance belongs to */
#ifndef FIPS_MODULE
    /*
     * In the FIPS module inner provider, this isn't needed, since the
     * error upcalls are always direct calls to the outer provider.
     */
    int error_lib;     /* ERR library number, one for each provider */
# ifndef OPENSSL_NO_ERR
    ERR_STRING_DATA *error_strings; /* Copy of what the provider gives us */
# endif
#endif

    /* Provider side functions */
    OSSL_FUNC_provider_teardown_fn *teardown;
    OSSL_FUNC_provider_gettable_params_fn *gettable_params;
    OSSL_FUNC_provider_get_params_fn *get_params;
    OSSL_FUNC_provider_get_capabilities_fn *get_capabilities;
    OSSL_FUNC_provider_self_test_fn *self_test;
    OSSL_FUNC_provider_query_operation_fn *query_operation;
    OSSL_FUNC_provider_unquery_operation_fn *unquery_operation;

    /*
     * Cache of bit to indicate of query_operation() has been called on
     * a specific operation or not.
     */
    unsigned char *operation_bits;
    size_t operation_bits_sz;
    CRYPTO_RWLOCK *opbits_lock;

#ifndef FIPS_MODULE
    /* Whether this provider is the child of some other provider */
    const OSSL_CORE_HANDLE *handle;
    unsigned int ischild:1;
#endif

    /* Provider side data */
    void *provctx;
    const OSSL_DISPATCH *dispatch;
};
DEFINE_STACK_OF(OSSL_PROVIDER)

static int ossl_provider_cmp(const OSSL_PROVIDER * const *a,
                             const OSSL_PROVIDER * const *b)
{
    return strcmp((*a)->name, (*b)->name);
}

/*-
 * Provider Object store
 * =====================
 *
 * The Provider Object store is a library context object, and therefore needs
 * an index.
 */

struct provider_store_st {
    OSSL_LIB_CTX *libctx;
    STACK_OF(OSSL_PROVIDER) *providers;
    STACK_OF(OSSL_PROVIDER_CHILD_CB) *child_cbs;
    CRYPTO_RWLOCK *default_path_lock;
    CRYPTO_RWLOCK *lock;
    char *default_path;
    OSSL_PROVIDER_INFO *provinfo;
    size_t numprovinfo;
    size_t provinfosz;
    unsigned int use_fallbacks:1;
    unsigned int freeing:1;
};

/*
 * provider_deactivate_free() is a wrapper around ossl_provider_deactivate()
 * and ossl_provider_free(), called as needed.
 * Since this is only called when the provider store is being emptied, we
 * don't need to care about any lock.
 */
static void provider_deactivate_free(OSSL_PROVIDER *prov)
{
    if (prov->flag_activated)
        ossl_provider_deactivate(prov, 1);
    ossl_provider_free(prov);
}

#ifndef FIPS_MODULE
static void ossl_provider_child_cb_free(OSSL_PROVIDER_CHILD_CB *cb)
{
    OPENSSL_free(cb);
}
#endif

static void infopair_free(INFOPAIR *pair)
{
    OPENSSL_free(pair->name);
    OPENSSL_free(pair->value);
    OPENSSL_free(pair);
}

static INFOPAIR *infopair_copy(const INFOPAIR *src)
{
    INFOPAIR *dest = OPENSSL_zalloc(sizeof(*dest));

    if (dest == NULL)
        return NULL;
    if (src->name != NULL) {
        dest->name = OPENSSL_strdup(src->name);
        if (dest->name == NULL)
            goto err;
    }
    if (src->value != NULL) {
        dest->value = OPENSSL_strdup(src->value);
        if (dest->value == NULL)
            goto err;
    }
    return dest;
 err:
    OPENSSL_free(dest->name);
    OPENSSL_free(dest);
    return NULL;
}

void ossl_provider_info_clear(OSSL_PROVIDER_INFO *info)
{
    OPENSSL_free(info->name);
    OPENSSL_free(info->path);
    sk_INFOPAIR_pop_free(info->parameters, infopair_free);
}

static void provider_store_free(void *vstore)
{
    struct provider_store_st *store = vstore;
    size_t i;

    if (store == NULL)
        return;
    store->freeing = 1;
    OPENSSL_free(store->default_path);
    sk_OSSL_PROVIDER_pop_free(store->providers, provider_deactivate_free);
#ifndef FIPS_MODULE
    sk_OSSL_PROVIDER_CHILD_CB_pop_free(store->child_cbs,
                                       ossl_provider_child_cb_free);
#endif
    CRYPTO_THREAD_lock_free(store->default_path_lock);
    CRYPTO_THREAD_lock_free(store->lock);
    for (i = 0; i < store->numprovinfo; i++)
        ossl_provider_info_clear(&store->provinfo[i]);
    OPENSSL_free(store->provinfo);
    OPENSSL_free(store);
}

static void *provider_store_new(OSSL_LIB_CTX *ctx)
{
    struct provider_store_st *store = OPENSSL_zalloc(sizeof(*store));

    if (store == NULL
        || (store->providers = sk_OSSL_PROVIDER_new(ossl_provider_cmp)) == NULL
        || (store->default_path_lock = CRYPTO_THREAD_lock_new()) == NULL
#ifndef FIPS_MODULE
        || (store->child_cbs = sk_OSSL_PROVIDER_CHILD_CB_new_null()) == NULL
#endif
        || (store->lock = CRYPTO_THREAD_lock_new()) == NULL) {
        provider_store_free(store);
        return NULL;
    }
    store->libctx = ctx;
    store->use_fallbacks = 1;

    return store;
}

static const OSSL_LIB_CTX_METHOD provider_store_method = {
    /* Needs to be freed before the child provider data is freed */
    OSSL_LIB_CTX_METHOD_PRIORITY_1,
    provider_store_new,
    provider_store_free,
};

static struct provider_store_st *get_provider_store(OSSL_LIB_CTX *libctx)
{
    struct provider_store_st *store = NULL;

    store = ossl_lib_ctx_get_data(libctx, OSSL_LIB_CTX_PROVIDER_STORE_INDEX,
                                  &provider_store_method);
    if (store == NULL)
        ERR_raise(ERR_LIB_CRYPTO, ERR_R_INTERNAL_ERROR);
    return store;
}

int ossl_provider_disable_fallback_loading(OSSL_LIB_CTX *libctx)
{
    struct provider_store_st *store;

    if ((store = get_provider_store(libctx)) != NULL) {
        if (!CRYPTO_THREAD_write_lock(store->lock))
            return 0;
        store->use_fallbacks = 0;
        CRYPTO_THREAD_unlock(store->lock);
        return 1;
    }
    return 0;
}

#define BUILTINS_BLOCK_SIZE     10

int ossl_provider_info_add_to_store(OSSL_LIB_CTX *libctx,
                                    OSSL_PROVIDER_INFO *entry)
{
    struct provider_store_st *store = get_provider_store(libctx);
    int ret = 0;

    if (entry->name == NULL) {
        ERR_raise(ERR_LIB_CRYPTO, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }

    if (store == NULL) {
        ERR_raise(ERR_LIB_CRYPTO, ERR_R_INTERNAL_ERROR);
        return 0;
    }

    if (!CRYPTO_THREAD_write_lock(store->lock))
        return 0;
    if (store->provinfosz == 0) {
        store->provinfo = OPENSSL_zalloc(sizeof(*store->provinfo)
                                         * BUILTINS_BLOCK_SIZE);
        if (store->provinfo == NULL) {
            ERR_raise(ERR_LIB_CRYPTO, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        store->provinfosz = BUILTINS_BLOCK_SIZE;
    } else if (store->numprovinfo == store->provinfosz) {
        OSSL_PROVIDER_INFO *tmpbuiltins;
        size_t newsz = store->provinfosz + BUILTINS_BLOCK_SIZE;

        tmpbuiltins = OPENSSL_realloc(store->provinfo,
                                      sizeof(*store->provinfo) * newsz);
        if (tmpbuiltins == NULL) {
            ERR_raise(ERR_LIB_CRYPTO, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        store->provinfo = tmpbuiltins;
        store->provinfosz = newsz;
    }
    store->provinfo[store->numprovinfo] = *entry;
    store->numprovinfo++;

    ret = 1;
 err:
    CRYPTO_THREAD_unlock(store->lock);
    return ret;
}

OSSL_PROVIDER *ossl_provider_find(OSSL_LIB_CTX *libctx, const char *name,
                                  ossl_unused int noconfig)
{
    struct provider_store_st *store = NULL;
    OSSL_PROVIDER *prov = NULL;

    if ((store = get_provider_store(libctx)) != NULL) {
        OSSL_PROVIDER tmpl = { 0, };
        int i;

#if !defined(FIPS_MODULE) && !defined(OPENSSL_NO_AUTOLOAD_CONFIG)
        /*
         * Make sure any providers are loaded from config before we try to find
         * them.
         */
        if (!noconfig) {
            if (ossl_lib_ctx_is_default(libctx))
                OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CONFIG, NULL);
        }
#endif

        tmpl.name = (char *)name;
        /*
         * A "find" operation can sort the stack, and therefore a write lock is
         * required.
         */
        if (!CRYPTO_THREAD_write_lock(store->lock))
            return NULL;
        if ((i = sk_OSSL_PROVIDER_find(store->providers, &tmpl)) != -1)
            prov = sk_OSSL_PROVIDER_value(store->providers, i);
        CRYPTO_THREAD_unlock(store->lock);
        if (prov != NULL && !ossl_provider_up_ref(prov))
            prov = NULL;
    }

    return prov;
}

/*-
 * Provider Object methods
 * =======================
 */

static OSSL_PROVIDER *provider_new(const char *name,
                                   OSSL_provider_init_fn *init_function,
                                   STACK_OF(INFOPAIR) *parameters)
{
    OSSL_PROVIDER *prov = NULL;

    if ((prov = OPENSSL_zalloc(sizeof(*prov))) == NULL
#ifndef HAVE_ATOMICS
        || (prov->refcnt_lock = CRYPTO_THREAD_lock_new()) == NULL
#endif
       ) {
        OPENSSL_free(prov);
        ERR_raise(ERR_LIB_CRYPTO, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    prov->refcnt = 1; /* 1 One reference to be returned */

    if ((prov->opbits_lock = CRYPTO_THREAD_lock_new()) == NULL
        || (prov->flag_lock = CRYPTO_THREAD_lock_new()) == NULL
        || (prov->name = OPENSSL_strdup(name)) == NULL
        || (prov->parameters = sk_INFOPAIR_deep_copy(parameters,
                                                     infopair_copy,
                                                     infopair_free)) == NULL) {
        ossl_provider_free(prov);
        ERR_raise(ERR_LIB_CRYPTO, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    prov->init_function = init_function;

    return prov;
}

int ossl_provider_up_ref(OSSL_PROVIDER *prov)
{
    int ref = 0;

    if (CRYPTO_UP_REF(&prov->refcnt, &ref, prov->refcnt_lock) <= 0)
        return 0;

#ifndef FIPS_MODULE
    if (prov->ischild) {
        if (!ossl_provider_up_ref_parent(prov, 0)) {
            ossl_provider_free(prov);
            return 0;
        }
    }
#endif

    return ref;
}

#ifndef FIPS_MODULE
static int provider_up_ref_intern(OSSL_PROVIDER *prov, int activate)
{
    if (activate)
        return ossl_provider_activate(prov, 1, 0);

    return ossl_provider_up_ref(prov);
}

static int provider_free_intern(OSSL_PROVIDER *prov, int deactivate)
{
    if (deactivate)
        return ossl_provider_deactivate(prov, 1);

    ossl_provider_free(prov);
    return 1;
}
#endif

/*
 * We assume that the requested provider does not already exist in the store.
 * The caller should check. If it does exist then adding it to the store later
 * will fail.
 */
OSSL_PROVIDER *ossl_provider_new(OSSL_LIB_CTX *libctx, const char *name,
                                 OSSL_provider_init_fn *init_function,
                                 int noconfig)
{
    struct provider_store_st *store = NULL;
    OSSL_PROVIDER_INFO template;
    OSSL_PROVIDER *prov = NULL;

    if ((store = get_provider_store(libctx)) == NULL)
        return NULL;

    memset(&template, 0, sizeof(template));
    if (init_function == NULL) {
        const OSSL_PROVIDER_INFO *p;
        size_t i;

        /* Check if this is a predefined builtin provider */
        for (p = ossl_predefined_providers; p->name != NULL; p++) {
            if (strcmp(p->name, name) == 0) {
                template = *p;
                break;
            }
        }
        if (p->name == NULL) {
            /* Check if this is a user added builtin provider */
            if (!CRYPTO_THREAD_read_lock(store->lock))
                return NULL;
            for (i = 0, p = store->provinfo; i < store->numprovinfo; p++, i++) {
                if (strcmp(p->name, name) == 0) {
                    template = *p;
                    break;
                }
            }
            CRYPTO_THREAD_unlock(store->lock);
        }
    } else {
        template.init = init_function;
    }

    /* provider_new() generates an error, so no need here */
    if ((prov = provider_new(name, template.init, template.parameters)) == NULL)
        return NULL;

    prov->libctx = libctx;
#ifndef FIPS_MODULE
    prov->error_lib = ERR_get_next_error_library();
#endif

    /*
     * At this point, the provider is only partially "loaded".  To be
     * fully "loaded", ossl_provider_activate() must also be called and it must
     * then be added to the provider store.
     */

    return prov;
}

/* Assumes that the store lock is held */
static int create_provider_children(OSSL_PROVIDER *prov)
{
    int ret = 1;
#ifndef FIPS_MODULE
    struct provider_store_st *store = prov->store;
    OSSL_PROVIDER_CHILD_CB *child_cb;
    int i, max;

    max = sk_OSSL_PROVIDER_CHILD_CB_num(store->child_cbs);
    for (i = 0; i < max; i++) {
        /*
         * This is newly activated (activatecnt == 1), so we need to
         * create child providers as necessary.
         */
        child_cb = sk_OSSL_PROVIDER_CHILD_CB_value(store->child_cbs, i);
        ret &= child_cb->create_cb((OSSL_CORE_HANDLE *)prov, child_cb->cbdata);
    }
#endif

    return ret;
}

int ossl_provider_add_to_store(OSSL_PROVIDER *prov, OSSL_PROVIDER **actualprov,
                               int retain_fallbacks)
{
    struct provider_store_st *store;
    int idx;
    OSSL_PROVIDER tmpl = { 0, };
    OSSL_PROVIDER *actualtmp = NULL;

    if (actualprov != NULL)
        *actualprov = NULL;

    if ((store = get_provider_store(prov->libctx)) == NULL)
        return 0;

    if (!CRYPTO_THREAD_write_lock(store->lock))
        return 0;

    tmpl.name = (char *)prov->name;
    idx = sk_OSSL_PROVIDER_find(store->providers, &tmpl);
    if (idx == -1)
        actualtmp = prov;
    else
        actualtmp = sk_OSSL_PROVIDER_value(store->providers, idx);

    if (idx == -1) {
        if (sk_OSSL_PROVIDER_push(store->providers, prov) == 0)
            goto err;
        prov->store = store;
        if (!create_provider_children(prov)) {
            sk_OSSL_PROVIDER_delete_ptr(store->providers, prov);
            goto err;
        }
        if (!retain_fallbacks)
            store->use_fallbacks = 0;
    }

    CRYPTO_THREAD_unlock(store->lock);

    if (actualprov != NULL) {
        if (!ossl_provider_up_ref(actualtmp)) {
            ERR_raise(ERR_LIB_CRYPTO, ERR_R_MALLOC_FAILURE);
            actualtmp = NULL;
            return 0;
        }
        *actualprov = actualtmp;
    }

    if (idx >= 0) {
        /*
         * The provider is already in the store. Probably two threads
         * independently initialised their own provider objects with the same
         * name and raced to put them in the store. This thread lost. We
         * deactivate the one we just created and use the one that already
         * exists instead.
         * If we get here then we know we did not create provider children
         * above, so we inform ossl_provider_deactivate not to attempt to remove
         * any.
         */
        ossl_provider_deactivate(prov, 0);
        ossl_provider_free(prov);
    }

    return 1;

 err:
    CRYPTO_THREAD_unlock(store->lock);
    return 0;
}

void ossl_provider_free(OSSL_PROVIDER *prov)
{
    if (prov != NULL) {
        int ref = 0;

        CRYPTO_DOWN_REF(&prov->refcnt, &ref, prov->refcnt_lock);

        /*
         * When the refcount drops to zero, we clean up the provider.
         * Note that this also does teardown, which may seem late,
         * considering that init happens on first activation.  However,
         * there may be other structures hanging on to the provider after
         * the last deactivation and may therefore need full access to the
         * provider's services.  Therefore, we deinit late.
         */
        if (ref == 0) {
            if (prov->flag_initialized) {
                ossl_provider_teardown(prov);
#ifndef OPENSSL_NO_ERR
# ifndef FIPS_MODULE
                if (prov->error_strings != NULL) {
                    ERR_unload_strings(prov->error_lib, prov->error_strings);
                    OPENSSL_free(prov->error_strings);
                    prov->error_strings = NULL;
                }
# endif
#endif
                OPENSSL_free(prov->operation_bits);
                prov->operation_bits = NULL;
                prov->operation_bits_sz = 0;
                prov->flag_initialized = 0;
            }

#ifndef FIPS_MODULE
            /*
             * We deregister thread handling whether or not the provider was
             * initialized. If init was attempted but was not successful then
             * the provider may still have registered a thread handler.
             */
            ossl_init_thread_deregister(prov);
            DSO_free(prov->module);
#endif
            OPENSSL_free(prov->name);
            OPENSSL_free(prov->path);
            sk_INFOPAIR_pop_free(prov->parameters, infopair_free);
            CRYPTO_THREAD_lock_free(prov->opbits_lock);
            CRYPTO_THREAD_lock_free(prov->flag_lock);
#ifndef HAVE_ATOMICS
            CRYPTO_THREAD_lock_free(prov->refcnt_lock);
#endif
            OPENSSL_free(prov);
        }
#ifndef FIPS_MODULE
        else if (prov->ischild) {
            ossl_provider_free_parent(prov, 0);
        }
#endif
    }
}

/* Setters */
int ossl_provider_set_module_path(OSSL_PROVIDER *prov, const char *module_path)
{
    OPENSSL_free(prov->path);
    prov->path = NULL;
    if (module_path == NULL)
        return 1;
    if ((prov->path = OPENSSL_strdup(module_path)) != NULL)
        return 1;
    ERR_raise(ERR_LIB_CRYPTO, ERR_R_MALLOC_FAILURE);
    return 0;
}

static int infopair_add(STACK_OF(INFOPAIR) **infopairsk, const char *name,
                        const char *value)
{
    INFOPAIR *pair = NULL;

    if ((pair = OPENSSL_zalloc(sizeof(*pair))) != NULL
        && (*infopairsk != NULL
            || (*infopairsk = sk_INFOPAIR_new_null()) != NULL)
        && (pair->name = OPENSSL_strdup(name)) != NULL
        && (pair->value = OPENSSL_strdup(value)) != NULL
        && sk_INFOPAIR_push(*infopairsk, pair) > 0)
        return 1;

    if (pair != NULL) {
        OPENSSL_free(pair->name);
        OPENSSL_free(pair->value);
        OPENSSL_free(pair);
    }
    ERR_raise(ERR_LIB_CRYPTO, ERR_R_MALLOC_FAILURE);
    return 0;
}

int ossl_provider_add_parameter(OSSL_PROVIDER *prov,
                                const char *name, const char *value)
{
    return infopair_add(&prov->parameters, name, value);
}

int ossl_provider_info_add_parameter(OSSL_PROVIDER_INFO *provinfo,
                                     const char *name,
                                     const char *value)
{
    return infopair_add(&provinfo->parameters, name, value);
}

/*
 * Provider activation.
 *
 * What "activation" means depends on the provider form; for built in
 * providers (in the library or the application alike), the provider
 * can already be considered to be loaded, all that's needed is to
 * initialize it.  However, for dynamically loadable provider modules,
 * we must first load that module.
 *
 * Built in modules are distinguished from dynamically loaded modules
 * with an already assigned init function.
 */
static const OSSL_DISPATCH *core_dispatch; /* Define further down */

int OSSL_PROVIDER_set_default_search_path(OSSL_LIB_CTX *libctx,
                                          const char *path)
{
    struct provider_store_st *store;
    char *p = NULL;

    if (path != NULL) {
        p = OPENSSL_strdup(path);
        if (p == NULL) {
            ERR_raise(ERR_LIB_CRYPTO, ERR_R_MALLOC_FAILURE);
            return 0;
        }
    }
    if ((store = get_provider_store(libctx)) != NULL
            && CRYPTO_THREAD_write_lock(store->default_path_lock)) {
        OPENSSL_free(store->default_path);
        store->default_path = p;
        CRYPTO_THREAD_unlock(store->default_path_lock);
        return 1;
    }
    OPENSSL_free(p);
    return 0;
}

/*
 * Internal version that doesn't affect the store flags, and thereby avoid
 * locking.  Direct callers must remember to set the store flags when
 * appropriate.
 */
static int provider_init(OSSL_PROVIDER *prov)
{
    const OSSL_DISPATCH *provider_dispatch = NULL;
    void *tmp_provctx = NULL;    /* safety measure */
#ifndef OPENSSL_NO_ERR
# ifndef FIPS_MODULE
    OSSL_FUNC_provider_get_reason_strings_fn *p_get_reason_strings = NULL;
# endif
#endif
    int ok = 0;

    if (!ossl_assert(!prov->flag_initialized)) {
        ERR_raise(ERR_LIB_CRYPTO, ERR_R_INTERNAL_ERROR);
        goto end;
    }

    /*
     * If the init function isn't set, it indicates that this provider is
     * a loadable module.
     */
    if (prov->init_function == NULL) {
#ifdef FIPS_MODULE
        goto end;
#else
        if (prov->module == NULL) {
            char *allocated_path = NULL;
            const char *module_path = NULL;
            char *merged_path = NULL;
            const char *load_dir = NULL;
            char *allocated_load_dir = NULL;
            struct provider_store_st *store;

            if ((prov->module = DSO_new()) == NULL) {
                /* DSO_new() generates an error already */
                goto end;
            }

            if ((store = get_provider_store(prov->libctx)) == NULL
                    || !CRYPTO_THREAD_read_lock(store->default_path_lock))
                goto end;

            if (store->default_path != NULL) {
                allocated_load_dir = OPENSSL_strdup(store->default_path);
                CRYPTO_THREAD_unlock(store->default_path_lock);
                if (allocated_load_dir == NULL) {
                    ERR_raise(ERR_LIB_CRYPTO, ERR_R_MALLOC_FAILURE);
                    goto end;
                }
                load_dir = allocated_load_dir;
            } else {
                CRYPTO_THREAD_unlock(store->default_path_lock);
            }

            if (load_dir == NULL) {
                load_dir = ossl_safe_getenv("OPENSSL_MODULES");
                if (load_dir == NULL)
                    load_dir = MODULESDIR;
            }

            DSO_ctrl(prov->module, DSO_CTRL_SET_FLAGS,
                     DSO_FLAG_NAME_TRANSLATION_EXT_ONLY, NULL);

            module_path = prov->path;
            if (module_path == NULL)
                module_path = allocated_path =
                    DSO_convert_filename(prov->module, prov->name);
            if (module_path != NULL)
                merged_path = DSO_merge(prov->module, module_path, load_dir);

            if (merged_path == NULL
                || (DSO_load(prov->module, merged_path, NULL, 0)) == NULL) {
                DSO_free(prov->module);
                prov->module = NULL;
            }

            OPENSSL_free(merged_path);
            OPENSSL_free(allocated_path);
            OPENSSL_free(allocated_load_dir);
        }

        if (prov->module == NULL) {
            /* DSO has already recorded errors, this is just a tracepoint */
            ERR_raise_data(ERR_LIB_CRYPTO, ERR_R_DSO_LIB,
                           "name=%s", prov->name);
            goto end;
        }

        prov->init_function = (OSSL_provider_init_fn *)
            DSO_bind_func(prov->module, "OSSL_provider_init");
#endif
    }

    /* Check for and call the initialise function for the provider. */
    if (prov->init_function == NULL) {
        ERR_raise_data(ERR_LIB_CRYPTO, ERR_R_UNSUPPORTED,
                       "name=%s, provider has no provider init function",
                       prov->name);
        goto end;
    }

    if (!prov->init_function((OSSL_CORE_HANDLE *)prov, core_dispatch,
                             &provider_dispatch, &tmp_provctx)) {
        ERR_raise_data(ERR_LIB_CRYPTO, ERR_R_INIT_FAIL,
                       "name=%s", prov->name);
        goto end;
    }
    prov->provctx = tmp_provctx;
    prov->dispatch = provider_dispatch;

    if (provider_dispatch != NULL) {
        for (; provider_dispatch->function_id != 0; provider_dispatch++) {
            switch (provider_dispatch->function_id) {
            case OSSL_FUNC_PROVIDER_TEARDOWN:
                prov->teardown =
                    OSSL_FUNC_provider_teardown(provider_dispatch);
                break;
            case OSSL_FUNC_PROVIDER_GETTABLE_PARAMS:
                prov->gettable_params =
                    OSSL_FUNC_provider_gettable_params(provider_dispatch);
                break;
            case OSSL_FUNC_PROVIDER_GET_PARAMS:
                prov->get_params =
                    OSSL_FUNC_provider_get_params(provider_dispatch);
                break;
            case OSSL_FUNC_PROVIDER_SELF_TEST:
                prov->self_test =
                    OSSL_FUNC_provider_self_test(provider_dispatch);
                break;
            case OSSL_FUNC_PROVIDER_GET_CAPABILITIES:
                prov->get_capabilities =
                    OSSL_FUNC_provider_get_capabilities(provider_dispatch);
                break;
            case OSSL_FUNC_PROVIDER_QUERY_OPERATION:
                prov->query_operation =
                    OSSL_FUNC_provider_query_operation(provider_dispatch);
                break;
            case OSSL_FUNC_PROVIDER_UNQUERY_OPERATION:
                prov->unquery_operation =
                    OSSL_FUNC_provider_unquery_operation(provider_dispatch);
                break;
#ifndef OPENSSL_NO_ERR
# ifndef FIPS_MODULE
            case OSSL_FUNC_PROVIDER_GET_REASON_STRINGS:
                p_get_reason_strings =
                    OSSL_FUNC_provider_get_reason_strings(provider_dispatch);
                break;
# endif
#endif
            }
        }
    }

#ifndef OPENSSL_NO_ERR
# ifndef FIPS_MODULE
    if (p_get_reason_strings != NULL) {
        const OSSL_ITEM *reasonstrings = p_get_reason_strings(prov->provctx);
        size_t cnt, cnt2;

        /*
         * ERR_load_strings() handles ERR_STRING_DATA rather than OSSL_ITEM,
         * although they are essentially the same type.
         * Furthermore, ERR_load_strings() patches the array's error number
         * with the error library number, so we need to make a copy of that
         * array either way.
         */
        cnt = 0;
        while (reasonstrings[cnt].id != 0) {
            if (ERR_GET_LIB(reasonstrings[cnt].id) != 0)
                goto end;
            cnt++;
        }
        cnt++;                   /* One for the terminating item */

        /* Allocate one extra item for the "library" name */
        prov->error_strings =
            OPENSSL_zalloc(sizeof(ERR_STRING_DATA) * (cnt + 1));
        if (prov->error_strings == NULL)
            goto end;

        /*
         * Set the "library" name.
         */
        prov->error_strings[0].error = ERR_PACK(prov->error_lib, 0, 0);
        prov->error_strings[0].string = prov->name;
        /*
         * Copy reasonstrings item 0..cnt-1 to prov->error_trings positions
         * 1..cnt.
         */
        for (cnt2 = 1; cnt2 <= cnt; cnt2++) {
            prov->error_strings[cnt2].error = (int)reasonstrings[cnt2-1].id;
            prov->error_strings[cnt2].string = reasonstrings[cnt2-1].ptr;
        }

        ERR_load_strings(prov->error_lib, prov->error_strings);
    }
# endif
#endif

    /* With this flag set, this provider has become fully "loaded". */
    prov->flag_initialized = 1;
    ok = 1;

 end:
    return ok;
}

/*
 * Deactivate a provider. If upcalls is 0 then we suppress any upcalls to a
 * parent provider. If removechildren is 0 then we suppress any calls to remove
 * child providers.
 * Return -1 on failure and the activation count on success
 */
static int provider_deactivate(OSSL_PROVIDER *prov, int upcalls,
                               int removechildren)
{
    int count;
    struct provider_store_st *store;
#ifndef FIPS_MODULE
    int freeparent = 0;
#endif
    int lock = 1;

    if (!ossl_assert(prov != NULL))
        return -1;

    /*
     * No need to lock if we've got no store because we've not been shared with
     * other threads.
     */
    store = get_provider_store(prov->libctx);
    if (store == NULL)
        lock = 0;

    if (lock && !CRYPTO_THREAD_read_lock(store->lock))
        return -1;
    if (lock && !CRYPTO_THREAD_write_lock(prov->flag_lock)) {
        CRYPTO_THREAD_unlock(store->lock);
        return -1;
    }

#ifndef FIPS_MODULE
    if (prov->activatecnt >= 2 && prov->ischild && upcalls) {
        /*
         * We have had a direct activation in this child libctx so we need to
         * now down the ref count in the parent provider. We do the actual down
         * ref outside of the flag_lock, since it could involve getting other
         * locks.
         */
        freeparent = 1;
    }
#endif

    if ((count = --prov->activatecnt) < 1)
        prov->flag_activated = 0;
#ifndef FIPS_MODULE
    else
        removechildren = 0;
#endif

#ifndef FIPS_MODULE
    if (removechildren && store != NULL) {
        int i, max = sk_OSSL_PROVIDER_CHILD_CB_num(store->child_cbs);
        OSSL_PROVIDER_CHILD_CB *child_cb;

        for (i = 0; i < max; i++) {
            child_cb = sk_OSSL_PROVIDER_CHILD_CB_value(store->child_cbs, i);
            child_cb->remove_cb((OSSL_CORE_HANDLE *)prov, child_cb->cbdata);
        }
    }
#endif
    if (lock) {
        CRYPTO_THREAD_unlock(prov->flag_lock);
        CRYPTO_THREAD_unlock(store->lock);
    }
#ifndef FIPS_MODULE
    if (freeparent)
        ossl_provider_free_parent(prov, 1);
#endif

    /* We don't deinit here, that's done in ossl_provider_free() */
    return count;
}

/*
 * Activate a provider.
 * Return -1 on failure and the activation count on success
 */
static int provider_activate(OSSL_PROVIDER *prov, int lock, int upcalls)
{
    int count = -1;
    struct provider_store_st *store;
    int ret = 1;

    store = prov->store;
    /*
    * If the provider hasn't been added to the store, then we don't need
    * any locks because we've not shared it with other threads.
    */
    if (store == NULL) {
        lock = 0;
        if (!provider_init(prov))
            return -1;
    }

#ifndef FIPS_MODULE
    if (prov->ischild && upcalls && !ossl_provider_up_ref_parent(prov, 1))
        return -1;
#endif

    if (lock && !CRYPTO_THREAD_read_lock(store->lock)) {
#ifndef FIPS_MODULE
        if (prov->ischild && upcalls)
            ossl_provider_free_parent(prov, 1);
#endif
        return -1;
    }

    if (lock && !CRYPTO_THREAD_write_lock(prov->flag_lock)) {
        CRYPTO_THREAD_unlock(store->lock);
#ifndef FIPS_MODULE
        if (prov->ischild && upcalls)
            ossl_provider_free_parent(prov, 1);
#endif
        return -1;
    }

    count = ++prov->activatecnt;
    prov->flag_activated = 1;

    if (prov->activatecnt == 1 && store != NULL) {
        ret = create_provider_children(prov);
    }
    if (lock) {
        CRYPTO_THREAD_unlock(prov->flag_lock);
        CRYPTO_THREAD_unlock(store->lock);
    }

    if (!ret)
        return -1;

    return count;
}

static int provider_flush_store_cache(const OSSL_PROVIDER *prov)
{
    struct provider_store_st *store;
    int freeing;

    if ((store = get_provider_store(prov->libctx)) == NULL)
        return 0;

    if (!CRYPTO_THREAD_read_lock(store->lock))
        return 0;
    freeing = store->freeing;
    CRYPTO_THREAD_unlock(store->lock);

    if (!freeing) {
        int acc
            = evp_method_store_cache_flush(prov->libctx)
#ifndef FIPS_MODULE
            + ossl_encoder_store_cache_flush(prov->libctx)
            + ossl_decoder_store_cache_flush(prov->libctx)
            + ossl_store_loader_store_cache_flush(prov->libctx)
#endif
            ;

#ifndef FIPS_MODULE
        return acc == 4;
#else
        return acc == 1;
#endif
    }
    return 1;
}

static int provider_remove_store_methods(OSSL_PROVIDER *prov)
{
    struct provider_store_st *store;
    int freeing;

    if ((store = get_provider_store(prov->libctx)) == NULL)
        return 0;

    if (!CRYPTO_THREAD_read_lock(store->lock))
        return 0;
    freeing = store->freeing;
    CRYPTO_THREAD_unlock(store->lock);

    if (!freeing) {
        int acc;

        if (!CRYPTO_THREAD_write_lock(prov->opbits_lock))
            return 0;
        OPENSSL_free(prov->operation_bits);
        prov->operation_bits = NULL;
        prov->operation_bits_sz = 0;
        CRYPTO_THREAD_unlock(prov->opbits_lock);

        acc = evp_method_store_remove_all_provided(prov)
#ifndef FIPS_MODULE
            + ossl_encoder_store_remove_all_provided(prov)
            + ossl_decoder_store_remove_all_provided(prov)
            + ossl_store_loader_store_remove_all_provided(prov)
#endif
            ;

#ifndef FIPS_MODULE
        return acc == 4;
#else
        return acc == 1;
#endif
    }
    return 1;
}

int ossl_provider_activate(OSSL_PROVIDER *prov, int upcalls, int aschild)
{
    int count;

    if (prov == NULL)
        return 0;
#ifndef FIPS_MODULE
    /*
     * If aschild is true, then we only actually do the activation if the
     * provider is a child. If its not, this is still success.
     */
    if (aschild && !prov->ischild)
        return 1;
#endif
    if ((count = provider_activate(prov, 1, upcalls)) > 0)
        return count == 1 ? provider_flush_store_cache(prov) : 1;

    return 0;
}

int ossl_provider_deactivate(OSSL_PROVIDER *prov, int removechildren)
{
    int count;

    if (prov == NULL
            || (count = provider_deactivate(prov, 1, removechildren)) < 0)
        return 0;
    return count == 0 ? provider_remove_store_methods(prov) : 1;
}

void *ossl_provider_ctx(const OSSL_PROVIDER *prov)
{
    return prov != NULL ? prov->provctx : NULL;
}

/*
 * This function only does something once when store->use_fallbacks == 1,
 * and then sets store->use_fallbacks = 0, so the second call and so on is
 * effectively a no-op.
 */
static int provider_activate_fallbacks(struct provider_store_st *store)
{
    int use_fallbacks;
    int activated_fallback_count = 0;
    int ret = 0;
    const OSSL_PROVIDER_INFO *p;

    if (!CRYPTO_THREAD_read_lock(store->lock))
        return 0;
    use_fallbacks = store->use_fallbacks;
    CRYPTO_THREAD_unlock(store->lock);
    if (!use_fallbacks)
        return 1;

    if (!CRYPTO_THREAD_write_lock(store->lock))
        return 0;
    /* Check again, just in case another thread changed it */
    use_fallbacks = store->use_fallbacks;
    if (!use_fallbacks) {
        CRYPTO_THREAD_unlock(store->lock);
        return 1;
    }

    for (p = ossl_predefined_providers; p->name != NULL; p++) {
        OSSL_PROVIDER *prov = NULL;

        if (!p->is_fallback)
            continue;
        /*
         * We use the internal constructor directly here,
         * otherwise we get a call loop
         */
        prov = provider_new(p->name, p->init, NULL);
        if (prov == NULL)
            goto err;
        prov->libctx = store->libctx;
#ifndef FIPS_MODULE
        prov->error_lib = ERR_get_next_error_library();
#endif

        /*
         * We are calling provider_activate while holding the store lock. This
         * means the init function will be called while holding a lock. Normally
         * we try to avoid calling a user callback while holding a lock.
         * However, fallbacks are never third party providers so we accept this.
         */
        if (provider_activate(prov, 0, 0) < 0) {
            ossl_provider_free(prov);
            goto err;
        }
        prov->store = store;
        if (sk_OSSL_PROVIDER_push(store->providers, prov) == 0) {
            ossl_provider_free(prov);
            goto err;
        }
        activated_fallback_count++;
    }

    if (activated_fallback_count > 0) {
        store->use_fallbacks = 0;
        ret = 1;
    }
 err:
    CRYPTO_THREAD_unlock(store->lock);
    return ret;
}

int ossl_provider_doall_activated(OSSL_LIB_CTX *ctx,
                                  int (*cb)(OSSL_PROVIDER *provider,
                                            void *cbdata),
                                  void *cbdata)
{
    int ret = 0, curr, max, ref = 0;
    struct provider_store_st *store = get_provider_store(ctx);
    STACK_OF(OSSL_PROVIDER) *provs = NULL;

#if !defined(FIPS_MODULE) && !defined(OPENSSL_NO_AUTOLOAD_CONFIG)
    /*
     * Make sure any providers are loaded from config before we try to use
     * them.
     */
    if (ossl_lib_ctx_is_default(ctx))
        OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CONFIG, NULL);
#endif

    if (store == NULL)
        return 1;
    if (!provider_activate_fallbacks(store))
        return 0;

    /*
     * Under lock, grab a copy of the provider list and up_ref each
     * provider so that they don't disappear underneath us.
     */
    if (!CRYPTO_THREAD_read_lock(store->lock))
        return 0;
    provs = sk_OSSL_PROVIDER_dup(store->providers);
    if (provs == NULL) {
        CRYPTO_THREAD_unlock(store->lock);
        return 0;
    }
    max = sk_OSSL_PROVIDER_num(provs);
    /*
     * We work backwards through the stack so that we can safely delete items
     * as we go.
     */
    for (curr = max - 1; curr >= 0; curr--) {
        OSSL_PROVIDER *prov = sk_OSSL_PROVIDER_value(provs, curr);

        if (!CRYPTO_THREAD_write_lock(prov->flag_lock))
            goto err_unlock;
        if (prov->flag_activated) {
            /*
             * We call CRYPTO_UP_REF directly rather than ossl_provider_up_ref
             * to avoid upping the ref count on the parent provider, which we
             * must not do while holding locks.
             */
            if (CRYPTO_UP_REF(&prov->refcnt, &ref, prov->refcnt_lock) <= 0) {
                CRYPTO_THREAD_unlock(prov->flag_lock);
                goto err_unlock;
            }
            /*
             * It's already activated, but we up the activated count to ensure
             * it remains activated until after we've called the user callback.
             * We do this with no locking (because we already hold the locks)
             * and no upcalls (which must not be called when locks are held). In
             * theory this could mean the parent provider goes inactive, whilst
             * still activated in the child for a short period. That's ok.
             */
            if (provider_activate(prov, 0, 0) < 0) {
                CRYPTO_DOWN_REF(&prov->refcnt, &ref, prov->refcnt_lock);
                CRYPTO_THREAD_unlock(prov->flag_lock);
                goto err_unlock;
            }
        } else {
            sk_OSSL_PROVIDER_delete(provs, curr);
            max--;
        }
        CRYPTO_THREAD_unlock(prov->flag_lock);
    }
    CRYPTO_THREAD_unlock(store->lock);

    /*
     * Now, we sweep through all providers not under lock
     */
    for (curr = 0; curr < max; curr++) {
        OSSL_PROVIDER *prov = sk_OSSL_PROVIDER_value(provs, curr);

        if (!cb(prov, cbdata)) {
            curr = -1;
            goto finish;
        }
    }
    curr = -1;

    ret = 1;
    goto finish;

 err_unlock:
    CRYPTO_THREAD_unlock(store->lock);
 finish:
    /*
     * The pop_free call doesn't do what we want on an error condition. We
     * either start from the first item in the stack, or part way through if
     * we only processed some of the items.
     */
    for (curr++; curr < max; curr++) {
        OSSL_PROVIDER *prov = sk_OSSL_PROVIDER_value(provs, curr);

        provider_deactivate(prov, 0, 1);
        /*
         * As above where we did the up-ref, we don't call ossl_provider_free
         * to avoid making upcalls. There should always be at least one ref
         * to the provider in the store, so this should never drop to 0.
         */
        CRYPTO_DOWN_REF(&prov->refcnt, &ref, prov->refcnt_lock);
        /*
         * Not much we can do if this assert ever fails. So we don't use
         * ossl_assert here.
         */
        assert(ref > 0);
    }
    sk_OSSL_PROVIDER_free(provs);
    return ret;
}

int OSSL_PROVIDER_available(OSSL_LIB_CTX *libctx, const char *name)
{
    OSSL_PROVIDER *prov = NULL;
    int available = 0;
    struct provider_store_st *store = get_provider_store(libctx);

    if (store == NULL || !provider_activate_fallbacks(store))
        return 0;

    prov = ossl_provider_find(libctx, name, 0);
    if (prov != NULL) {
        if (!CRYPTO_THREAD_read_lock(prov->flag_lock))
            return 0;
        available = prov->flag_activated;
        CRYPTO_THREAD_unlock(prov->flag_lock);
        ossl_provider_free(prov);
    }
    return available;
}

/* Setters of Provider Object data */
int ossl_provider_set_fallback(OSSL_PROVIDER *prov)
{
    if (prov == NULL)
        return 0;

    prov->flag_fallback = 1;
    return 1;
}

/* Getters of Provider Object data */
const char *ossl_provider_name(const OSSL_PROVIDER *prov)
{
    return prov->name;
}

const DSO *ossl_provider_dso(const OSSL_PROVIDER *prov)
{
    return prov->module;
}

const char *ossl_provider_module_name(const OSSL_PROVIDER *prov)
{
#ifdef FIPS_MODULE
    return NULL;
#else
    return DSO_get_filename(prov->module);
#endif
}

const char *ossl_provider_module_path(const OSSL_PROVIDER *prov)
{
#ifdef FIPS_MODULE
    return NULL;
#else
    /* FIXME: Ensure it's a full path */
    return DSO_get_filename(prov->module);
#endif
}

void *ossl_provider_prov_ctx(const OSSL_PROVIDER *prov)
{
    if (prov != NULL)
        return prov->provctx;

    return NULL;
}

const OSSL_DISPATCH *ossl_provider_get0_dispatch(const OSSL_PROVIDER *prov)
{
    if (prov != NULL)
        return prov->dispatch;

    return NULL;
}

OSSL_LIB_CTX *ossl_provider_libctx(const OSSL_PROVIDER *prov)
{
    return prov != NULL ? prov->libctx : NULL;
}

/* Wrappers around calls to the provider */
void ossl_provider_teardown(const OSSL_PROVIDER *prov)
{
    if (prov->teardown != NULL
#ifndef FIPS_MODULE
            && !prov->ischild
#endif
       )
        prov->teardown(prov->provctx);
}

const OSSL_PARAM *ossl_provider_gettable_params(const OSSL_PROVIDER *prov)
{
    return prov->gettable_params == NULL
        ? NULL : prov->gettable_params(prov->provctx);
}

int ossl_provider_get_params(const OSSL_PROVIDER *prov, OSSL_PARAM params[])
{
    return prov->get_params == NULL
        ? 0 : prov->get_params(prov->provctx, params);
}

int ossl_provider_self_test(const OSSL_PROVIDER *prov)
{
    int ret;

    if (prov->self_test == NULL)
        return 1;
    ret = prov->self_test(prov->provctx);
    if (ret == 0)
        (void)provider_remove_store_methods((OSSL_PROVIDER *)prov);
    return ret;
}

int ossl_provider_get_capabilities(const OSSL_PROVIDER *prov,
                                   const char *capability,
                                   OSSL_CALLBACK *cb,
                                   void *arg)
{
    return prov->get_capabilities == NULL
        ? 1 : prov->get_capabilities(prov->provctx, capability, cb, arg);
}

const OSSL_ALGORITHM *ossl_provider_query_operation(const OSSL_PROVIDER *prov,
                                                    int operation_id,
                                                    int *no_cache)
{
    const OSSL_ALGORITHM *res;

    if (prov->query_operation == NULL)
        return NULL;
    res = prov->query_operation(prov->provctx, operation_id, no_cache);
#if defined(OPENSSL_NO_CACHED_FETCH)
    /* Forcing the non-caching of queries */
    if (no_cache != NULL)
        *no_cache = 1;
#endif
    return res;
}

void ossl_provider_unquery_operation(const OSSL_PROVIDER *prov,
                                     int operation_id,
                                     const OSSL_ALGORITHM *algs)
{
    if (prov->unquery_operation != NULL)
        prov->unquery_operation(prov->provctx, operation_id, algs);
}

int ossl_provider_set_operation_bit(OSSL_PROVIDER *provider, size_t bitnum)
{
    size_t byte = bitnum / 8;
    unsigned char bit = (1 << (bitnum % 8)) & 0xFF;

    if (!CRYPTO_THREAD_write_lock(provider->opbits_lock))
        return 0;
    if (provider->operation_bits_sz <= byte) {
        unsigned char *tmp = OPENSSL_realloc(provider->operation_bits,
                                             byte + 1);

        if (tmp == NULL) {
            CRYPTO_THREAD_unlock(provider->opbits_lock);
            ERR_raise(ERR_LIB_CRYPTO, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        provider->operation_bits = tmp;
        memset(provider->operation_bits + provider->operation_bits_sz,
               '\0', byte + 1 - provider->operation_bits_sz);
        provider->operation_bits_sz = byte + 1;
    }
    provider->operation_bits[byte] |= bit;
    CRYPTO_THREAD_unlock(provider->opbits_lock);
    return 1;
}

int ossl_provider_test_operation_bit(OSSL_PROVIDER *provider, size_t bitnum,
                                     int *result)
{
    size_t byte = bitnum / 8;
    unsigned char bit = (1 << (bitnum % 8)) & 0xFF;

    if (!ossl_assert(result != NULL)) {
        ERR_raise(ERR_LIB_CRYPTO, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }

    *result = 0;
    if (!CRYPTO_THREAD_read_lock(provider->opbits_lock))
        return 0;
    if (provider->operation_bits_sz > byte)
        *result = ((provider->operation_bits[byte] & bit) != 0);
    CRYPTO_THREAD_unlock(provider->opbits_lock);
    return 1;
}

#ifndef FIPS_MODULE
const OSSL_CORE_HANDLE *ossl_provider_get_parent(OSSL_PROVIDER *prov)
{
    return prov->handle;
}

int ossl_provider_is_child(const OSSL_PROVIDER *prov)
{
    return prov->ischild;
}

int ossl_provider_set_child(OSSL_PROVIDER *prov, const OSSL_CORE_HANDLE *handle)
{
    prov->handle = handle;
    prov->ischild = 1;

    return 1;
}

int ossl_provider_default_props_update(OSSL_LIB_CTX *libctx, const char *props)
{
#ifndef FIPS_MODULE
    struct provider_store_st *store = NULL;
    int i, max;
    OSSL_PROVIDER_CHILD_CB *child_cb;

    if ((store = get_provider_store(libctx)) == NULL)
        return 0;

    if (!CRYPTO_THREAD_read_lock(store->lock))
        return 0;

    max = sk_OSSL_PROVIDER_CHILD_CB_num(store->child_cbs);
    for (i = 0; i < max; i++) {
        child_cb = sk_OSSL_PROVIDER_CHILD_CB_value(store->child_cbs, i);
        child_cb->global_props_cb(props, child_cb->cbdata);
    }

    CRYPTO_THREAD_unlock(store->lock);
#endif
    return 1;
}

static int ossl_provider_register_child_cb(const OSSL_CORE_HANDLE *handle,
                                           int (*create_cb)(
                                               const OSSL_CORE_HANDLE *provider,
                                               void *cbdata),
                                           int (*remove_cb)(
                                               const OSSL_CORE_HANDLE *provider,
                                               void *cbdata),
                                           int (*global_props_cb)(
                                               const char *props,
                                               void *cbdata),
                                           void *cbdata)
{
    /*
     * This is really an OSSL_PROVIDER that we created and cast to
     * OSSL_CORE_HANDLE originally. Therefore it is safe to cast it back.
     */
    OSSL_PROVIDER *thisprov = (OSSL_PROVIDER *)handle;
    OSSL_PROVIDER *prov;
    OSSL_LIB_CTX *libctx = thisprov->libctx;
    struct provider_store_st *store = NULL;
    int ret = 0, i, max;
    OSSL_PROVIDER_CHILD_CB *child_cb;
    char *propsstr = NULL;

    if ((store = get_provider_store(libctx)) == NULL)
        return 0;

    child_cb = OPENSSL_malloc(sizeof(*child_cb));
    if (child_cb == NULL)
        return 0;
    child_cb->prov = thisprov;
    child_cb->create_cb = create_cb;
    child_cb->remove_cb = remove_cb;
    child_cb->global_props_cb = global_props_cb;
    child_cb->cbdata = cbdata;

    if (!CRYPTO_THREAD_write_lock(store->lock)) {
        OPENSSL_free(child_cb);
        return 0;
    }
    propsstr = evp_get_global_properties_str(libctx, 0);

    if (propsstr != NULL) {
        global_props_cb(propsstr, cbdata);
        OPENSSL_free(propsstr);
    }
    max = sk_OSSL_PROVIDER_num(store->providers);
    for (i = 0; i < max; i++) {
        int activated;

        prov = sk_OSSL_PROVIDER_value(store->providers, i);

        if (!CRYPTO_THREAD_read_lock(prov->flag_lock))
            break;
        activated = prov->flag_activated;
        CRYPTO_THREAD_unlock(prov->flag_lock);
        /*
         * We hold the store lock while calling the user callback. This means
         * that the user callback must be short and simple and not do anything
         * likely to cause a deadlock. We don't hold the flag_lock during this
         * call. In theory this means that another thread could deactivate it
         * while we are calling create. This is ok because the other thread
         * will also call remove_cb, but won't be able to do so until we release
         * the store lock.
         */
        if (activated && !create_cb((OSSL_CORE_HANDLE *)prov, cbdata))
            break;
    }
    if (i == max) {
        /* Success */
        ret = sk_OSSL_PROVIDER_CHILD_CB_push(store->child_cbs, child_cb);
    }
    if (i != max || ret <= 0) {
        /* Failed during creation. Remove everything we just added */
        for (; i >= 0; i--) {
            prov = sk_OSSL_PROVIDER_value(store->providers, i);
            remove_cb((OSSL_CORE_HANDLE *)prov, cbdata);
        }
        OPENSSL_free(child_cb);
        ret = 0;
    }
    CRYPTO_THREAD_unlock(store->lock);

    return ret;
}

static void ossl_provider_deregister_child_cb(const OSSL_CORE_HANDLE *handle)
{
    /*
     * This is really an OSSL_PROVIDER that we created and cast to
     * OSSL_CORE_HANDLE originally. Therefore it is safe to cast it back.
     */
    OSSL_PROVIDER *thisprov = (OSSL_PROVIDER *)handle;
    OSSL_LIB_CTX *libctx = thisprov->libctx;
    struct provider_store_st *store = NULL;
    int i, max;
    OSSL_PROVIDER_CHILD_CB *child_cb;

    if ((store = get_provider_store(libctx)) == NULL)
        return;

    if (!CRYPTO_THREAD_write_lock(store->lock))
        return;
    max = sk_OSSL_PROVIDER_CHILD_CB_num(store->child_cbs);
    for (i = 0; i < max; i++) {
        child_cb = sk_OSSL_PROVIDER_CHILD_CB_value(store->child_cbs, i);
        if (child_cb->prov == thisprov) {
            /* Found an entry */
            sk_OSSL_PROVIDER_CHILD_CB_delete(store->child_cbs, i);
            OPENSSL_free(child_cb);
            break;
        }
    }
    CRYPTO_THREAD_unlock(store->lock);
}
#endif

/*-
 * Core functions for the provider
 * ===============================
 *
 * This is the set of functions that the core makes available to the provider
 */

/*
 * This returns a list of Provider Object parameters with their types, for
 * discovery.  We do not expect that many providers will use this, but one
 * never knows.
 */
static const OSSL_PARAM param_types[] = {
    OSSL_PARAM_DEFN(OSSL_PROV_PARAM_CORE_VERSION, OSSL_PARAM_UTF8_PTR, NULL, 0),
    OSSL_PARAM_DEFN(OSSL_PROV_PARAM_CORE_PROV_NAME, OSSL_PARAM_UTF8_PTR,
                    NULL, 0),
#ifndef FIPS_MODULE
    OSSL_PARAM_DEFN(OSSL_PROV_PARAM_CORE_MODULE_FILENAME, OSSL_PARAM_UTF8_PTR,
                    NULL, 0),
#endif
    OSSL_PARAM_END
};

/*
 * Forward declare all the functions that are provided aa dispatch.
 * This ensures that the compiler will complain if they aren't defined
 * with the correct signature.
 */
static OSSL_FUNC_core_gettable_params_fn core_gettable_params;
static OSSL_FUNC_core_get_params_fn core_get_params;
static OSSL_FUNC_core_get_libctx_fn core_get_libctx;
static OSSL_FUNC_core_thread_start_fn core_thread_start;
#ifndef FIPS_MODULE
static OSSL_FUNC_core_new_error_fn core_new_error;
static OSSL_FUNC_core_set_error_debug_fn core_set_error_debug;
static OSSL_FUNC_core_vset_error_fn core_vset_error;
static OSSL_FUNC_core_set_error_mark_fn core_set_error_mark;
static OSSL_FUNC_core_clear_last_error_mark_fn core_clear_last_error_mark;
static OSSL_FUNC_core_pop_error_to_mark_fn core_pop_error_to_mark;
OSSL_FUNC_BIO_new_file_fn ossl_core_bio_new_file;
OSSL_FUNC_BIO_new_membuf_fn ossl_core_bio_new_mem_buf;
OSSL_FUNC_BIO_read_ex_fn ossl_core_bio_read_ex;
OSSL_FUNC_BIO_write_ex_fn ossl_core_bio_write_ex;
OSSL_FUNC_BIO_gets_fn ossl_core_bio_gets;
OSSL_FUNC_BIO_puts_fn ossl_core_bio_puts;
OSSL_FUNC_BIO_up_ref_fn ossl_core_bio_up_ref;
OSSL_FUNC_BIO_free_fn ossl_core_bio_free;
OSSL_FUNC_BIO_vprintf_fn ossl_core_bio_vprintf;
OSSL_FUNC_BIO_vsnprintf_fn BIO_vsnprintf;
static OSSL_FUNC_self_test_cb_fn core_self_test_get_callback;
OSSL_FUNC_get_entropy_fn ossl_rand_get_entropy;
OSSL_FUNC_cleanup_entropy_fn ossl_rand_cleanup_entropy;
OSSL_FUNC_get_nonce_fn ossl_rand_get_nonce;
OSSL_FUNC_cleanup_nonce_fn ossl_rand_cleanup_nonce;
#endif
OSSL_FUNC_CRYPTO_malloc_fn CRYPTO_malloc;
OSSL_FUNC_CRYPTO_zalloc_fn CRYPTO_zalloc;
OSSL_FUNC_CRYPTO_free_fn CRYPTO_free;
OSSL_FUNC_CRYPTO_clear_free_fn CRYPTO_clear_free;
OSSL_FUNC_CRYPTO_realloc_fn CRYPTO_realloc;
OSSL_FUNC_CRYPTO_clear_realloc_fn CRYPTO_clear_realloc;
OSSL_FUNC_CRYPTO_secure_malloc_fn CRYPTO_secure_malloc;
OSSL_FUNC_CRYPTO_secure_zalloc_fn CRYPTO_secure_zalloc;
OSSL_FUNC_CRYPTO_secure_free_fn CRYPTO_secure_free;
OSSL_FUNC_CRYPTO_secure_clear_free_fn CRYPTO_secure_clear_free;
OSSL_FUNC_CRYPTO_secure_allocated_fn CRYPTO_secure_allocated;
OSSL_FUNC_OPENSSL_cleanse_fn OPENSSL_cleanse;
#ifndef FIPS_MODULE
OSSL_FUNC_provider_register_child_cb_fn ossl_provider_register_child_cb;
OSSL_FUNC_provider_deregister_child_cb_fn ossl_provider_deregister_child_cb;
static OSSL_FUNC_provider_name_fn core_provider_get0_name;
static OSSL_FUNC_provider_get0_provider_ctx_fn core_provider_get0_provider_ctx;
static OSSL_FUNC_provider_get0_dispatch_fn core_provider_get0_dispatch;
static OSSL_FUNC_provider_up_ref_fn core_provider_up_ref_intern;
static OSSL_FUNC_provider_free_fn core_provider_free_intern;
static OSSL_FUNC_core_obj_add_sigid_fn core_obj_add_sigid;
static OSSL_FUNC_core_obj_create_fn core_obj_create;
#endif

static const OSSL_PARAM *core_gettable_params(const OSSL_CORE_HANDLE *handle)
{
    return param_types;
}

static int core_get_params(const OSSL_CORE_HANDLE *handle, OSSL_PARAM params[])
{
    int i;
    OSSL_PARAM *p;
    /*
     * We created this object originally and we know it is actually an
     * OSSL_PROVIDER *, so the cast is safe
     */
    OSSL_PROVIDER *prov = (OSSL_PROVIDER *)handle;

    if ((p = OSSL_PARAM_locate(params, OSSL_PROV_PARAM_CORE_VERSION)) != NULL)
        OSSL_PARAM_set_utf8_ptr(p, OPENSSL_VERSION_STR);
    if ((p = OSSL_PARAM_locate(params, OSSL_PROV_PARAM_CORE_PROV_NAME)) != NULL)
        OSSL_PARAM_set_utf8_ptr(p, prov->name);

#ifndef FIPS_MODULE
    if ((p = OSSL_PARAM_locate(params,
                               OSSL_PROV_PARAM_CORE_MODULE_FILENAME)) != NULL)
        OSSL_PARAM_set_utf8_ptr(p, ossl_provider_module_path(prov));
#endif

    if (prov->parameters == NULL)
        return 1;

    for (i = 0; i < sk_INFOPAIR_num(prov->parameters); i++) {
        INFOPAIR *pair = sk_INFOPAIR_value(prov->parameters, i);

        if ((p = OSSL_PARAM_locate(params, pair->name)) != NULL)
            OSSL_PARAM_set_utf8_ptr(p, pair->value);
    }
    return 1;
}

static OPENSSL_CORE_CTX *core_get_libctx(const OSSL_CORE_HANDLE *handle)
{
    /*
     * We created this object originally and we know it is actually an
     * OSSL_PROVIDER *, so the cast is safe
     */
    OSSL_PROVIDER *prov = (OSSL_PROVIDER *)handle;

    /*
     * Using ossl_provider_libctx would be wrong as that returns
     * NULL for |prov| == NULL and NULL libctx has a special meaning
     * that does not apply here. Here |prov| == NULL can happen only in
     * case of a coding error.
     */
    assert(prov != NULL);
    return (OPENSSL_CORE_CTX *)prov->libctx;
}

static int core_thread_start(const OSSL_CORE_HANDLE *handle,
                             OSSL_thread_stop_handler_fn handfn,
                             void *arg)
{
    /*
     * We created this object originally and we know it is actually an
     * OSSL_PROVIDER *, so the cast is safe
     */
    OSSL_PROVIDER *prov = (OSSL_PROVIDER *)handle;

    return ossl_init_thread_start(prov, arg, handfn);
}

/*
 * The FIPS module inner provider doesn't implement these.  They aren't
 * needed there, since the FIPS module upcalls are always the outer provider
 * ones.
 */
#ifndef FIPS_MODULE
/*
 * These error functions should use |handle| to select the proper
 * library context to report in the correct error stack if error
 * stacks become tied to the library context.
 * We cannot currently do that since there's no support for it in the
 * ERR subsystem.
 */
static void core_new_error(const OSSL_CORE_HANDLE *handle)
{
    ERR_new();
}

static void core_set_error_debug(const OSSL_CORE_HANDLE *handle,
                                 const char *file, int line, const char *func)
{
    ERR_set_debug(file, line, func);
}

static void core_vset_error(const OSSL_CORE_HANDLE *handle,
                            uint32_t reason, const char *fmt, va_list args)
{
    /*
     * We created this object originally and we know it is actually an
     * OSSL_PROVIDER *, so the cast is safe
     */
    OSSL_PROVIDER *prov = (OSSL_PROVIDER *)handle;

    /*
     * If the uppermost 8 bits are non-zero, it's an OpenSSL library
     * error and will be treated as such.  Otherwise, it's a new style
     * provider error and will be treated as such.
     */
    if (ERR_GET_LIB(reason) != 0) {
        ERR_vset_error(ERR_GET_LIB(reason), ERR_GET_REASON(reason), fmt, args);
    } else {
        ERR_vset_error(prov->error_lib, (int)reason, fmt, args);
    }
}

static int core_set_error_mark(const OSSL_CORE_HANDLE *handle)
{
    return ERR_set_mark();
}

static int core_clear_last_error_mark(const OSSL_CORE_HANDLE *handle)
{
    return ERR_clear_last_mark();
}

static int core_pop_error_to_mark(const OSSL_CORE_HANDLE *handle)
{
    return ERR_pop_to_mark();
}

static void core_self_test_get_callback(OPENSSL_CORE_CTX *libctx,
                                        OSSL_CALLBACK **cb, void **cbarg)
{
    OSSL_SELF_TEST_get_callback((OSSL_LIB_CTX *)libctx, cb, cbarg);
}

static const char *core_provider_get0_name(const OSSL_CORE_HANDLE *prov)
{
    return OSSL_PROVIDER_get0_name((const OSSL_PROVIDER *)prov);
}

static void *core_provider_get0_provider_ctx(const OSSL_CORE_HANDLE *prov)
{
    return OSSL_PROVIDER_get0_provider_ctx((const OSSL_PROVIDER *)prov);
}

static const OSSL_DISPATCH *
core_provider_get0_dispatch(const OSSL_CORE_HANDLE *prov)
{
    return OSSL_PROVIDER_get0_dispatch((const OSSL_PROVIDER *)prov);
}

static int core_provider_up_ref_intern(const OSSL_CORE_HANDLE *prov,
                                       int activate)
{
    return provider_up_ref_intern((OSSL_PROVIDER *)prov, activate);
}

static int core_provider_free_intern(const OSSL_CORE_HANDLE *prov,
                                     int deactivate)
{
    return provider_free_intern((OSSL_PROVIDER *)prov, deactivate);
}

static int core_obj_add_sigid(const OSSL_CORE_HANDLE *prov,
                              const char *sign_name, const char *digest_name,
                              const char *pkey_name)
{
    int sign_nid = OBJ_txt2nid(sign_name);
    int digest_nid = NID_undef;
    int pkey_nid = OBJ_txt2nid(pkey_name);

    if (digest_name != NULL && digest_name[0] != '\0'
        && (digest_nid = OBJ_txt2nid(digest_name)) == NID_undef)
            return 0;

    if (sign_nid == NID_undef)
        return 0;

    /*
     * Check if it already exists. This is a success if so (even if we don't
     * have nids for the digest/pkey)
     */
    if (OBJ_find_sigid_algs(sign_nid, NULL, NULL))
        return 1;

    if (pkey_nid == NID_undef)
        return 0;

    return OBJ_add_sigid(sign_nid, digest_nid, pkey_nid);
}

static int core_obj_create(const OSSL_CORE_HANDLE *prov, const char *oid,
                           const char *sn, const char *ln)
{
    /* Check if it already exists and create it if not */
    return OBJ_txt2nid(oid) != NID_undef
           || OBJ_create(oid, sn, ln) != NID_undef;
}
#endif /* FIPS_MODULE */

/*
 * Functions provided by the core.
 */
static const OSSL_DISPATCH core_dispatch_[] = {
    { OSSL_FUNC_CORE_GETTABLE_PARAMS, (void (*)(void))core_gettable_params },
    { OSSL_FUNC_CORE_GET_PARAMS, (void (*)(void))core_get_params },
    { OSSL_FUNC_CORE_GET_LIBCTX, (void (*)(void))core_get_libctx },
    { OSSL_FUNC_CORE_THREAD_START, (void (*)(void))core_thread_start },
#ifndef FIPS_MODULE
    { OSSL_FUNC_CORE_NEW_ERROR, (void (*)(void))core_new_error },
    { OSSL_FUNC_CORE_SET_ERROR_DEBUG, (void (*)(void))core_set_error_debug },
    { OSSL_FUNC_CORE_VSET_ERROR, (void (*)(void))core_vset_error },
    { OSSL_FUNC_CORE_SET_ERROR_MARK, (void (*)(void))core_set_error_mark },
    { OSSL_FUNC_CORE_CLEAR_LAST_ERROR_MARK,
      (void (*)(void))core_clear_last_error_mark },
    { OSSL_FUNC_CORE_POP_ERROR_TO_MARK, (void (*)(void))core_pop_error_to_mark },
    { OSSL_FUNC_BIO_NEW_FILE, (void (*)(void))ossl_core_bio_new_file },
    { OSSL_FUNC_BIO_NEW_MEMBUF, (void (*)(void))ossl_core_bio_new_mem_buf },
    { OSSL_FUNC_BIO_READ_EX, (void (*)(void))ossl_core_bio_read_ex },
    { OSSL_FUNC_BIO_WRITE_EX, (void (*)(void))ossl_core_bio_write_ex },
    { OSSL_FUNC_BIO_GETS, (void (*)(void))ossl_core_bio_gets },
    { OSSL_FUNC_BIO_PUTS, (void (*)(void))ossl_core_bio_puts },
    { OSSL_FUNC_BIO_CTRL, (void (*)(void))ossl_core_bio_ctrl },
    { OSSL_FUNC_BIO_UP_REF, (void (*)(void))ossl_core_bio_up_ref },
    { OSSL_FUNC_BIO_FREE, (void (*)(void))ossl_core_bio_free },
    { OSSL_FUNC_BIO_VPRINTF, (void (*)(void))ossl_core_bio_vprintf },
    { OSSL_FUNC_BIO_VSNPRINTF, (void (*)(void))BIO_vsnprintf },
    { OSSL_FUNC_SELF_TEST_CB, (void (*)(void))core_self_test_get_callback },
    { OSSL_FUNC_GET_ENTROPY, (void (*)(void))ossl_rand_get_entropy },
    { OSSL_FUNC_CLEANUP_ENTROPY, (void (*)(void))ossl_rand_cleanup_entropy },
    { OSSL_FUNC_GET_NONCE, (void (*)(void))ossl_rand_get_nonce },
    { OSSL_FUNC_CLEANUP_NONCE, (void (*)(void))ossl_rand_cleanup_nonce },
#endif
    { OSSL_FUNC_CRYPTO_MALLOC, (void (*)(void))CRYPTO_malloc },
    { OSSL_FUNC_CRYPTO_ZALLOC, (void (*)(void))CRYPTO_zalloc },
    { OSSL_FUNC_CRYPTO_FREE, (void (*)(void))CRYPTO_free },
    { OSSL_FUNC_CRYPTO_CLEAR_FREE, (void (*)(void))CRYPTO_clear_free },
    { OSSL_FUNC_CRYPTO_REALLOC, (void (*)(void))CRYPTO_realloc },
    { OSSL_FUNC_CRYPTO_CLEAR_REALLOC, (void (*)(void))CRYPTO_clear_realloc },
    { OSSL_FUNC_CRYPTO_SECURE_MALLOC, (void (*)(void))CRYPTO_secure_malloc },
    { OSSL_FUNC_CRYPTO_SECURE_ZALLOC, (void (*)(void))CRYPTO_secure_zalloc },
    { OSSL_FUNC_CRYPTO_SECURE_FREE, (void (*)(void))CRYPTO_secure_free },
    { OSSL_FUNC_CRYPTO_SECURE_CLEAR_FREE,
        (void (*)(void))CRYPTO_secure_clear_free },
    { OSSL_FUNC_CRYPTO_SECURE_ALLOCATED,
        (void (*)(void))CRYPTO_secure_allocated },
    { OSSL_FUNC_OPENSSL_CLEANSE, (void (*)(void))OPENSSL_cleanse },
#ifndef FIPS_MODULE
    { OSSL_FUNC_PROVIDER_REGISTER_CHILD_CB,
        (void (*)(void))ossl_provider_register_child_cb },
    { OSSL_FUNC_PROVIDER_DEREGISTER_CHILD_CB,
        (void (*)(void))ossl_provider_deregister_child_cb },
    { OSSL_FUNC_PROVIDER_NAME,
        (void (*)(void))core_provider_get0_name },
    { OSSL_FUNC_PROVIDER_GET0_PROVIDER_CTX,
        (void (*)(void))core_provider_get0_provider_ctx },
    { OSSL_FUNC_PROVIDER_GET0_DISPATCH,
        (void (*)(void))core_provider_get0_dispatch },
    { OSSL_FUNC_PROVIDER_UP_REF,
        (void (*)(void))core_provider_up_ref_intern },
    { OSSL_FUNC_PROVIDER_FREE,
        (void (*)(void))core_provider_free_intern },
    { OSSL_FUNC_CORE_OBJ_ADD_SIGID, (void (*)(void))core_obj_add_sigid },
    { OSSL_FUNC_CORE_OBJ_CREATE, (void (*)(void))core_obj_create },
#endif
    { 0, NULL }
};
static const OSSL_DISPATCH *core_dispatch = core_dispatch_;
