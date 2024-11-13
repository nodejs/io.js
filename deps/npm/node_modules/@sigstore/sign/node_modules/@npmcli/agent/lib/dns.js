'use strict'

const { LRUCache } = require('lru-cache')
const dns = require('dns')

// this is a factory so that each request can have its own opts (i.e. ttl)
// while still sharing the cache across all requests
const cache = new LRUCache({ max: 50 })

const getOptions = ({
  family = 0,
  hints = dns.ADDRCONFIG,
  all = false,
  verbatim = undefined,
  ttl = 5 * 60 * 1000,
  lookup = dns.lookup,
}) => ({
  // hints and lookup are returned since both are top level properties to (net|tls).connect
  hints,
  lookup: (hostname, ...args) => {
    const callback = args.pop() // callback is always last arg
    const lookupOptions = args[0] ?? {}

    const options = {
      family,
      hints,
      all,
      verbatim,
      ...(typeof lookupOptions === 'number' ? { family: lookupOptions } : lookupOptions),
    }

    const key = JSON.stringify({ hostname, ...options })

    if (cache.has(key)) {
      const cached = cache.get(key)
      return process.nextTick(callback, null, ...cached)
    }

    lookup(hostname, options, (err, ...result) => {
      if (err) {
        return callback(err)
      }

      cache.set(key, result, { ttl })
      return callback(null, ...result)
    })
  },
})

module.exports = {
  cache,
  getOptions,
}
