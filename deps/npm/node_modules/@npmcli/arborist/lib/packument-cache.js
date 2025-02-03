const { LRUCache } = require('lru-cache')
const { getHeapStatistics } = require('node:v8')
const { log } = require('proc-log')

// This is an in-memory cache that Pacote uses for packuments.
// Packuments are usually cached on disk.  This allows for rapid re-requests
// of the same packument to bypass disk reads.  The tradeoff here is memory
// usage for disk reads.
class PackumentCache extends LRUCache {
  static #heapLimit = Math.floor(getHeapStatistics().heap_size_limit)

  #sizeKey
  #disposed = new Set()

  #log (...args) {
    log.silly('packumentCache', ...args)
  }

  constructor ({
    // How much of this.#heapLimit to take up
    heapFactor = 0.25,
    // How much of this.#maxSize we allow any one packument to take up
    // Anything over this is not cached
    maxEntryFactor = 0.5,
    sizeKey = '_contentLength',
  } = {}) {
    const maxSize = Math.floor(PackumentCache.#heapLimit * heapFactor)
    const maxEntrySize = Math.floor(maxSize * maxEntryFactor)
    super({
      maxSize,
      maxEntrySize,
      sizeCalculation: (p) => {
        // Don't cache if we dont know the size
        // Some versions of pacote set this to `0`, newer versions set it to `null`
        if (!p[sizeKey]) {
          return maxEntrySize + 1
        }
        if (p[sizeKey] < 10_000) {
          return p[sizeKey] * 2
        }
        if (p[sizeKey] < 1_000_000) {
          return Math.floor(p[sizeKey] * 1.5)
        }
        // It is less beneficial to store a small amount of super large things
        // at the cost of all other packuments.
        return maxEntrySize + 1
      },
      dispose: (v, k) => {
        this.#disposed.add(k)
        this.#log(k, 'dispose')
      },
    })
    this.#sizeKey = sizeKey
    this.#log(`heap:${PackumentCache.#heapLimit} maxSize:${maxSize} maxEntrySize:${maxEntrySize}`)
  }

  set (k, v, ...args) {
    // we use disposed only for a logging signal if we are setting packuments that
    // have already been evicted from the cache previously. logging here could help
    // us tune this in the future.
    const disposed = this.#disposed.has(k)
    /* istanbul ignore next - this doesnt happen consistently so hard to test without resorting to unit tests  */
    if (disposed) {
      this.#disposed.delete(k)
    }
    this.#log(k, 'set', `size:${v[this.#sizeKey]} disposed:${disposed}`)
    return super.set(k, v, ...args)
  }

  has (k, ...args) {
    const has = super.has(k, ...args)
    this.#log(k, `cache-${has ? 'hit' : 'miss'}`)
    return has
  }
}

module.exports = PackumentCache
