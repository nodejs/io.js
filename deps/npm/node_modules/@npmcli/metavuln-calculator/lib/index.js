// this is the public class that is used by consumers.
// the Advisory class handles all the calculation, and this
// class handles all the IO with the registry and cache.
const pacote = require('pacote')
const cacache = require('cacache')
const { time } = require('proc-log')
const Advisory = require('./advisory.js')
const { homedir } = require('os')
const jsonParse = require('json-parse-even-better-errors')

const _packument = Symbol('packument')
const _cachePut = Symbol('cachePut')
const _cacheGet = Symbol('cacheGet')
const _cacheData = Symbol('cacheData')
const _packuments = Symbol('packuments')
const _cache = Symbol('cache')
const _options = Symbol('options')
const _advisories = Symbol('advisories')
const _calculate = Symbol('calculate')

class Calculator {
  constructor (options = {}) {
    this[_options] = { ...options }
    this[_cache] = this[_options].cache || (homedir() + '/.npm/_cacache')
    this[_options].cache = this[_cache]
    this[_packuments] = new Map()
    this[_cacheData] = new Map()
    this[_advisories] = new Map()
  }

  get cache () {
    return this[_cache]
  }

  get options () {
    return { ...this[_options] }
  }

  async calculate (name, source) {
    const k = `security-advisory:${name}:${source.id}`
    if (this[_advisories].has(k)) {
      return this[_advisories].get(k)
    }

    const p = this[_calculate](name, source)
    this[_advisories].set(k, p)
    return p
  }

  async [_calculate] (name, source) {
    const k = `security-advisory:${name}:${source.id}`
    const timeEnd = time.start(`metavuln:calculate:${k}`)
    const advisory = new Advisory(name, source, this[_options])
    // load packument and cached advisory
    const [cached, packument] = await Promise.all([
      this[_cacheGet](advisory),
      this[_packument](name),
    ])
    const timeEndLoad = time.start(`metavuln:load:${k}`)
    advisory.load(cached, packument)
    timeEndLoad()
    if (advisory.updated) {
      await this[_cachePut](advisory)
    }
    this[_advisories].set(k, advisory)
    timeEnd()
    return advisory
  }

  async [_cachePut] (advisory) {
    const { name, id } = advisory
    const key = `security-advisory:${name}:${id}`
    const timeEnd = time.start(`metavuln:cache:put:${key}`)
    const data = JSON.stringify(advisory)
    const options = { ...this[_options] }
    this[_cacheData].set(key, jsonParse(data))
    await cacache.put(this[_cache], key, data, options).catch(() => {})
    timeEnd()
  }

  async [_cacheGet] (advisory) {
    const { name, id } = advisory
    const key = `security-advisory:${name}:${id}`
    /* istanbul ignore if - should be impossible, since we memoize the
     * advisory object itself using the same key, just being cautious */
    if (this[_cacheData].has(key)) {
      return this[_cacheData].get(key)
    }

    const timeEnd = time.start(`metavuln:cache:get:${key}`)
    const p = cacache.get(this[_cache], key, { ...this[_options] })
      .catch(() => ({ data: '{}' }))
      .then(({ data }) => {
        data = jsonParse(data)
        timeEnd()
        this[_cacheData].set(key, data)
        return data
      })
    this[_cacheData].set(key, p)
    return p
  }

  async [_packument] (name) {
    if (this[_packuments].has(name)) {
      return this[_packuments].get(name)
    }

    const timeEnd = time.start(`metavuln:packument:${name}`)
    const p = pacote.packument(name, { ...this[_options] })
      .catch(() => {
        // presumably not something from the registry.
        // an empty packument will have an effective range of *
        return {
          name,
          versions: {},
        }
      })
      .then(paku => {
        timeEnd()
        this[_packuments].set(name, paku)
        return paku
      })
    this[_packuments].set(name, p)
    return p
  }
}

module.exports = Calculator
