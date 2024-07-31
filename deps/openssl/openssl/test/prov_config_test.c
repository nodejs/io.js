/*
 * Copyright 2021 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/evp.h>
#include <openssl/conf.h>
#include "testutil.h"

static char *configfile = NULL;
static char *recurseconfigfile = NULL;

/*
 * Test to make sure there are no leaks or failures from loading the config
 * file twice.
 */
static int test_double_config(void)
{
    OSSL_LIB_CTX *ctx = OSSL_LIB_CTX_new();
    int testresult = 0;
    EVP_MD *sha256 = NULL;

    if (!TEST_ptr(configfile))
        return 0;
    if (!TEST_ptr(ctx))
        return 0;

    if (!TEST_true(OSSL_LIB_CTX_load_config(ctx, configfile)))
        return 0;
    if (!TEST_true(OSSL_LIB_CTX_load_config(ctx, configfile)))
        return 0;

    /* Check we can actually fetch something */
    sha256 = EVP_MD_fetch(ctx, "SHA2-256", NULL);
    if (!TEST_ptr(sha256))
        goto err;

    testresult = 1;
 err:
    EVP_MD_free(sha256);
    OSSL_LIB_CTX_free(ctx);
    return testresult;
}

static int test_recursive_config(void)
{
    OSSL_LIB_CTX *ctx = OSSL_LIB_CTX_new();
    int testresult = 0;
    unsigned long err;

    if (!TEST_ptr(recurseconfigfile))
        goto err;

    if (!TEST_ptr(ctx))
        goto err;

    if (!TEST_false(OSSL_LIB_CTX_load_config(ctx, recurseconfigfile)))
        goto err;

    err = ERR_peek_error();
    /* We expect to get a recursion error here */
    if (ERR_GET_REASON(err) == CONF_R_RECURSIVE_SECTION_REFERENCE)
        testresult = 1;
 err:
    OSSL_LIB_CTX_free(ctx);
    return testresult;
}

OPT_TEST_DECLARE_USAGE("configfile\n")

int setup_tests(void)
{
    if (!test_skip_common_options()) {
        TEST_error("Error parsing test options\n");
        return 0;
    }

    if (!TEST_ptr(configfile = test_get_argument(0)))
        return 0;

    if (!TEST_ptr(recurseconfigfile = test_get_argument(1)))
        return 0;

    ADD_TEST(test_recursive_config);
    ADD_TEST(test_double_config);
    return 1;
}
