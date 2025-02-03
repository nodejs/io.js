// The tracker class is intentionally as naive as possible.  it is just
// an ergonomic wrapper around process.emit('progress', ...)
const EE = require('events')
class Tracker extends EE {
  constructor (name, key, total) {
    super()
    if (!name) {
      throw new Error('proggy: Tracker needs a name')
    }

    if (typeof key === 'number' && !total) {
      total = key
      key = null
    }

    if (!total) {
      total = 100
    }

    if (!key) {
      key = name
    }

    this.done = false
    this.name = name
    this.key = key
    this.value = 0
    this.total = total
  }

  finish (metadata = {}) {
    this.update(this.total, this.total, metadata)
  }

  update (value, total, metadata) {
    if (!metadata) {
      if (total && typeof total === 'object') {
        metadata = total
      } else {
        metadata = {}
      }
    }
    if (typeof total !== 'number') {
      total = this.total
    }

    if (this.done) {
      const msg = `proggy: updating completed tracker: ${JSON.stringify(this.key)}`
      throw new Error(msg)
    }
    this.value = value
    this.total = total
    const done = this.value >= this.total
    process.emit('progress', this.key, {
      ...metadata,
      name: this.name,
      key: this.key,
      value,
      total,
      done,
    })
    if (done) {
      this.done = true
      this.emit('done')
    }
  }
}
module.exports = Tracker
