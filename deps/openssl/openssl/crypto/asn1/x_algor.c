/*
 * Copyright 1998-2021 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stddef.h>
#include <openssl/x509.h>
#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/err.h>
#include "crypto/asn1.h"
#include "crypto/evp.h"

ASN1_SEQUENCE(X509_ALGOR) = {
        ASN1_SIMPLE(X509_ALGOR, algorithm, ASN1_OBJECT),
        ASN1_OPT(X509_ALGOR, parameter, ASN1_ANY)
} ASN1_SEQUENCE_END(X509_ALGOR)

ASN1_ITEM_TEMPLATE(X509_ALGORS) =
        ASN1_EX_TEMPLATE_TYPE(ASN1_TFLG_SEQUENCE_OF, 0, algorithms, X509_ALGOR)
ASN1_ITEM_TEMPLATE_END(X509_ALGORS)

IMPLEMENT_ASN1_FUNCTIONS(X509_ALGOR)
IMPLEMENT_ASN1_ENCODE_FUNCTIONS_fname(X509_ALGORS, X509_ALGORS, X509_ALGORS)
IMPLEMENT_ASN1_DUP_FUNCTION(X509_ALGOR)

int X509_ALGOR_set0(X509_ALGOR *alg, ASN1_OBJECT *aobj, int ptype, void *pval)
{
    if (alg == NULL)
        return 0;

    if (ptype != V_ASN1_UNDEF) {
        if (alg->parameter == NULL)
            alg->parameter = ASN1_TYPE_new();
        if (alg->parameter == NULL)
            return 0;
    }

    ASN1_OBJECT_free(alg->algorithm);
    alg->algorithm = aobj;

    if (ptype == 0)
        return 1;
    if (ptype == V_ASN1_UNDEF) {
        ASN1_TYPE_free(alg->parameter);
        alg->parameter = NULL;
    } else
        ASN1_TYPE_set(alg->parameter, ptype, pval);
    return 1;
}

void X509_ALGOR_get0(const ASN1_OBJECT **paobj, int *pptype,
                     const void **ppval, const X509_ALGOR *algor)
{
    if (paobj)
        *paobj = algor->algorithm;
    if (pptype) {
        if (algor->parameter == NULL) {
            *pptype = V_ASN1_UNDEF;
            return;
        } else
            *pptype = algor->parameter->type;
        if (ppval)
            *ppval = algor->parameter->value.ptr;
    }
}

/* Set up an X509_ALGOR DigestAlgorithmIdentifier from an EVP_MD */

void X509_ALGOR_set_md(X509_ALGOR *alg, const EVP_MD *md)
{
    int param_type;

    if (md->flags & EVP_MD_FLAG_DIGALGID_ABSENT)
        param_type = V_ASN1_UNDEF;
    else
        param_type = V_ASN1_NULL;

    X509_ALGOR_set0(alg, OBJ_nid2obj(EVP_MD_get_type(md)), param_type, NULL);

}

int X509_ALGOR_cmp(const X509_ALGOR *a, const X509_ALGOR *b)
{
    int rv;
    rv = OBJ_cmp(a->algorithm, b->algorithm);
    if (rv)
        return rv;
    if (!a->parameter && !b->parameter)
        return 0;
    return ASN1_TYPE_cmp(a->parameter, b->parameter);
}

int X509_ALGOR_copy(X509_ALGOR *dest, const X509_ALGOR *src)
{
    if (src == NULL || dest == NULL)
        return 0;

    if (dest->algorithm)
         ASN1_OBJECT_free(dest->algorithm);
    dest->algorithm = NULL;

    if (dest->parameter)
        ASN1_TYPE_free(dest->parameter);
    dest->parameter = NULL;

    if (src->algorithm)
        if ((dest->algorithm = OBJ_dup(src->algorithm)) == NULL)
            return 0;

    if (src->parameter != NULL) {
        dest->parameter = ASN1_TYPE_new();
        if (dest->parameter == NULL)
            return 0;

        /* Assuming this is also correct for a BOOL.
         * set does copy as a side effect.
         */
        if (ASN1_TYPE_set1(dest->parameter, src->parameter->type,
                           src->parameter->value.ptr) == 0)
            return 0;
    }

    return 1;
}

/* allocate and set algorithm ID from EVP_MD, default SHA1 */
int ossl_x509_algor_new_from_md(X509_ALGOR **palg, const EVP_MD *md)
{
    /* Default is SHA1 so no need to create it - still success */
    if (md == NULL || EVP_MD_is_a(md, "SHA1"))
        return 1;
    *palg = X509_ALGOR_new();
    if (*palg == NULL)
        return 0;
    X509_ALGOR_set_md(*palg, md);
    return 1;
}

/* convert algorithm ID to EVP_MD, default SHA1 */
const EVP_MD *ossl_x509_algor_get_md(X509_ALGOR *alg)
{
    const EVP_MD *md;

    if (alg == NULL)
        return EVP_sha1();
    md = EVP_get_digestbyobj(alg->algorithm);
    if (md == NULL)
        ERR_raise(ERR_LIB_ASN1, ASN1_R_UNKNOWN_DIGEST);
    return md;
}

X509_ALGOR *ossl_x509_algor_mgf1_decode(X509_ALGOR *alg)
{
    if (OBJ_obj2nid(alg->algorithm) != NID_mgf1)
        return NULL;
    return ASN1_TYPE_unpack_sequence(ASN1_ITEM_rptr(X509_ALGOR),
                                     alg->parameter);
}

/* Allocate and set MGF1 algorithm ID from EVP_MD */
int ossl_x509_algor_md_to_mgf1(X509_ALGOR **palg, const EVP_MD *mgf1md)
{
    X509_ALGOR *algtmp = NULL;
    ASN1_STRING *stmp = NULL;

    *palg = NULL;
    if (mgf1md == NULL || EVP_MD_is_a(mgf1md, "SHA1"))
        return 1;
    /* need to embed algorithm ID inside another */
    if (!ossl_x509_algor_new_from_md(&algtmp, mgf1md))
        goto err;
    if (ASN1_item_pack(algtmp, ASN1_ITEM_rptr(X509_ALGOR), &stmp) == NULL)
         goto err;
    *palg = X509_ALGOR_new();
    if (*palg == NULL)
        goto err;
    if (!X509_ALGOR_set0(*palg, OBJ_nid2obj(NID_mgf1), V_ASN1_SEQUENCE, stmp)) {
        X509_ALGOR_free(*palg);
        *palg = NULL;
        goto err;
    }
    stmp = NULL;
 err:
    ASN1_STRING_free(stmp);
    X509_ALGOR_free(algtmp);
    if (*palg != NULL)
        return 1;
    return 0;
}
