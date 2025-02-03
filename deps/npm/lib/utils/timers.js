const EE = require('node:events')
const fs = require('node:fs')
const { log, time } = require('proc-log')

const INITIAL_TIMER = 'npm'

class Timers extends EE {
  #file
  #timing

  #unfinished = new Map()
  #finished = {}

  constructor () {
    super()
    this.on()
    time.start(INITIAL_TIMER)
    this.started = this.#unfinished.get(INITIAL_TIMER)
  }

  on () {
    process.on('time', this.#timeHandler)
  }

  off () {
    process.off('time', this.#timeHandler)
  }

  load ({ path, timing } = {}) {
    this.#timing = timing
    this.#file = `${path}timing.json`
  }

  finish (metadata) {
    time.end(INITIAL_TIMER)

    for (const [name, timer] of this.#unfinished) {
      log.silly('unfinished npm timer', name, timer)
    }

    if (!this.#timing) {
      // Not in timing mode, nothing else to do here
      return
    }

    try {
      this.#writeFile(metadata)
      log.info('timing', `Timing info written to: ${this.#file}`)
    } catch (e) {
      log.warn('timing', `could not write timing file: ${e}`)
    }
  }

  #writeFile (metadata) {
    const globalStart = this.started
    const globalEnd = this.#finished[INITIAL_TIMER]
    const content = {
      metadata,
      timers: this.#finished,
      // add any unfinished timers with their relative start/end
      unfinishedTimers: [...this.#unfinished.entries()].reduce((acc, [name, start]) => {
        acc[name] = [start - globalStart, globalEnd - globalStart]
        return acc
      }, {}),
    }
    fs.writeFileSync(this.#file, JSON.stringify(content) + '\n')
  }

  #timeHandler = (level, name) => {
    const now = Date.now()
    switch (level) {
      case time.KEYS.start:
        this.#unfinished.set(name, now)
        break
      case time.KEYS.end: {
        if (this.#unfinished.has(name)) {
          const ms = now - this.#unfinished.get(name)
          this.#finished[name] = ms
          this.#unfinished.delete(name)
          log.timing(name, `Completed in ${ms}ms`)
        } else {
          log.silly('timing', `Tried to end timer that doesn't exist: ${name}`)
        }
      }
    }
  }
}

module.exports = Timers
