/* MIT License
 *
 * Copyright (c) 2024 Brad House
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

/* DNS cookies are a simple form of learned mutual authentication supported by
 * most DNS server implementations these days and can help prevent DNS Cache
 * Poisoning attacks for clients and DNS amplification attacks for servers.
 *
 * A good overview is here:
 * https://www.dotmagazine.online/issues/digital-responsibility-and-sustainability/dns-cookies-transaction-mechanism
 *
 * RFCs used for implementation are
 * [RFC7873](https://datatracker.ietf.org/doc/html/rfc7873) which is extended by
 * [RFC9018](https://datatracker.ietf.org/doc/html/rfc9018).
 *
 * Though this could be used on TCP, the likelihood of it being useful is small
 * and could cause some issues.  TCP is better used as a fallback in case there
 * are issues with DNS Cookie support in the upstream servers (e.g. AnyCast
 * cluster issues).
 *
 * While most recursive DNS servers support DNS Cookies, public DNS servers like
 * Google (8.8.8.8, 8.8.4.4) and CloudFlare (1.1.1.1, 1.0.0.1) don't seem to
 * have this enabled yet for unknown reasons.
 *
 * The risk to having DNS Cookie support always enabled is nearly zero as there
 * is built-in detection support and it will simply bypass using cookies if the
 * remote server doesn't support it.  The problem arises if a remote server
 * supports DNS cookies, then stops supporting them (such as if an administrator
 * reconfigured the server, or maybe there are different servers in a cluster
 * with different configurations).  We need to detect this behavior by tracking
 * how much time has gone by since we received our last valid cookie reply, and
 * if we exceed the threshold, reset all cookie parameters like we haven't
 * attempted a request yet.
 *
 * ## Implementation Plan
 *
 * ### Constants:
 *  - `COOKIE_CLIENT_TIMEOUT`: 86400s (1 day)
 *     - How often to regenerate the per-server client cookie, even if our
 *       source ip address hasn't changed.
 *  - `COOKIE_UNSUPPORTED_TIMEOUT`: 300s (5 minutes)
 *     - If a server responds without a cookie in the reply, this is how long to
 *       wait before attempting to send a client cookie again.
 *  - `COOKIE_REGRESSION_TIMEOUT`: 120s (2 minutes)
 *     - If a server was once known to return cookies, and all of a sudden stops
 *       returning cookies (but the reply is otherwise valid), this is how long
 *       to continue to attempt to use cookies before giving up and resetting.
 *       Such an event would cause an outage for this duration, but since a
 *       cache poisoning attack should be dropping invalid replies we should be
 *       able to still get the valid reply and not assume it is a server
 *       regression just because we received replies without cookies.
 *  - `COOKIE_RESEND_MAX`: 3
 *    - Maximum times to resend a query to a server due to the server responding
 *      with `BAD_COOKIE`, after this, we switch to TCP.
 *
 * ### Per-server variables:
 *  - `cookie.state`: Known state of cookie support, enumeration.
 *    - `INITIAL` (0): Initial state, not yet determined. Used during startup.
 *    - `GENERATED` (1): Cookie has been generated and sent to a server, but no
 *      validated response yet.
 *    - `SUPPORTED` (2):  Server has been determined to properly support cookies
 *    - `UNSUPPORTED` (3): Server has been determined to not support cookies
 *  - `cookie.client` : 8 byte randomly generated client cookie
 *  - `cookie.client_ts`: Timestamp client cookie was generated
 *  - `cookie.client_ip`: IP address client used to connect to server
 *  - `cookie.server`: 8 to 32 byte server cookie
 *  - `cookie.server_len`: length of server cookie
 *  - `cookie.unsupported_ts`: Timestamp of last attempt to use a cookies, but
 *    it was determined that the server didn't support them.
 *
 * ### Per-query variables:
 *  - `query.client_cookie`: Duplicate of `cookie.client` at the point in time
 *    the query is put on the wire.  This should be available in the
 *    `ares_dns_record_t` for the request for verification purposes so we don't
 *    actually need to duplicate this, just naming it here for the ease of
 *    documentation below.
 * - `query.cookie_try_count`: Number of tries to send a cookie but receive
 *   `BAD_COOKIE` responses.  Used to know when we need to switch to TCP.
 *
 * ### Procedure:
 * **NOTE**: These steps will all be done after obtaining a connection handle as
 * some of these steps depend on determining the source ip address for the
 * connection.
 *
 * 1. If the query is not using EDNS, then **skip any remaining processing**.
 * 2. If using TCP, ensure there is no EDNS cookie opt (10) set (there may have
 *    been if this is a resend after upgrade to TCP), then **skip any remaining
 *    processing**.
 * 3. If `cookie.state == SUPPORTED`, `cookie.unsupported_ts` is non-zero, and
 *    evaluates greater than `COOKIE_REGRESSION_TIMEOUT`, then clear all cookie
 *    settings, set `cookie.state = INITIAL`. Continue to next step (4)
 * 4. If `cookie.state == UNSUPPORTED`
 *     - If `cookie.unsupported_ts` evaluates less than
 *       `COOKIE_UNSUPPORTED_TIMEOUT`
 *        - Ensure there is no EDNS cookie opt (10) set (shouldn't be unless
 *          requestor had put this themselves), then **skip any remaining
 *          processing** as we don't want to try to send cookies.
 *     - Otherwise:
 *       - clear all cookie settings, set `cookie.state = INITIAL`.
 *       - Continue to next step (5) which will send a new cookie.
 * 5. If `cookie.state == INITIAL`:
 *    - randomly generate new `cookie.client`
 *    - set `cookie.client_ts` to the current timestamp.
 *    - set `cookie.state = GENERATED`.
 *    - set `cookie.client_ip` to the current source ip address.
 * 6. If `cookie.state == GENERATED || cookie.state == SUPPORTED` and
 *    `cookie.client_ip` does not match the current source ip address:
 *    - clear `cookie.server`
 *    - randomly generate new `cookie.client`
 *    - set `cookie.client_ts` to the current timestamp.
 *    - set `cookie.client_ip` to the current source ip address.
 *    - do not change the `cookie.state`
 * 7. If `cookie.state == SUPPORTED` and `cookie.client_ts` evaluation exceeds
 *    `COOKIE_CLIENT_TIMEOUT`:
 *    - clear `cookie.server`
 *    - randomly generate new `cookie.client`
 *    - set `cookie.client_ts` to the current timestamp.
 *    - set `cookie.client_ip` to the current source ip address.
 *    - do not change the `cookie.state`
 * 8. Generate EDNS OPT record (10) for client cookie.  The option value will be
 *    the `cookie.client` concatenated with the `cookie.server`.  If there is no
 *    known server cookie, it will not be appended. Copy `cookie.client` to
 *    `query.client_cookie` to handle possible client cookie changes by other
 *    queries before a reply is received (technically this is in the cached
 *    `ares_dns_record_t` so no need to manually do this). Send request to
 *    server.
 * 9. Evaluate response:
 *     1. If invalid EDNS OPT cookie (10) length sent back in response (valid
 *        length is 16-40), or bad client cookie value (validate first 8 bytes
 *        against `query.client_cookie` not `cookie.client`), **drop response**
 *        as if it hadn't been received.  This is likely a spoofing attack.
 *        Wait for valid response up to normal response timeout.
 *     2. If a EDNS OPT cookie (10) server cookie is returned:
 *         - set `cookie.unsupported_ts` to zero and `cookie.state = SUPPORTED`.
 *           We can confirm this server supports cookies based on the existence
 *           of this record.
 *         - If a new EDNS OPT cookie (10) server cookie is in the response, and
 *           the `client.cookie` matches the `query.client_cookie` still (hasn't
 *           been rotated by some other parallel query), save it as
 *           `cookie.server`.
 *     3. If dns response `rcode` is `BAD_COOKIE`:
 *         - Ensure a EDNS OPT cookie (10) is returned, otherwise **drop
 *           response**, this is completely invalid and likely an spoof of some
 *           sort.
 *         - Otherwise
 *           - Increment `query.cookie_try_count`
 *           - If `query.cookie_try_count >= COOKIE_RESEND_MAX`, set
 *             `query.using_tcp` to force the next attempt to use TCP.
 *           - **Requeue the query**, but do not increment the normal
 *             `try_count` as a `BAD_COOKIE` reply isn't a normal try failure.
 *             This should end up going all the way back to step 1 on the next
 *             attempt.
 *     4. If EDNS OPT cookie (10) is **NOT** returned in the response:
 *         - If `cookie.state == SUPPORTED`
 *           - if `cookie.unsupported_ts` is zero, set to the current timestamp.
 *           - Drop the response, wait for a valid response to be returned
 *         - if `cookie.state == GENERATED`
 *           - clear all cookie settings
 *           - set `cookie.state = UNSUPPORTED`
 *           - set `cookie.unsupported_ts` to the current time
 *         - Accept response (state should be `UNSUPPORTED` if we're here)
 */

#include "ares_private.h"

/* 1 day */
#define COOKIE_CLIENT_TIMEOUT_MS (86400 * 1000)

/* 5 minutes */
#define COOKIE_UNSUPPORTED_TIMEOUT_MS (300 * 1000)

/* 2 minutes */
#define COOKIE_REGRESSION_TIMEOUT_MS (120 * 1000)

#define COOKIE_RESEND_MAX 3

static const unsigned char *
  ares_dns_cookie_fetch(const ares_dns_record_t *dnsrec, size_t *len)
{
  const ares_dns_rr_t *rr  = ares_dns_get_opt_rr_const(dnsrec);
  const unsigned char *val = NULL;
  *len                     = 0;

  if (rr == NULL) {
    return NULL;
  }

  if (!ares_dns_rr_get_opt_byid(rr, ARES_RR_OPT_OPTIONS, ARES_OPT_PARAM_COOKIE,
                                &val, len)) {
    return NULL;
  }

  return val;
}

static ares_bool_t timeval_is_set(const ares_timeval_t *tv)
{
  if (tv->sec != 0 && tv->usec != 0) {
    return ARES_TRUE;
  }
  return ARES_FALSE;
}

static ares_bool_t timeval_expired(const ares_timeval_t *tv,
                                   const ares_timeval_t *now,
                                   unsigned long         millsecs)
{
  ares_int64_t   tvdiff_ms;
  ares_timeval_t tvdiff;
  ares__timeval_diff(&tvdiff, tv, now);

  tvdiff_ms = tvdiff.sec * 1000 + tvdiff.usec / 1000;
  if (tvdiff_ms >= (ares_int64_t)millsecs) {
    return ARES_TRUE;
  }
  return ARES_FALSE;
}

static void ares_cookie_clear(ares_cookie_t *cookie)
{
  memset(cookie, 0, sizeof(*cookie));
  cookie->state = ARES_COOKIE_INITIAL;
}

static void ares_cookie_generate(ares_cookie_t *cookie, ares_conn_t *conn,
                                 const ares_timeval_t *now)
{
  ares_channel_t *channel = conn->server->channel;

  ares__rand_bytes(channel->rand_state, cookie->client, sizeof(cookie->client));
  memcpy(&cookie->client_ts, now, sizeof(cookie->client_ts));
  memcpy(&cookie->client_ip, &conn->self_ip, sizeof(cookie->client_ip));
}

static void ares_cookie_clear_server(ares_cookie_t *cookie)
{
  memset(cookie->server, 0, sizeof(cookie->server));
  cookie->server_len = 0;
}

static ares_bool_t ares_addr_equal(const struct ares_addr *addr1,
                                   const struct ares_addr *addr2)
{
  if (addr1->family != addr2->family) {
    return ARES_FALSE;
  }

  switch (addr1->family) {
    case AF_INET:
      if (memcmp(&addr1->addr.addr4, &addr2->addr.addr4,
                 sizeof(addr1->addr.addr4)) == 0) {
        return ARES_TRUE;
      }
      break;
    case AF_INET6:
      /* This structure is weird, and due to padding SonarCloud complains if
       * you don't punch all the way down.  At some point we should rework
       * this structure */
      if (memcmp(&addr1->addr.addr6._S6_un._S6_u8,
                 &addr2->addr.addr6._S6_un._S6_u8,
                 sizeof(addr1->addr.addr6._S6_un._S6_u8)) == 0) {
        return ARES_TRUE;
      }
      break;
    default:
      break; /* LCOV_EXCL_LINE */
  }

  return ARES_FALSE;
}

ares_status_t ares_cookie_apply(ares_dns_record_t *dnsrec, ares_conn_t *conn,
                                const ares_timeval_t *now)
{
  ares_server_t *server = conn->server;
  ares_cookie_t *cookie = &server->cookie;
  ares_dns_rr_t *rr     = ares_dns_get_opt_rr(dnsrec);
  unsigned char  c[40];
  size_t         c_len;

  /* If there is no OPT record, then EDNS isn't supported, and therefore
   * cookies can't be supported */
  if (rr == NULL) {
    return ARES_SUCCESS;
  }

  /* No cookies on TCP, make sure we remove one if one is present */
  if (conn->flags & ARES_CONN_FLAG_TCP) {
    ares_dns_rr_del_opt_byid(rr, ARES_RR_OPT_OPTIONS, ARES_OPT_PARAM_COOKIE);
    return ARES_SUCCESS;
  }

  /* Look for regression */
  if (cookie->state == ARES_COOKIE_SUPPORTED &&
      timeval_is_set(&cookie->unsupported_ts) &&
      timeval_expired(&cookie->unsupported_ts, now,
                      COOKIE_REGRESSION_TIMEOUT_MS)) {
    ares_cookie_clear(cookie);
  }

  /* Handle unsupported state */
  if (cookie->state == ARES_COOKIE_UNSUPPORTED) {
    /* If timer hasn't expired, just delete any possible cookie and return */
    if (!timeval_expired(&cookie->unsupported_ts, now,
                         COOKIE_REGRESSION_TIMEOUT_MS)) {
      ares_dns_rr_del_opt_byid(rr, ARES_RR_OPT_OPTIONS, ARES_OPT_PARAM_COOKIE);
      return ARES_SUCCESS;
    }

    /* We want to try to "learn" again */
    ares_cookie_clear(cookie);
  }

  /* Generate a new cookie */
  if (cookie->state == ARES_COOKIE_INITIAL) {
    ares_cookie_generate(cookie, conn, now);
    cookie->state = ARES_COOKIE_GENERATED;
  }

  /* Regenerate the cookie and clear the server cookie if the client ip has
   * changed */
  if ((cookie->state == ARES_COOKIE_GENERATED ||
       cookie->state == ARES_COOKIE_SUPPORTED) &&
      !ares_addr_equal(&conn->self_ip, &cookie->client_ip)) {
    ares_cookie_clear_server(cookie);
    ares_cookie_generate(cookie, conn, now);
  }

  /* If the client cookie has reached its maximum time, refresh it */
  if (cookie->state == ARES_COOKIE_SUPPORTED &&
      timeval_expired(&cookie->client_ts, now, COOKIE_CLIENT_TIMEOUT_MS)) {
    ares_cookie_clear_server(cookie);
    ares_cookie_generate(cookie, conn, now);
  }

  /* Generate the full cookie which is the client cookie concatenated with the
   * server cookie (if there is one) and apply it. */
  memcpy(c, cookie->client, sizeof(cookie->client));
  if (cookie->server_len) {
    memcpy(c + sizeof(cookie->client), cookie->server, cookie->server_len);
  }
  c_len = sizeof(cookie->client) + cookie->server_len;

  return ares_dns_rr_set_opt(rr, ARES_RR_OPT_OPTIONS, ARES_OPT_PARAM_COOKIE, c,
                             c_len);
}

ares_status_t ares_cookie_validate(ares_query_t            *query,
                                   const ares_dns_record_t *dnsresp,
                                   ares_conn_t *conn, const ares_timeval_t *now)
{
  ares_server_t           *server = conn->server;
  ares_cookie_t           *cookie = &server->cookie;
  const ares_dns_record_t *dnsreq = query->query;
  const unsigned char     *resp_cookie;
  size_t                   resp_cookie_len;
  const unsigned char     *req_cookie;
  size_t                   req_cookie_len;

  resp_cookie = ares_dns_cookie_fetch(dnsresp, &resp_cookie_len);

  /* Invalid cookie length, drop */
  if (resp_cookie && (resp_cookie_len < 8 || resp_cookie_len > 40)) {
    return ARES_EBADRESP;
  }

  req_cookie = ares_dns_cookie_fetch(dnsreq, &req_cookie_len);

  /* Didn't request cookies, so we can stop evaluating */
  if (req_cookie == NULL) {
    return ARES_SUCCESS;
  }

  /* If 8-byte prefix for returned cookie doesn't match the requested cookie,
   * drop for spoofing */
  if (resp_cookie && memcmp(req_cookie, resp_cookie, 8) != 0) {
    return ARES_EBADRESP;
  }

  if (resp_cookie && resp_cookie_len > 8) {
    /* Make sure we record that we successfully received a cookie response */
    cookie->state = ARES_COOKIE_SUPPORTED;
    memset(&cookie->unsupported_ts, 0, sizeof(cookie->unsupported_ts));

    /* If client cookie hasn't been rotated, save the returned server cookie */
    if (memcmp(cookie->client, req_cookie, sizeof(cookie->client)) == 0) {
      cookie->server_len = resp_cookie_len - 8;
      memcpy(cookie->server, resp_cookie + 8, cookie->server_len);
    }
  }

  if (ares_dns_record_get_rcode(dnsresp) == ARES_RCODE_BADCOOKIE) {
    /* Illegal to return BADCOOKIE but no cookie, drop */
    if (resp_cookie == NULL) {
      return ARES_EBADRESP;
    }

    /* If we have too many attempts to send a cookie, we need to requeue as
     * tcp */
    query->cookie_try_count++;
    if (query->cookie_try_count >= COOKIE_RESEND_MAX) {
      query->using_tcp = ARES_TRUE;
    }

    /* Resend the request, hopefully it will work the next time as we should
     * have recorded a server cookie */
    ares__requeue_query(query, now, ARES_SUCCESS,
                        ARES_FALSE /* Don't increment try count */);

    /* Parent needs to drop this response */
    return ARES_EBADRESP;
  }

  /* We've got a response with a server cookie, and we've done all the
   * evaluation we can, return success */
  if (resp_cookie_len > 8) {
    return ARES_SUCCESS;
  }

  if (cookie->state == ARES_COOKIE_SUPPORTED) {
    /* If we're not currently tracking an error time yet, start */
    if (!timeval_is_set(&cookie->unsupported_ts)) {
      memcpy(&cookie->unsupported_ts, now, sizeof(cookie->unsupported_ts));
    }
    /* Drop it since we expected a cookie */
    return ARES_EBADRESP;
  }

  if (cookie->state == ARES_COOKIE_GENERATED) {
    ares_cookie_clear(cookie);
    cookie->state = ARES_COOKIE_UNSUPPORTED;
    memcpy(&cookie->unsupported_ts, now, sizeof(cookie->unsupported_ts));
  }

  /* Cookie state should be UNSUPPORTED if we're here */
  return ARES_SUCCESS;
}
