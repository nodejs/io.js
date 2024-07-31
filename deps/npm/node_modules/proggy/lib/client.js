const EE = require('events')
const onProgress = Symbol('onProgress')
const bars = Symbol('bars')
const listener = Symbol('listener')
const normData = Symbol('normData')
class Client extends EE {
  constructor ({ normalize = false, stopOnDone = false } = {}) {
    super()
    this.normalize = !!normalize
    this.stopOnDone = !!stopOnDone
    this[bars] = new Map()
    this[listener] = null
  }

  get size () {
    return this[bars].size
  }

  get listening () {
    return !!this[listener]
  }

  addListener (...args) {
    return this.on(...args)
  }

  on (ev, ...args) {
    if (ev === 'progress' && !this[listener]) {
      this.start()
    }
    return super.on(ev, ...args)
  }

  off (ev, ...args) {
    return this.removeListener(ev, ...args)
  }

  removeListener (ev, ...args) {
    const ret = super.removeListener(ev, ...args)
    if (ev === 'progress' && this.listeners(ev).length === 0) {
      this.stop()
    }
    return ret
  }

  stop () {
    if (this[listener]) {
      process.removeListener('progress', this[listener])
      this[listener] = null
    }
  }

  start () {
    if (!this[listener]) {
      this[listener] = (...args) => this[onProgress](...args)
      process.on('progress', this[listener])
    }
  }

  [onProgress] (key, data) {
    data = this[normData](key, data)
    if (!this[bars].has(key)) {
      this.emit('bar', key, data)
    }
    this[bars].set(key, data)
    this.emit('progress', key, data)
    if (data.done) {
      this[bars].delete(key)
      this.emit('barDone', key, data)
      if (this.size === 0) {
        if (this.stopOnDone) {
          this.stop()
        }
        this.emit('done')
      }
    }
  }

  [normData] (key, data) {
    const actualValue = data.value
    const actualTotal = data.total
    let value = actualValue
    let total = actualTotal
    const done = data.done || value >= total
    if (this.normalize) {
      const bar = this[bars].get(key)
      total = 100
      if (done) {
        value = 100
      } else {
        // show value as a portion of 100
        const pct = 100 * actualValue / actualTotal
        if (bar) {
          // don't ever go backwards, and don't stand still
          // move at least 1% of the remaining value if it wouldn't move.
          value = (pct > bar.value) ? pct
            : (100 - bar.value) / 100 + bar.value
        }
      }
    }
    // include the key
    return {
      ...data,
      key,
      name: data.name || key,
      value,
      total,
      actualValue,
      actualTotal,
      done,
    }
  }
}
module.exports = Client
