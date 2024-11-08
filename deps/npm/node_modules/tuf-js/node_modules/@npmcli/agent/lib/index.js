'use strict'

const { LRUCache } = require('lru-cache')
const { normalizeOptions, cacheOptions } = require('./options')
const { getProxy, proxyCache } = require('./proxy.js')
const dns = require('./dns.js')
const Agent = require('./agents.js')

const agentCache = new LRUCache({ max: 20 })

const getAgent = (url, { agent, proxy, noProxy, ...options } = {}) => {
  // false has meaning so this can't be a simple truthiness check
  if (agent != null) {
    return agent
  }

  url = new URL(url)

  const proxyForUrl = getProxy(url, { proxy, noProxy })
  const normalizedOptions = {
    ...normalizeOptions(options),
    proxy: proxyForUrl,
  }

  const cacheKey = cacheOptions({
    ...normalizedOptions,
    secureEndpoint: url.protocol === 'https:',
  })

  if (agentCache.has(cacheKey)) {
    return agentCache.get(cacheKey)
  }

  const newAgent = new Agent(normalizedOptions)
  agentCache.set(cacheKey, newAgent)

  return newAgent
}

module.exports = {
  getAgent,
  Agent,
  // these are exported for backwards compatability
  HttpAgent: Agent,
  HttpsAgent: Agent,
  cache: {
    proxy: proxyCache,
    agent: agentCache,
    dns: dns.cache,
    clear: () => {
      proxyCache.clear()
      agentCache.clear()
      dns.cache.clear()
    },
  },
}
