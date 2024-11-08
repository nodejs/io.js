'use strict'

const MinipassPipeline = require('minipass-pipeline')

class CachingMinipassPipeline extends MinipassPipeline {
  #events = []
  #data = new Map()

  constructor (opts, ...streams) {
    // CRITICAL: do NOT pass the streams to the call to super(), this will start
    // the flow of data and potentially cause the events we need to catch to emit
    // before we've finished our own setup. instead we call super() with no args,
    // finish our setup, and then push the streams into ourselves to start the
    // data flow
    super()
    this.#events = opts.events

    /* istanbul ignore next - coverage disabled because this is pointless to test here */
    if (streams.length) {
      this.push(...streams)
    }
  }

  on (event, handler) {
    if (this.#events.includes(event) && this.#data.has(event)) {
      return handler(...this.#data.get(event))
    }

    return super.on(event, handler)
  }

  emit (event, ...data) {
    if (this.#events.includes(event)) {
      this.#data.set(event, data)
    }

    return super.emit(event, ...data)
  }
}

module.exports = CachingMinipassPipeline
