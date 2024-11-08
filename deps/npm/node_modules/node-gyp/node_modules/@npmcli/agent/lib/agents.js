'use strict'

const net = require('net')
const tls = require('tls')
const { once } = require('events')
const timers = require('timers/promises')
const { normalizeOptions, cacheOptions } = require('./options')
const { getProxy, getProxyAgent, proxyCache } = require('./proxy.js')
const Errors = require('./errors.js')
const { Agent: AgentBase } = require('agent-base')

module.exports = class Agent extends AgentBase {
  #options
  #timeouts
  #proxy
  #noProxy
  #ProxyAgent

  constructor (options = {}) {
    const { timeouts, proxy, noProxy, ...normalizedOptions } = normalizeOptions(options)

    super(normalizedOptions)

    this.#options = normalizedOptions
    this.#timeouts = timeouts

    if (proxy) {
      this.#proxy = new URL(proxy)
      this.#noProxy = noProxy
      this.#ProxyAgent = getProxyAgent(proxy)
    }
  }

  get proxy () {
    return this.#proxy ? { url: this.#proxy } : {}
  }

  #getProxy (options) {
    if (!this.#proxy) {
      return
    }

    const proxy = getProxy(`${options.protocol}//${options.host}:${options.port}`, {
      proxy: this.#proxy,
      noProxy: this.#noProxy,
    })

    if (!proxy) {
      return
    }

    const cacheKey = cacheOptions({
      ...options,
      ...this.#options,
      timeouts: this.#timeouts,
      proxy,
    })

    if (proxyCache.has(cacheKey)) {
      return proxyCache.get(cacheKey)
    }

    let ProxyAgent = this.#ProxyAgent
    if (Array.isArray(ProxyAgent)) {
      ProxyAgent = this.isSecureEndpoint(options) ? ProxyAgent[1] : ProxyAgent[0]
    }

    const proxyAgent = new ProxyAgent(proxy, {
      ...this.#options,
      socketOptions: { family: this.#options.family },
    })
    proxyCache.set(cacheKey, proxyAgent)

    return proxyAgent
  }

  // takes an array of promises and races them against the connection timeout
  // which will throw the necessary error if it is hit. This will return the
  // result of the promise race.
  async #timeoutConnection ({ promises, options, timeout }, ac = new AbortController()) {
    if (timeout) {
      const connectionTimeout = timers.setTimeout(timeout, null, { signal: ac.signal })
        .then(() => {
          throw new Errors.ConnectionTimeoutError(`${options.host}:${options.port}`)
        }).catch((err) => {
          if (err.name === 'AbortError') {
            return
          }
          throw err
        })
      promises.push(connectionTimeout)
    }

    let result
    try {
      result = await Promise.race(promises)
      ac.abort()
    } catch (err) {
      ac.abort()
      throw err
    }
    return result
  }

  async connect (request, options) {
    // if the connection does not have its own lookup function
    // set, then use the one from our options
    options.lookup ??= this.#options.lookup

    let socket
    let timeout = this.#timeouts.connection
    const isSecureEndpoint = this.isSecureEndpoint(options)

    const proxy = this.#getProxy(options)
    if (proxy) {
      // some of the proxies will wait for the socket to fully connect before
      // returning so we have to await this while also racing it against the
      // connection timeout.
      const start = Date.now()
      socket = await this.#timeoutConnection({
        options,
        timeout,
        promises: [proxy.connect(request, options)],
      })
      // see how much time proxy.connect took and subtract it from
      // the timeout
      if (timeout) {
        timeout = timeout - (Date.now() - start)
      }
    } else {
      socket = (isSecureEndpoint ? tls : net).connect(options)
    }

    socket.setKeepAlive(this.keepAlive, this.keepAliveMsecs)
    socket.setNoDelay(this.keepAlive)

    const abortController = new AbortController()
    const { signal } = abortController

    const connectPromise = socket[isSecureEndpoint ? 'secureConnecting' : 'connecting']
      ? once(socket, isSecureEndpoint ? 'secureConnect' : 'connect', { signal })
      : Promise.resolve()

    await this.#timeoutConnection({
      options,
      timeout,
      promises: [
        connectPromise,
        once(socket, 'error', { signal }).then((err) => {
          throw err[0]
        }),
      ],
    }, abortController)

    if (this.#timeouts.idle) {
      socket.setTimeout(this.#timeouts.idle, () => {
        socket.destroy(new Errors.IdleTimeoutError(`${options.host}:${options.port}`))
      })
    }

    return socket
  }

  addRequest (request, options) {
    const proxy = this.#getProxy(options)
    // it would be better to call proxy.addRequest here but this causes the
    // http-proxy-agent to call its super.addRequest which causes the request
    // to be added to the agent twice. since we only support 3 agents
    // currently (see the required agents in proxy.js) we have manually
    // checked that the only public methods we need to call are called in the
    // next block. this could change in the future and presumably we would get
    // failing tests until we have properly called the necessary methods on
    // each of our proxy agents
    if (proxy?.setRequestProps) {
      proxy.setRequestProps(request, options)
    }

    request.setHeader('connection', this.keepAlive ? 'keep-alive' : 'close')

    if (this.#timeouts.response) {
      let responseTimeout
      request.once('finish', () => {
        setTimeout(() => {
          request.destroy(new Errors.ResponseTimeoutError(request, this.#proxy))
        }, this.#timeouts.response)
      })
      request.once('response', () => {
        clearTimeout(responseTimeout)
      })
    }

    if (this.#timeouts.transfer) {
      let transferTimeout
      request.once('response', (res) => {
        setTimeout(() => {
          res.destroy(new Errors.TransferTimeoutError(request, this.#proxy))
        }, this.#timeouts.transfer)
        res.once('close', () => {
          clearTimeout(transferTimeout)
        })
      })
    }

    return super.addRequest(request, options)
  }
}
