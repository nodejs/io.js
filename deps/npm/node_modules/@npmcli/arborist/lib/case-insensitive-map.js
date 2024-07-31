// package children are represented with a Map object, but many file systems
// are case-insensitive and unicode-normalizing, so we need to treat
// node.children.get('FOO') and node.children.get('foo') as the same thing.

module.exports = class CIMap extends Map {
  #keys = new Map()

  constructor (items = []) {
    super()
    for (const [key, val] of items) {
      this.set(key, val)
    }
  }

  #normKey (key) {
    if (typeof key !== 'string') {
      return key
    }
    return key.normalize('NFKD').toLowerCase()
  }

  get (key) {
    const normKey = this.#normKey(key)
    return this.#keys.has(normKey) ? super.get(this.#keys.get(normKey))
      : undefined
  }

  set (key, val) {
    const normKey = this.#normKey(key)
    if (this.#keys.has(normKey)) {
      super.delete(this.#keys.get(normKey))
    }
    this.#keys.set(normKey, key)
    return super.set(key, val)
  }

  delete (key) {
    const normKey = this.#normKey(key)
    if (this.#keys.has(normKey)) {
      const prevKey = this.#keys.get(normKey)
      this.#keys.delete(normKey)
      return super.delete(prevKey)
    }
  }

  has (key) {
    const normKey = this.#normKey(key)
    return this.#keys.has(normKey) && super.has(this.#keys.get(normKey))
  }
}
