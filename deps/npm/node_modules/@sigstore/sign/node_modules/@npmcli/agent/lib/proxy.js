'use strict'

const { HttpProxyAgent } = require('http-proxy-agent')
const { HttpsProxyAgent } = require('https-proxy-agent')
const { SocksProxyAgent } = require('socks-proxy-agent')
const { LRUCache } = require('lru-cache')
const { InvalidProxyProtocolError } = require('./errors.js')

const PROXY_CACHE = new LRUCache({ max: 20 })

const SOCKS_PROTOCOLS = new Set(SocksProxyAgent.protocols)

const PROXY_ENV_KEYS = new Set(['https_proxy', 'http_proxy', 'proxy', 'no_proxy'])

const PROXY_ENV = Object.entries(process.env).reduce((acc, [key, value]) => {
  key = key.toLowerCase()
  if (PROXY_ENV_KEYS.has(key)) {
    acc[key] = value
  }
  return acc
}, {})

const getProxyAgent = (url) => {
  url = new URL(url)

  const protocol = url.protocol.slice(0, -1)
  if (SOCKS_PROTOCOLS.has(protocol)) {
    return SocksProxyAgent
  }
  if (protocol === 'https' || protocol === 'http') {
    return [HttpProxyAgent, HttpsProxyAgent]
  }

  throw new InvalidProxyProtocolError(url)
}

const isNoProxy = (url, noProxy) => {
  if (typeof noProxy === 'string') {
    noProxy = noProxy.split(',').map((p) => p.trim()).filter(Boolean)
  }

  if (!noProxy || !noProxy.length) {
    return false
  }

  const hostSegments = url.hostname.split('.').reverse()

  return noProxy.some((no) => {
    const noSegments = no.split('.').filter(Boolean).reverse()
    if (!noSegments.length) {
      return false
    }

    for (let i = 0; i < noSegments.length; i++) {
      if (hostSegments[i] !== noSegments[i]) {
        return false
      }
    }

    return true
  })
}

const getProxy = (url, { proxy, noProxy }) => {
  url = new URL(url)

  if (!proxy) {
    proxy = url.protocol === 'https:'
      ? PROXY_ENV.https_proxy
      : PROXY_ENV.https_proxy || PROXY_ENV.http_proxy || PROXY_ENV.proxy
  }

  if (!noProxy) {
    noProxy = PROXY_ENV.no_proxy
  }

  if (!proxy || isNoProxy(url, noProxy)) {
    return null
  }

  return new URL(proxy)
}

module.exports = {
  getProxyAgent,
  getProxy,
  proxyCache: PROXY_CACHE,
}
