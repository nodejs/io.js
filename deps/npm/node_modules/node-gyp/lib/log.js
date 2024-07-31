'use strict'

const procLog = require('proc-log')
const { format } = require('util')

// helper to emit log messages with a predefined prefix
const logLevels = Object.keys(procLog).filter((k) => typeof procLog[k] === 'function')
const withPrefix = (prefix) => logLevels.reduce((acc, level) => {
  acc[level] = (...args) => procLog[level](prefix, ...args)
  return acc
}, {})

// very basic ansi color generator
const COLORS = {
  wrap: (str, colors) => {
    const codes = colors.filter(c => typeof c === 'number')
    return `\x1b[${codes.join(';')}m${str}\x1b[0m`
  },
  inverse: 7,
  fg: {
    black: 30,
    red: 31,
    green: 32,
    yellow: 33,
    blue: 34,
    magenta: 35,
    cyan: 36,
    white: 37
  },
  bg: {
    black: 40,
    red: 41,
    green: 42,
    yellow: 43,
    blue: 44,
    magenta: 45,
    cyan: 46,
    white: 47
  }
}

class Logger {
  #buffer = []
  #paused = null
  #level = null
  #stream = null

  // ordered from loudest to quietest
  #levels = [{
    id: 'silly',
    display: 'sill',
    style: { inverse: true }
  }, {
    id: 'verbose',
    display: 'verb',
    style: { fg: 'cyan', bg: 'black' }
  }, {
    id: 'info',
    style: { fg: 'green' }
  }, {
    id: 'http',
    style: { fg: 'green', bg: 'black' }
  }, {
    id: 'notice',
    style: { fg: 'cyan', bg: 'black' }
  }, {
    id: 'warn',
    display: 'WARN',
    style: { fg: 'black', bg: 'yellow' }
  }, {
    id: 'error',
    display: 'ERR!',
    style: { fg: 'red', bg: 'black' }
  }]

  constructor (stream) {
    process.on('log', (...args) => this.#onLog(...args))
    this.#levels = new Map(this.#levels.map((level, index) => [level.id, { ...level, index }]))
    this.level = 'info'
    this.stream = stream
    procLog.pause()
  }

  get stream () {
    return this.#stream
  }

  set stream (stream) {
    this.#stream = stream
  }

  get level () {
    return this.#levels.get(this.#level) ?? null
  }

  set level (level) {
    this.#level = this.#levels.get(level)?.id ?? null
  }

  isVisible (level) {
    return this.level?.index <= this.#levels.get(level)?.index ?? -1
  }

  #onLog (...args) {
    const [level] = args

    if (level === 'pause') {
      this.#paused = true
      return
    }

    if (level === 'resume') {
      this.#paused = false
      this.#buffer.forEach((b) => this.#log(...b))
      this.#buffer.length = 0
      return
    }

    if (this.#paused) {
      this.#buffer.push(args)
      return
    }

    this.#log(...args)
  }

  #color (str, { fg, bg, inverse }) {
    if (!this.#stream?.isTTY) {
      return str
    }

    return COLORS.wrap(str, [
      COLORS.fg[fg],
      COLORS.bg[bg],
      inverse && COLORS.inverse
    ])
  }

  #log (levelId, msgPrefix, ...args) {
    if (!this.isVisible(levelId) || typeof this.#stream?.write !== 'function') {
      return
    }

    const level = this.#levels.get(levelId)

    const prefixParts = [
      this.#color('gyp', { fg: 'white', bg: 'black' }),
      this.#color(level.display ?? level.id, level.style)
    ]
    if (msgPrefix) {
      prefixParts.push(this.#color(msgPrefix, { fg: 'magenta' }))
    }

    const prefix = prefixParts.join(' ').trim() + ' '
    const lines = format(...args).split(/\r?\n/).map(l => prefix + l.trim())

    this.#stream.write(lines.join('\n') + '\n')
  }
}

// used to suppress logs in tests
const NULL_LOGGER = !!process.env.NODE_GYP_NULL_LOGGER

module.exports = {
  logger: new Logger(NULL_LOGGER ? null : process.stderr),
  stdout: NULL_LOGGER ? () => {} : (...args) => console.log(...args),
  withPrefix,
  ...procLog
}
