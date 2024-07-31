/*
 * Copyright 2020-2021 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright Siemens AG 2020
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/http.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <string.h>

#include "testutil.h"

static const ASN1_ITEM *x509_it = NULL;
static X509 *x509 = NULL;
#define RPATH "/path/result.crt"

typedef struct {
    BIO *out;
    char version;
    int keep_alive;
} server_args;

/*-
 * Pretty trivial HTTP mock server:
 * For POST, copy request headers+body from mem BIO 'in' as response to 'out'.
 * For GET, redirect to RPATH, else respond with 'rsp' of ASN1 type 'it'.
 * Respond with HTTP version 1.'version' and 'keep_alive' (unless implicit).
 */
static int mock_http_server(BIO *in, BIO *out, char version, int keep_alive,
                            ASN1_VALUE *rsp, const ASN1_ITEM *it)
{
    const char *req, *path;
    long count = BIO_get_mem_data(in, (unsigned char **)&req);
    const char *hdr = (char *)req;
    int is_get = count >= 4 && strncmp(hdr, "GET ", 4) == 0;
    int len;

    /* first line should contain "<GET or POST> <path> HTTP/1.x" */
    if (is_get)
        hdr += 4;
    else if (TEST_true(count >= 5 && strncmp(hdr, "POST ", 5) == 0))
        hdr += 5;
    else
        return 0;

    path = hdr;
    hdr = strchr(hdr, ' ');
    if (hdr == NULL)
        return 0;
    len = strlen("HTTP/1.");
    if (!TEST_strn_eq(++hdr, "HTTP/1.", len))
        return 0;
    hdr += len;
    /* check for HTTP version 1.0 .. 1.1 */
    if (!TEST_char_le('0', *hdr) || !TEST_char_le(*hdr++, '1'))
        return 0;
    if (!TEST_char_eq(*hdr++, '\r') || !TEST_char_eq(*hdr++, '\n'))
        return 0;
    count -= (hdr - req);
    if (count < 0 || out == NULL)
        return 0;

    if (strncmp(path, RPATH, strlen(RPATH)) != 0) {
        if (!is_get)
            return 0;
        return BIO_printf(out, "HTTP/1.%c 301 Moved Permanently\r\n"
                          "Location: %s\r\n\r\n",
                          version, RPATH) > 0; /* same server */
    }
    if (BIO_printf(out, "HTTP/1.%c 200 OK\r\n", version) <= 0)
        return 0;
    if ((version == '0') == keep_alive) /* otherwise, default */
        if (BIO_printf(out, "Connection: %s\r\n",
                       version == '0' ? "keep-alive" : "close") <= 0)
            return 0;
    if (is_get) { /* construct new header and body */
        if ((len = ASN1_item_i2d(rsp, NULL, it)) <= 0)
            return 0;
        if (BIO_printf(out, "Content-Type: application/x-x509-ca-cert\r\n"
                       "Content-Length: %d\r\n\r\n", len) <= 0)
            return 0;
        return ASN1_item_i2d_bio(it, out, rsp);
    } else {
        len = strlen("Connection: ");
        if (strncmp(hdr, "Connection: ", len) == 0) {
            /* skip req Connection header */
            hdr = strstr(hdr + len, "\r\n");
            if (hdr == NULL)
                return 0;
            hdr += 2;
        }
        /* echo remaining request header and body */
        return BIO_write(out, hdr, count) == count;
    }
}

static long http_bio_cb_ex(BIO *bio, int oper, const char *argp, size_t len,
                           int cmd, long argl, int ret, size_t *processed)
{
    server_args *args = (server_args *)BIO_get_callback_arg(bio);

    if (oper == (BIO_CB_CTRL | BIO_CB_RETURN) && cmd == BIO_CTRL_FLUSH)
        ret = mock_http_server(bio, args->out, args->version, args->keep_alive,
                               (ASN1_VALUE *)x509, x509_it);
    return ret;
}

static int test_http_x509(int do_get)
{
    X509 *rcert = NULL;
    BIO *wbio = BIO_new(BIO_s_mem());
    BIO *rbio = BIO_new(BIO_s_mem());
    server_args mock_args = { NULL, '0', 0 };
    BIO *rsp, *req = ASN1_item_i2d_mem_bio(x509_it, (ASN1_VALUE *)x509);
    STACK_OF(CONF_VALUE) *headers = NULL;
    const char content_type[] = "application/x-x509-ca-cert";
    int res = 0;

    if (wbio == NULL || rbio == NULL || req == NULL)
        goto err;
    mock_args.out = rbio;
    BIO_set_callback_ex(wbio, http_bio_cb_ex);
    BIO_set_callback_arg(wbio, (char *)&mock_args);

    rsp = do_get ?
        OSSL_HTTP_get("/will-be-redirected",
                      NULL /* proxy */, NULL /* no_proxy */,
                      wbio, rbio, NULL /* bio_update_fn */, NULL /* arg */,
                      0 /* buf_size */, headers, content_type,
                      1 /* expect_asn1 */,
                      OSSL_HTTP_DEFAULT_MAX_RESP_LEN, 0 /* timeout */)
        : OSSL_HTTP_transfer(NULL, NULL /* host */, NULL /* port */, RPATH,
                             0 /* use_ssl */,NULL /* proxy */, NULL /* no_pr */,
                             wbio, rbio, NULL /* bio_fn */, NULL /* arg */,
                             0 /* buf_size */, headers, content_type,
                             req, content_type, 1 /* expect_asn1 */,
                             OSSL_HTTP_DEFAULT_MAX_RESP_LEN, 0 /* timeout */,
                             0 /* keep_alive */);
    rcert = d2i_X509_bio(rsp, NULL);
    BIO_free(rsp);
    res = TEST_ptr(rcert) && TEST_int_eq(X509_cmp(x509, rcert), 0);

 err:
    X509_free(rcert);
    BIO_free(req);
    BIO_free(wbio);
    BIO_free(rbio);
    sk_CONF_VALUE_pop_free(headers, X509V3_conf_free);
    return res;
}

static int test_http_keep_alive(char version, int keep_alive, int kept_alive)
{
    BIO *wbio = BIO_new(BIO_s_mem());
    BIO *rbio = BIO_new(BIO_s_mem());
    BIO *rsp;
    server_args mock_args = { NULL, '0', 0 };
    const char *const content_type = "application/x-x509-ca-cert";
    OSSL_HTTP_REQ_CTX *rctx = NULL;
    int i, res = 0;

    if (wbio == NULL || rbio == NULL)
        goto err;
    mock_args.out = rbio;
    mock_args.version = version;
    mock_args.keep_alive = kept_alive;
    BIO_set_callback_ex(wbio, http_bio_cb_ex);
    BIO_set_callback_arg(wbio, (char *)&mock_args);

    for (res = 1, i = 1; res && i <= 2; i++) {
        rsp = OSSL_HTTP_transfer(&rctx, NULL /* server */, NULL /* port */,
                                 RPATH, 0 /* use_ssl */,
                                 NULL /* proxy */, NULL /* no_proxy */,
                                 wbio, rbio, NULL /* bio_update_fn */, NULL,
                                 0 /* buf_size */, NULL /* headers */,
                                 NULL /* content_type */, NULL /* req => GET */,
                                 content_type, 0 /* ASN.1 not expected */,
                                 0 /* max_resp_len */, 0 /* timeout */,
                                 keep_alive);
        if (keep_alive == 2 && kept_alive == 0)
            res = res && TEST_ptr_null(rsp)
                && TEST_int_eq(OSSL_HTTP_is_alive(rctx), 0);
        else
            res = res && TEST_ptr(rsp)
                && TEST_int_eq(OSSL_HTTP_is_alive(rctx), keep_alive > 0);
        BIO_free(rsp);
        (void)BIO_reset(rbio); /* discard response contents */
        keep_alive = 0;
    }
    OSSL_HTTP_close(rctx, res);

 err:
    BIO_free(wbio);
    BIO_free(rbio);
    return res;
}

static int test_http_url_ok(const char *url, int exp_ssl, const char *exp_host,
                            const char *exp_port, const char *exp_path)
{
    char *user, *host, *port, *path, *query, *frag;
    int exp_num, num, ssl;
    int res;

    if (!TEST_int_eq(sscanf(exp_port, "%d", &exp_num), 1))
        return 0;
    res = TEST_true(OSSL_HTTP_parse_url(url, &ssl, &user, &host, &port, &num,
                                        &path, &query, &frag))
        && TEST_str_eq(host, exp_host)
        && TEST_str_eq(port, exp_port)
        && TEST_int_eq(num, exp_num)
        && TEST_str_eq(path, exp_path)
        && TEST_int_eq(ssl, exp_ssl);
    if (res && *user != '\0')
        res = TEST_str_eq(user, "user:pass");
    if (res && *frag != '\0')
        res = TEST_str_eq(frag, "fr");
    if (res && *query != '\0')
        res = TEST_str_eq(query, "q");
    OPENSSL_free(user);
    OPENSSL_free(host);
    OPENSSL_free(port);
    OPENSSL_free(path);
    OPENSSL_free(query);
    OPENSSL_free(frag);
    return res;
}

static int test_http_url_path_query_ok(const char *url, const char *exp_path_qu)
{
    char *host, *path;
    int res;

    res = TEST_true(OSSL_HTTP_parse_url(url, NULL, NULL, &host, NULL, NULL,
                                        &path, NULL, NULL))
        && TEST_str_eq(host, "host")
        && TEST_str_eq(path, exp_path_qu);
    OPENSSL_free(host);
    OPENSSL_free(path);
    return res;
}

static int test_http_url_dns(void)
{
    return test_http_url_ok("host:65535/path", 0, "host", "65535", "/path");
}

static int test_http_url_path_query(void)
{
    return test_http_url_path_query_ok("http://usr@host:1/p?q=x#frag", "/p?q=x")
        && test_http_url_path_query_ok("http://host?query#frag", "/?query")
        && test_http_url_path_query_ok("http://host:9999#frag", "/");
}

static int test_http_url_userinfo_query_fragment(void)
{
    return test_http_url_ok("user:pass@host/p?q#fr", 0, "host", "80", "/p");
}

static int test_http_url_ipv4(void)
{
    return test_http_url_ok("https://1.2.3.4/p/q", 1, "1.2.3.4", "443", "/p/q");
}

static int test_http_url_ipv6(void)
{
    return test_http_url_ok("http://[FF01::101]:6", 0, "[FF01::101]", "6", "/");
}

static int test_http_url_invalid(const char *url)
{
    char *host = "1", *port = "1", *path = "1";
    int num = 1, ssl = 1;
    int res;

    res = TEST_false(OSSL_HTTP_parse_url(url, &ssl, NULL, &host, &port, &num,
                                         &path, NULL, NULL))
        && TEST_ptr_null(host)
        && TEST_ptr_null(port)
        && TEST_ptr_null(path);
    if (!res) {
        OPENSSL_free(host);
        OPENSSL_free(port);
        OPENSSL_free(path);
    }
    return res;
}

static int test_http_url_invalid_prefix(void)
{
    return test_http_url_invalid("htttps://1.2.3.4:65535/pkix");
}

static int test_http_url_invalid_port(void)
{
    return test_http_url_invalid("https://1.2.3.4:65536/pkix")
           && test_http_url_invalid("https://1.2.3.4:");
}

static int test_http_url_invalid_path(void)
{
    return test_http_url_invalid("https://[FF01::101]pkix");
}

static int test_http_get_x509(void)
{
    return test_http_x509(1);
}

static int test_http_post_x509(void)
{
    return test_http_x509(0);
}

static int test_http_keep_alive_0_no_no(void)
{
    return test_http_keep_alive('0', 0, 0);
}

static int test_http_keep_alive_1_no_no(void)
{
    return test_http_keep_alive('1', 0, 0);
}

static int test_http_keep_alive_0_prefer_yes(void)
{
    return test_http_keep_alive('0', 1, 1);
}

static int test_http_keep_alive_1_prefer_yes(void)
{
    return test_http_keep_alive('1', 1, 1);
}

static int test_http_keep_alive_0_require_yes(void)
{
    return test_http_keep_alive('0', 2, 1);
}

static int test_http_keep_alive_1_require_yes(void)
{
    return test_http_keep_alive('1', 2, 1);
}

static int test_http_keep_alive_0_require_no(void)
{
    return test_http_keep_alive('0', 2, 0);
}

static int test_http_keep_alive_1_require_no(void)
{
    return test_http_keep_alive('1', 2, 0);
}

void cleanup_tests(void)
{
    X509_free(x509);
}

OPT_TEST_DECLARE_USAGE("cert.pem\n")

int setup_tests(void)
{
    if (!test_skip_common_options())
        return 0;

    x509_it = ASN1_ITEM_rptr(X509);
    if (!TEST_ptr((x509 = load_cert_pem(test_get_argument(0), NULL))))
        return 0;

    ADD_TEST(test_http_url_dns);
    ADD_TEST(test_http_url_path_query);
    ADD_TEST(test_http_url_userinfo_query_fragment);
    ADD_TEST(test_http_url_ipv4);
    ADD_TEST(test_http_url_ipv6);
    ADD_TEST(test_http_url_invalid_prefix);
    ADD_TEST(test_http_url_invalid_port);
    ADD_TEST(test_http_url_invalid_path);
    ADD_TEST(test_http_get_x509);
    ADD_TEST(test_http_post_x509);
    ADD_TEST(test_http_keep_alive_0_no_no);
    ADD_TEST(test_http_keep_alive_1_no_no);
    ADD_TEST(test_http_keep_alive_0_prefer_yes);
    ADD_TEST(test_http_keep_alive_1_prefer_yes);
    ADD_TEST(test_http_keep_alive_0_require_yes);
    ADD_TEST(test_http_keep_alive_1_require_yes);
    ADD_TEST(test_http_keep_alive_0_require_no);
    ADD_TEST(test_http_keep_alive_1_require_no);
    return 1;
}
