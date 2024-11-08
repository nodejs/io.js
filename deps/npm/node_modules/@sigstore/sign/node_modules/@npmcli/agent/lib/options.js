'use strict'

const dns = require('./dns')

const normalizeOptions = (opts) => {
  const family = parseInt(opts.family ?? '0', 10)
  const keepAlive = opts.keepAlive ?? true

  const normalized = {
    // nodejs http agent options. these are all the defaults
    // but kept here to increase the likelihood of cache hits
    // https://nodejs.org/api/http.html#new-agentoptions
    keepAliveMsecs: keepAlive ? 1000 : undefined,
    maxSockets: opts.maxSockets ?? 15,
    maxTotalSockets: Infinity,
    maxFreeSockets: keepAlive ? 256 : undefined,
    scheduling: 'fifo',
    // then spread the rest of the options
    ...opts,
    // we already set these to their defaults that we want
    family,
    keepAlive,
    // our custom timeout options
    timeouts: {
      // the standard timeout option is mapped to our idle timeout
      // and then deleted below
      idle: opts.timeout ?? 0,
      connection: 0,
      response: 0,
      transfer: 0,
      ...opts.timeouts,
    },
    // get the dns options that go at the top level of socket connection
    ...dns.getOptions({ family, ...opts.dns }),
  }

  // remove timeout since we already used it to set our own idle timeout
  delete normalized.timeout

  return normalized
}

const createKey = (obj) => {
  let key = ''
  const sorted = Object.entries(obj).sort((a, b) => a[0] - b[0])
  for (let [k, v] of sorted) {
    if (v == null) {
      v = 'null'
    } else if (v instanceof URL) {
      v = v.toString()
    } else if (typeof v === 'object') {
      v = createKey(v)
    }
    key += `${k}:${v}:`
  }
  return key
}

const cacheOptions = ({ secureEndpoint, ...options }) => createKey({
  secureEndpoint: !!secureEndpoint,
  // socket connect options
  family: options.family,
  hints: options.hints,
  localAddress: options.localAddress,
  // tls specific connect options
  strictSsl: secureEndpoint ? !!options.rejectUnauthorized : false,
  ca: secureEndpoint ? options.ca : null,
  cert: secureEndpoint ? options.cert : null,
  key: secureEndpoint ? options.key : null,
  // http agent options
  keepAlive: options.keepAlive,
  keepAliveMsecs: options.keepAliveMsecs,
  maxSockets: options.maxSockets,
  maxTotalSockets: options.maxTotalSockets,
  maxFreeSockets: options.maxFreeSockets,
  scheduling: options.scheduling,
  // timeout options
  timeouts: options.timeouts,
  // proxy
  proxy: options.proxy,
})

module.exports = {
  normalizeOptions,
  cacheOptions,
}
