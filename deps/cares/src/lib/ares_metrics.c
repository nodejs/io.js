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


/* IMPLEMENTATION NOTES
 * ====================
 *
 * With very little effort we should be able to determine fairly proper timeouts
 * we can use based on prior query history.  We track in order to be able to
 * auto-scale when network conditions change (e.g. maybe there is a provider
 * failover and timings change due to that).  Apple appears to do this within
 * their system resolver in MacOS.  Obviously we should have a minimum, maximum,
 * and initial value to make sure the algorithm doesn't somehow go off the
 * rails.
 *
 * Values:
 * - Minimum Timeout: 250ms (approximate RTT half-way around the globe)
 * - Maximum Timeout: 5000ms (Recommended timeout in RFC 1123), can be reduced
 *   by ARES_OPT_MAXTIMEOUTMS, but otherwise the bound specified by the option
 *   caps the retry timeout.
 * - Initial Timeout: User-specified via configuration or ARES_OPT_TIMEOUTMS
 * - Average latency multiplier: 5x (a local DNS server returning a cached value
 *   will be quicker than if it needs to recurse so we need to account for this)
 * - Minimum Count for Average: 3.  This is the minimum number of queries we
 *   need to form an average for the bucket.
 *
 * Per-server buckets for tracking latency over time (these are ephemeral
 * meaning they don't persist once a channel is destroyed).  We record both the
 * current timespan for the bucket and the immediate preceding timespan in case
 * of roll-overs we can still maintain recent metrics for calculations:
 * - 1 minute
 * - 15 minutes
 * - 1 hr
 * - 1 day
 * - since inception
 *
 * Each bucket would contain:
 * - timestamp (divided by interval)
 * - minimum latency
 * - maximum latency
 * - total time
 * - count
 * NOTE: average latency is (total time / count), we will calculate this
 *       dynamically when needed
 *
 * Basic algorithm for calculating timeout to use would be:
 * - Scan from most recent bucket to least recent
 * - Check timestamp of bucket, if doesn't match current time, continue to next
 *   bucket
 * - Check count of bucket, if its not at least the "Minimum Count for Average",
 *   check the previous bucket, otherwise continue to next bucket
 * - If we reached the end with no bucket match, use "Initial Timeout"
 * - If bucket is selected, take ("total time" / count) as Average latency,
 *   multiply by "Average Latency Multiplier", bound by "Minimum Timeout" and
 *   "Maximum Timeout"
 * NOTE: The timeout calculated may not be the timeout used.  If we are retrying
 * the query on the same server another time, then it will use a larger value
 *
 * On each query reply where the response is legitimate (proper response or
 * NXDOMAIN) and not something like a server error:
 * - Cycle through each bucket in order
 * - Check timestamp of bucket against current timestamp, if out of date
 *   overwrite previous entry with values, clear current values
 * - Compare current minimum and maximum recorded latency against query time and
 *   adjust if necessary
 * - Increment "count" by 1 and "total time" by the query time
 *
 * Other Notes:
 * - This is always-on, the only user-configurable value is the initial
 *   timeout which will simply re-uses the current option.
 * - Minimum and Maximum latencies for a bucket are currently unused but are
 *   there in case we find a need for them in the future.
 */

#include "ares_private.h"

/*! Minimum timeout value. Chosen due to it being approximately RTT half-way
 *  around the world */
#define MIN_TIMEOUT_MS 250

/*! Multiplier to apply to average latency to come up with an initial timeout */
#define AVG_TIMEOUT_MULTIPLIER 5

/*! Upper timeout bounds, only used if channel->maxtimeout not set */
#define MAX_TIMEOUT_MS 5000

/*! Minimum queries required to form an average */
#define MIN_COUNT_FOR_AVERAGE 3

static time_t ares_metric_timestamp(ares_server_bucket_t  bucket,
                                    const ares_timeval_t *now,
                                    ares_bool_t           is_previous)
{
  time_t divisor = 1; /* Silence bogus MSVC warning by setting default value */

  switch (bucket) {
    case ARES_METRIC_1MINUTE:
      divisor = 60;
      break;
    case ARES_METRIC_15MINUTES:
      divisor = 15 * 60;
      break;
    case ARES_METRIC_1HOUR:
      divisor = 60 * 60;
      break;
    case ARES_METRIC_1DAY:
      divisor = 24 * 60 * 60;
      break;
    case ARES_METRIC_INCEPTION:
      return is_previous ? 0 : 1;
    case ARES_METRIC_COUNT:
      return 0; /* Invalid! */
  }

  if (is_previous) {
    if (divisor >= now->sec) {
      return 0;
    }
    return (time_t)((now->sec - divisor) / divisor);
  }

  return (time_t)(now->sec / divisor);
}

void ares_metrics_record(const ares_query_t *query, ares_server_t *server,
                         ares_status_t status, const ares_dns_record_t *dnsrec)
{
  ares_timeval_t       now;
  ares_timeval_t       tvdiff;
  unsigned int         query_ms;
  ares_dns_rcode_t     rcode;
  ares_server_bucket_t i;

  if (status != ARES_SUCCESS) {
    return;
  }

  if (server == NULL) {
    return;
  }

  ares__tvnow(&now);

  rcode = ares_dns_record_get_rcode(dnsrec);
  if (rcode != ARES_RCODE_NOERROR && rcode != ARES_RCODE_NXDOMAIN) {
    return;
  }

  ares__timeval_diff(&tvdiff, &query->ts, &now);
  query_ms = (unsigned int)(tvdiff.sec + (tvdiff.usec / 1000));
  if (query_ms == 0) {
    query_ms = 1;
  }

  /* Place in each bucket */
  for (i = 0; i < ARES_METRIC_COUNT; i++) {
    time_t ts = ares_metric_timestamp(i, &now, ARES_FALSE);

    /* Copy metrics to prev and clear */
    if (ts != server->metrics[i].ts) {
      server->metrics[i].prev_ts          = server->metrics[i].ts;
      server->metrics[i].prev_total_ms    = server->metrics[i].total_ms;
      server->metrics[i].prev_total_count = server->metrics[i].total_count;
      server->metrics[i].ts               = ts;
      server->metrics[i].latency_min_ms   = 0;
      server->metrics[i].latency_max_ms   = 0;
      server->metrics[i].total_ms         = 0;
      server->metrics[i].total_count      = 0;
    }

    if (server->metrics[i].latency_min_ms == 0 ||
        server->metrics[i].latency_min_ms > query_ms) {
      server->metrics[i].latency_min_ms = query_ms;
    }

    if (query_ms > server->metrics[i].latency_max_ms) {
      server->metrics[i].latency_min_ms = query_ms;
    }

    server->metrics[i].total_count++;
    server->metrics[i].total_ms += (ares_uint64_t)query_ms;
  }
}

size_t ares_metrics_server_timeout(const ares_server_t  *server,
                                   const ares_timeval_t *now)
{
  const ares_channel_t *channel = server->channel;
  ares_server_bucket_t  i;
  size_t                timeout_ms = 0;
  size_t                max_timeout_ms;

  for (i = 0; i < ARES_METRIC_COUNT; i++) {
    time_t ts = ares_metric_timestamp(i, now, ARES_FALSE);

    /* This ts has been invalidated, see if we should use the previous
     * time period */
    if (ts != server->metrics[i].ts ||
        server->metrics[i].total_count < MIN_COUNT_FOR_AVERAGE) {
      time_t prev_ts = ares_metric_timestamp(i, now, ARES_TRUE);
      if (prev_ts != server->metrics[i].prev_ts ||
          server->metrics[i].prev_total_count < MIN_COUNT_FOR_AVERAGE) {
        /* Move onto next bucket */
        continue;
      }
      /* Calculate average time for previous bucket */
      timeout_ms = (size_t)(server->metrics[i].prev_total_ms /
                            server->metrics[i].prev_total_count);
    } else {
      /* Calculate average time for current bucket*/
      timeout_ms =
        (size_t)(server->metrics[i].total_ms / server->metrics[i].total_count);
    }

    /* Multiply average by constant to get timeout value */
    timeout_ms *= AVG_TIMEOUT_MULTIPLIER;
    break;
  }

  /* If we're here, that means its the first query for the server, so we just
   * use the initial default timeout */
  if (timeout_ms == 0) {
    timeout_ms = channel->timeout;
  }

  /* don't go below lower bounds */
  if (timeout_ms < MIN_TIMEOUT_MS) {
    timeout_ms = MIN_TIMEOUT_MS;
  }

  /* don't go above upper bounds */
  max_timeout_ms = channel->maxtimeout ? channel->maxtimeout : MAX_TIMEOUT_MS;
  if (timeout_ms > max_timeout_ms) {
    timeout_ms = max_timeout_ms;
  }

  return timeout_ms;
}
