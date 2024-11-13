/*
 * Copyright 2019-2024 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "crypto/cryptlib.h"
#include <openssl/conf.h>
#include "internal/thread_once.h"
#include "internal/property.h"
#include "internal/core.h"
#include "internal/bio.h"
#include "internal/provider.h"
#include "crypto/ctype.h"
#include "crypto/rand.h"

struct ossl_lib_ctx_onfree_list_st {
    ossl_lib_ctx_onfree_fn *fn;
    struct ossl_lib_ctx_onfree_list_st *next;
};

struct ossl_lib_ctx_st {
    CRYPTO_RWLOCK *lock;
    CRYPTO_EX_DATA data;

    /*
     * For most data in the OSSL_LIB_CTX we just use ex_data to store it. But
     * that doesn't work for ex_data itself - so we store that directly.
     */
    OSSL_EX_DATA_GLOBAL global;

    /* Map internal static indexes to dynamically created indexes */
    int dyn_indexes[OSSL_LIB_CTX_MAX_INDEXES];

    /* Keep a separate lock for each index */
    CRYPTO_RWLOCK *index_locks[OSSL_LIB_CTX_MAX_INDEXES];

    CRYPTO_RWLOCK *oncelock;
    int run_once_done[OSSL_LIB_CTX_MAX_RUN_ONCE];
    int run_once_ret[OSSL_LIB_CTX_MAX_RUN_ONCE];
    struct ossl_lib_ctx_onfree_list_st *onfreelist;
    unsigned int ischild:1;
};

int ossl_lib_ctx_write_lock(OSSL_LIB_CTX *ctx)
{
    return CRYPTO_THREAD_write_lock(ossl_lib_ctx_get_concrete(ctx)->lock);
}

int ossl_lib_ctx_read_lock(OSSL_LIB_CTX *ctx)
{
    return CRYPTO_THREAD_read_lock(ossl_lib_ctx_get_concrete(ctx)->lock);
}

int ossl_lib_ctx_unlock(OSSL_LIB_CTX *ctx)
{
    return CRYPTO_THREAD_unlock(ossl_lib_ctx_get_concrete(ctx)->lock);
}

int ossl_lib_ctx_is_child(OSSL_LIB_CTX *ctx)
{
    ctx = ossl_lib_ctx_get_concrete(ctx);

    if (ctx == NULL)
        return 0;
    return ctx->ischild;
}

static int context_init(OSSL_LIB_CTX *ctx)
{
    size_t i;
    int exdata_done = 0;

    ctx->lock = CRYPTO_THREAD_lock_new();
    if (ctx->lock == NULL)
        return 0;

    ctx->oncelock = CRYPTO_THREAD_lock_new();
    if (ctx->oncelock == NULL)
        goto err;

    for (i = 0; i < OSSL_LIB_CTX_MAX_INDEXES; i++) {
        ctx->index_locks[i] = CRYPTO_THREAD_lock_new();
        ctx->dyn_indexes[i] = -1;
        if (ctx->index_locks[i] == NULL)
            goto err;
    }

    /* OSSL_LIB_CTX is built on top of ex_data so we initialise that directly */
    if (!ossl_do_ex_data_init(ctx))
        goto err;
    exdata_done = 1;

    if (!ossl_crypto_new_ex_data_ex(ctx, CRYPTO_EX_INDEX_OSSL_LIB_CTX, NULL,
                                    &ctx->data))
        goto err;

    /* Everything depends on properties, so we also pre-initialise that */
    if (!ossl_property_parse_init(ctx))
        goto err;

    return 1;
 err:
    if (exdata_done)
        ossl_crypto_cleanup_all_ex_data_int(ctx);
    for (i = 0; i < OSSL_LIB_CTX_MAX_INDEXES; i++)
        CRYPTO_THREAD_lock_free(ctx->index_locks[i]);
    CRYPTO_THREAD_lock_free(ctx->oncelock);
    CRYPTO_THREAD_lock_free(ctx->lock);
    memset(ctx, '\0', sizeof(*ctx));
    return 0;
}

static int context_deinit(OSSL_LIB_CTX *ctx)
{
    struct ossl_lib_ctx_onfree_list_st *tmp, *onfree;
    int i;

    if (ctx == NULL)
        return 1;

    ossl_ctx_thread_stop(ctx);

    onfree = ctx->onfreelist;
    while (onfree != NULL) {
        onfree->fn(ctx);
        tmp = onfree;
        onfree = onfree->next;
        OPENSSL_free(tmp);
    }
    CRYPTO_free_ex_data(CRYPTO_EX_INDEX_OSSL_LIB_CTX, NULL, &ctx->data);
    ossl_crypto_cleanup_all_ex_data_int(ctx);
    for (i = 0; i < OSSL_LIB_CTX_MAX_INDEXES; i++)
        CRYPTO_THREAD_lock_free(ctx->index_locks[i]);

    CRYPTO_THREAD_lock_free(ctx->oncelock);
    CRYPTO_THREAD_lock_free(ctx->lock);
    ctx->lock = NULL;
    return 1;
}

#ifndef FIPS_MODULE
/* The default default context */
static OSSL_LIB_CTX default_context_int;

static CRYPTO_ONCE default_context_init = CRYPTO_ONCE_STATIC_INIT;
static CRYPTO_THREAD_LOCAL default_context_thread_local;

DEFINE_RUN_ONCE_STATIC(default_context_do_init)
{
    return CRYPTO_THREAD_init_local(&default_context_thread_local, NULL)
        && context_init(&default_context_int);
}

void ossl_lib_ctx_default_deinit(void)
{
    context_deinit(&default_context_int);
    CRYPTO_THREAD_cleanup_local(&default_context_thread_local);
}

static OSSL_LIB_CTX *get_thread_default_context(void)
{
    if (!RUN_ONCE(&default_context_init, default_context_do_init))
        return NULL;

    return CRYPTO_THREAD_get_local(&default_context_thread_local);
}

static OSSL_LIB_CTX *get_default_context(void)
{
    OSSL_LIB_CTX *current_defctx = get_thread_default_context();

    if (current_defctx == NULL)
        current_defctx = &default_context_int;
    return current_defctx;
}

static int set_default_context(OSSL_LIB_CTX *defctx)
{
    if (defctx == &default_context_int)
        defctx = NULL;

    return CRYPTO_THREAD_set_local(&default_context_thread_local, defctx);
}
#endif

OSSL_LIB_CTX *OSSL_LIB_CTX_new(void)
{
    OSSL_LIB_CTX *ctx = OPENSSL_zalloc(sizeof(*ctx));

    if (ctx != NULL && !context_init(ctx)) {
        OPENSSL_free(ctx);
        ctx = NULL;
    }
    return ctx;
}

#ifndef FIPS_MODULE
OSSL_LIB_CTX *OSSL_LIB_CTX_new_from_dispatch(const OSSL_CORE_HANDLE *handle,
                                             const OSSL_DISPATCH *in)
{
    OSSL_LIB_CTX *ctx = OSSL_LIB_CTX_new();

    if (ctx == NULL)
        return NULL;

    if (!ossl_bio_init_core(ctx, in)) {
        OSSL_LIB_CTX_free(ctx);
        return NULL;
    }

    return ctx;
}

OSSL_LIB_CTX *OSSL_LIB_CTX_new_child(const OSSL_CORE_HANDLE *handle,
                                     const OSSL_DISPATCH *in)
{
    OSSL_LIB_CTX *ctx = OSSL_LIB_CTX_new_from_dispatch(handle, in);

    if (ctx == NULL)
        return NULL;

    if (!ossl_provider_init_as_child(ctx, handle, in)) {
        OSSL_LIB_CTX_free(ctx);
        return NULL;
    }
    ctx->ischild = 1;

    return ctx;
}

int OSSL_LIB_CTX_load_config(OSSL_LIB_CTX *ctx, const char *config_file)
{
    return CONF_modules_load_file_ex(ctx, config_file, NULL, 0) > 0;
}
#endif

void OSSL_LIB_CTX_free(OSSL_LIB_CTX *ctx)
{
    if (ctx == NULL || ossl_lib_ctx_is_default(ctx))
        return;

#ifndef FIPS_MODULE
    if (ctx->ischild)
        ossl_provider_deinit_child(ctx);
#endif
    context_deinit(ctx);
    OPENSSL_free(ctx);
}

#ifndef FIPS_MODULE
OSSL_LIB_CTX *OSSL_LIB_CTX_get0_global_default(void)
{
    if (!RUN_ONCE(&default_context_init, default_context_do_init))
        return NULL;

    return &default_context_int;
}

OSSL_LIB_CTX *OSSL_LIB_CTX_set0_default(OSSL_LIB_CTX *libctx)
{
    OSSL_LIB_CTX *current_defctx;

    if ((current_defctx = get_default_context()) != NULL) {
        if (libctx != NULL)
            set_default_context(libctx);
        return current_defctx;
    }

    return NULL;
}

void ossl_release_default_drbg_ctx(void)
{
    int dynidx = default_context_int.dyn_indexes[OSSL_LIB_CTX_DRBG_INDEX];

    /* early release of the DRBG in global default libctx, no locking */
    if (dynidx != -1) {
        void *data;

        data = CRYPTO_get_ex_data(&default_context_int.data, dynidx);
        ossl_rand_ctx_free(data);
        CRYPTO_set_ex_data(&default_context_int.data, dynidx, NULL);
    }
}
#endif

OSSL_LIB_CTX *ossl_lib_ctx_get_concrete(OSSL_LIB_CTX *ctx)
{
#ifndef FIPS_MODULE
    if (ctx == NULL)
        return get_default_context();
#endif
    return ctx;
}

int ossl_lib_ctx_is_default(OSSL_LIB_CTX *ctx)
{
#ifndef FIPS_MODULE
    if (ctx == NULL || ctx == get_default_context())
        return 1;
#endif
    return 0;
}

int ossl_lib_ctx_is_global_default(OSSL_LIB_CTX *ctx)
{
#ifndef FIPS_MODULE
    if (ossl_lib_ctx_get_concrete(ctx) == &default_context_int)
        return 1;
#endif
    return 0;
}

static void ossl_lib_ctx_generic_new(void *parent_ign, void *ptr_ign,
                                     CRYPTO_EX_DATA *ad, int index,
                                     long argl_ign, void *argp)
{
    const OSSL_LIB_CTX_METHOD *meth = argp;
    OSSL_LIB_CTX *ctx = ossl_crypto_ex_data_get_ossl_lib_ctx(ad);
    void *ptr = meth->new_func(ctx);

    if (ptr != NULL) {
        if (!CRYPTO_THREAD_write_lock(ctx->lock))
            /*
             * Can't return something, so best to hope that something will
             * fail later. :(
             */
            return;
        CRYPTO_set_ex_data(ad, index, ptr);
        CRYPTO_THREAD_unlock(ctx->lock);
    }
}
static void ossl_lib_ctx_generic_free(void *parent_ign, void *ptr,
                                      CRYPTO_EX_DATA *ad, int index,
                                      long argl_ign, void *argp)
{
    const OSSL_LIB_CTX_METHOD *meth = argp;

    meth->free_func(ptr);
}

static int ossl_lib_ctx_init_index(OSSL_LIB_CTX *ctx, int static_index,
                                   const OSSL_LIB_CTX_METHOD *meth)
{
    int idx;

    ctx = ossl_lib_ctx_get_concrete(ctx);
    if (ctx == NULL)
        return 0;

    idx = ossl_crypto_get_ex_new_index_ex(ctx, CRYPTO_EX_INDEX_OSSL_LIB_CTX, 0,
                                          (void *)meth,
                                          ossl_lib_ctx_generic_new,
                                          NULL, ossl_lib_ctx_generic_free,
                                          meth->priority);
    if (idx < 0)
        return 0;

    ctx->dyn_indexes[static_index] = idx;
    return 1;
}

void *ossl_lib_ctx_get_data(OSSL_LIB_CTX *ctx, int index,
                            const OSSL_LIB_CTX_METHOD *meth)
{
    void *data = NULL;
    int dynidx;

    ctx = ossl_lib_ctx_get_concrete(ctx);
    if (ctx == NULL)
        return NULL;

    if (!CRYPTO_THREAD_read_lock(ctx->lock))
        return NULL;
    dynidx = ctx->dyn_indexes[index];
    CRYPTO_THREAD_unlock(ctx->lock);

    if (dynidx != -1) {
        if (!CRYPTO_THREAD_read_lock(ctx->index_locks[index]))
            return NULL;
        if (!CRYPTO_THREAD_read_lock(ctx->lock)) {
            CRYPTO_THREAD_unlock(ctx->index_locks[index]);
            return NULL;
        }
        data = CRYPTO_get_ex_data(&ctx->data, dynidx);
        CRYPTO_THREAD_unlock(ctx->lock);
        CRYPTO_THREAD_unlock(ctx->index_locks[index]);
        return data;
    }

    if (!CRYPTO_THREAD_write_lock(ctx->index_locks[index]))
        return NULL;
    if (!CRYPTO_THREAD_write_lock(ctx->lock)) {
        CRYPTO_THREAD_unlock(ctx->index_locks[index]);
        return NULL;
    }

    dynidx = ctx->dyn_indexes[index];
    if (dynidx != -1) {
        data = CRYPTO_get_ex_data(&ctx->data, dynidx);
        CRYPTO_THREAD_unlock(ctx->lock);
        CRYPTO_THREAD_unlock(ctx->index_locks[index]);
        return data;
    }

    if (!ossl_lib_ctx_init_index(ctx, index, meth)) {
        CRYPTO_THREAD_unlock(ctx->lock);
        CRYPTO_THREAD_unlock(ctx->index_locks[index]);
        return NULL;
    }

    CRYPTO_THREAD_unlock(ctx->lock);

    /*
     * The alloc call ensures there's a value there. We release the ctx->lock
     * for this, because the allocation itself may recursively call
     * ossl_lib_ctx_get_data for other indexes (never this one). The allocation
     * will itself aquire the ctx->lock when it actually comes to store the
     * allocated data (see ossl_lib_ctx_generic_new() above). We call
     * ossl_crypto_alloc_ex_data_intern() here instead of CRYPTO_alloc_ex_data().
     * They do the same thing except that the latter calls CRYPTO_get_ex_data()
     * as well - which we must not do without holding the ctx->lock.
     */
    if (ossl_crypto_alloc_ex_data_intern(CRYPTO_EX_INDEX_OSSL_LIB_CTX, NULL,
                                         &ctx->data, ctx->dyn_indexes[index])) {
        if (!CRYPTO_THREAD_read_lock(ctx->lock))
            goto end;
        data = CRYPTO_get_ex_data(&ctx->data, ctx->dyn_indexes[index]);
        CRYPTO_THREAD_unlock(ctx->lock);
    }

end:
    CRYPTO_THREAD_unlock(ctx->index_locks[index]);
    return data;
}

OSSL_EX_DATA_GLOBAL *ossl_lib_ctx_get_ex_data_global(OSSL_LIB_CTX *ctx)
{
    ctx = ossl_lib_ctx_get_concrete(ctx);
    if (ctx == NULL)
        return NULL;
    return &ctx->global;
}

int ossl_lib_ctx_run_once(OSSL_LIB_CTX *ctx, unsigned int idx,
                          ossl_lib_ctx_run_once_fn run_once_fn)
{
    int done = 0, ret = 0;

    ctx = ossl_lib_ctx_get_concrete(ctx);
    if (ctx == NULL)
        return 0;

    if (!CRYPTO_THREAD_read_lock(ctx->oncelock))
        return 0;
    done = ctx->run_once_done[idx];
    if (done)
        ret = ctx->run_once_ret[idx];
    CRYPTO_THREAD_unlock(ctx->oncelock);

    if (done)
        return ret;

    if (!CRYPTO_THREAD_write_lock(ctx->oncelock))
        return 0;
    if (ctx->run_once_done[idx]) {
        ret = ctx->run_once_ret[idx];
        CRYPTO_THREAD_unlock(ctx->oncelock);
        return ret;
    }

    ret = run_once_fn(ctx);
    ctx->run_once_done[idx] = 1;
    ctx->run_once_ret[idx] = ret;
    CRYPTO_THREAD_unlock(ctx->oncelock);

    return ret;
}

int ossl_lib_ctx_onfree(OSSL_LIB_CTX *ctx, ossl_lib_ctx_onfree_fn onfreefn)
{
    struct ossl_lib_ctx_onfree_list_st *newonfree
        = OPENSSL_malloc(sizeof(*newonfree));

    if (newonfree == NULL)
        return 0;

    newonfree->fn = onfreefn;
    newonfree->next = ctx->onfreelist;
    ctx->onfreelist = newonfree;

    return 1;
}

const char *ossl_lib_ctx_get_descriptor(OSSL_LIB_CTX *libctx)
{
#ifdef FIPS_MODULE
    return "FIPS internal library context";
#else
    if (ossl_lib_ctx_is_global_default(libctx))
        return "Global default library context";
    if (ossl_lib_ctx_is_default(libctx))
        return "Thread-local default library context";
    return "Non-default library context";
#endif
}
