/*-
 * Copyright 2019-2024 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*-
 * Example of using EVP_MD_fetch and EVP_Digest* methods to calculate
 * a digest of static buffers
 * You can find SHA3 test vectors from NIST here:
 * https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Algorithm-Validation-Program/documents/sha3/sha-3bytetestvectors.zip
 * For example, contains these lines:
    Len = 80
    Msg = 1ca984dcc913344370cf
    MD = 6915ea0eeffb99b9b246a0e34daf3947852684c3d618260119a22835659e4f23d4eb66a15d0affb8e93771578f5e8f25b7a5f2a55f511fb8b96325ba2cd14816
 * use xxd convert the hex message string to binary input for EVP_MD_stdin:
 * echo "1ca984dcc913344370cf" | xxd -r -p | ./EVP_MD_stdin
 * and then verify the output matches MD above.
 */

#include <string.h>
#include <stdio.h>
#include <openssl/err.h>
#include <openssl/evp.h>

/*-
 * This demonstration will show how to digest data using
 * a BIO created to read from stdin
 */

int demonstrate_digest(BIO *input)
{
    OSSL_LIB_CTX *library_context = NULL;
    int result = 0;
    const char * option_properties = NULL;
    EVP_MD *message_digest = NULL;
    EVP_MD_CTX *digest_context = NULL;
    int digest_length;
    unsigned char *digest_value = NULL;
    unsigned char buffer[512];
    int ii;

    library_context = OSSL_LIB_CTX_new();
    if (library_context == NULL) {
        fprintf(stderr, "OSSL_LIB_CTX_new() returned NULL\n");
        goto cleanup;
    }

    /*
     * Fetch a message digest by name
     * The algorithm name is case insensitive. 
     * See providers(7) for details about algorithm fetching
     */
    message_digest = EVP_MD_fetch(library_context,
                                  "SHA3-512", option_properties);
    if (message_digest == NULL) {
        fprintf(stderr, "EVP_MD_fetch could not find SHA3-512.");
        ERR_print_errors_fp(stderr);
        OSSL_LIB_CTX_free(library_context);
        return 0;
    }
    /* Determine the length of the fetched digest type */
    digest_length = EVP_MD_get_size(message_digest);
    if (digest_length <= 0) {
        fprintf(stderr, "EVP_MD_get_size returned invalid size.\n");
        goto cleanup;
    }

    digest_value = OPENSSL_malloc(digest_length);
    if (digest_value == NULL) {
        fprintf(stderr, "No memory.\n");
        goto cleanup;
    }
    /*
     * Make a message digest context to hold temporary state
     * during digest creation
     */
    digest_context = EVP_MD_CTX_new();
    if (digest_context == NULL) {
        fprintf(stderr, "EVP_MD_CTX_new failed.\n");
        ERR_print_errors_fp(stderr);
        goto cleanup;
    }
    /*
     * Initialize the message digest context to use the fetched 
     * digest provider
     */
    if (EVP_DigestInit(digest_context, message_digest) != 1) {
        fprintf(stderr, "EVP_DigestInit failed.\n");
        ERR_print_errors_fp(stderr);
        goto cleanup;
    }
    while ((ii = BIO_read(input, buffer, sizeof(buffer))) > 0) {
        if (EVP_DigestUpdate(digest_context, buffer, ii) != 1) {
            fprintf(stderr, "EVP_DigestUpdate() failed.\n");
            goto cleanup;
        }
    }
    if (EVP_DigestFinal(digest_context, digest_value, &digest_length) != 1) {
        fprintf(stderr, "EVP_DigestFinal() failed.\n");
        goto cleanup;
    }
    result = 1;
    for (ii=0; ii<digest_length; ii++) {
        fprintf(stdout, "%02x", digest_value[ii]);
    }
    fprintf(stdout, "\n");

cleanup:
    if (result != 1)
        ERR_print_errors_fp(stderr);
    /* OpenSSL free functions will ignore NULL arguments */
    EVP_MD_CTX_free(digest_context);
    OPENSSL_free(digest_value);
    EVP_MD_free(message_digest);

    OSSL_LIB_CTX_free(library_context);
    return result;
}

int main(void)
{
    int result = 1;
    BIO *input = BIO_new_fd( fileno(stdin), 1 );

    if (input != NULL) {
        result = demonstrate_digest(input);
        BIO_free(input);
    }
    return result;
}
