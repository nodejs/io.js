const { log, output, input, META } = require('proc-log')
const { explain } = require('./explain-eresolve.js')
const { formatWithOptions } = require('./format')

// This is the general approach to color:
// Eventually this will be exposed somewhere we can refer to these by name.
// Foreground colors only. Never set the background color.
/*
 * Black # (Don't use)
 * Red # Danger
 * Green # Success
 * Yellow # Warning
 * Blue # Accent
 * Magenta # Done
 * Cyan # Emphasis
 * White # (Don't use)
 */

// Translates log levels to chalk colors
const COLOR_PALETTE = ({ chalk: c }) => ({
  heading: c.bold,
  title: c.blueBright,
  timing: c.magentaBright,
  // loglevels
  error: c.red,
  warn: c.yellow,
  notice: c.cyanBright,
  http: c.green,
  info: c.cyan,
  verbose: c.blue,
  silly: c.blue.dim,
})

const LEVEL_OPTIONS = {
  silent: {
    index: 0,
  },
  error: {
    index: 1,
  },
  warn: {
    index: 2,
  },
  notice: {
    index: 3,
  },
  http: {
    index: 4,
  },
  info: {
    index: 5,
  },
  verbose: {
    index: 6,
  },
  silly: {
    index: 7,
  },
}

const LEVEL_METHODS = {
  ...LEVEL_OPTIONS,
  [log.KEYS.timing]: {
    show: ({ timing, index }) => !!timing && index !== 0,
  },
}

const setBlocking = (stream) => {
  // Copied from https://github.com/yargs/set-blocking
  // https://raw.githubusercontent.com/yargs/set-blocking/master/LICENSE.txt
  /* istanbul ignore next - we trust that this works */
  if (stream._handle && stream.isTTY && typeof stream._handle.setBlocking === 'function') {
    stream._handle.setBlocking(true)
  }
  return stream
}

// These are important
// This is the key that is returned to the user for errors
const ERROR_KEY = 'error'
// This is the key producers use to indicate that there
// is a json error that should be merged into the finished output
const JSON_ERROR_KEY = 'jsonError'

const isPlainObject = (v) => v && typeof v === 'object' && !Array.isArray(v)

const getArrayOrObject = (items) => {
  if (items.length) {
    const foundNonObject = items.find(o => !isPlainObject(o))
    // Non-objects and arrays cant be merged, so just return the first item
    if (foundNonObject) {
      return foundNonObject
    }
    // We use objects with 0,1,2,etc keys to merge array
    if (items.every((o, i) => Object.hasOwn(o, i))) {
      return Object.assign([], ...items)
    }
  }
  // Otherwise its an object with all object items merged together
  return Object.assign({}, ...items.filter(o => isPlainObject(o)))
}

const getJsonBuffer = ({ [JSON_ERROR_KEY]: metaError }, buffer) => {
  const items = []
  // meta also contains the meta object passed to flush
  const errors = metaError ? [metaError] : []
  // index 1 is the meta, 2 is the logged argument
  for (const [, { [JSON_ERROR_KEY]: error }, obj] of buffer) {
    if (obj) {
      items.push(obj)
    }
    if (error) {
      errors.push(error)
    }
  }

  if (!items.length && !errors.length) {
    return null
  }

  const res = getArrayOrObject(items)

  // This skips any error checking since we can only set an error property
  // on an object that can be stringified
  // XXX(BREAKING_CHANGE): remove this in favor of always returning an object with result and error keys
  if (isPlainObject(res) && errors.length) {
    // This is not ideal. JSON output has always been keyed at the root with an `error`
    // key, so we cant change that without it being a breaking change. At the same time
    // some commands output arbitrary keys at the top level of the output, such as package
    // names. So the output could already have the same key. The choice here is to overwrite
    // it with our error since that is (probably?) more important.
    // XXX(BREAKING_CHANGE): all json output should be keyed under well known keys, eg `result` and `error`
    if (res[ERROR_KEY]) {
      log.warn('', `overwriting existing ${ERROR_KEY} on json output`)
    }
    res[ERROR_KEY] = getArrayOrObject(errors)
  }

  return res
}

const withMeta = (handler) => (level, ...args) => {
  let meta = {}
  const last = args.at(-1)
  if (last && typeof last === 'object' && Object.hasOwn(last, META)) {
    meta = args.pop()
  }
  return handler(level, meta, ...args)
}

class Display {
  #logState = {
    buffering: true,
    buffer: [],
  }

  #outputState = {
    buffering: true,
    buffer: [],
  }

  // colors
  #noColorChalk
  #stdoutChalk
  #stdoutColor
  #stderrChalk
  #stderrColor
  #logColors

  // progress
  #progress

  // options
  #command
  #levelIndex
  #timing
  #json
  #heading
  #silent

  // display streams
  #stdout
  #stderr

  constructor ({ stdout, stderr }) {
    this.#stdout = setBlocking(stdout)
    this.#stderr = setBlocking(stderr)

    // Handlers are set immediately so they can buffer all events
    process.on('log', this.#logHandler)
    process.on('output', this.#outputHandler)
    process.on('input', this.#inputHandler)
    this.#progress = new Progress({ stream: stderr })
  }

  off () {
    process.off('log', this.#logHandler)
    this.#logState.buffer.length = 0
    process.off('output', this.#outputHandler)
    this.#outputState.buffer.length = 0
    process.off('input', this.#inputHandler)
    this.#progress.off()
  }

  get chalk () {
    return {
      noColor: this.#noColorChalk,
      stdout: this.#stdoutChalk,
      stderr: this.#stderrChalk,
    }
  }

  async load ({
    command,
    heading,
    json,
    loglevel,
    progress,
    stderrColor,
    stdoutColor,
    timing,
    unicode,
  }) {
    // get createSupportsColor from chalk directly if this lands
    // https://github.com/chalk/chalk/pull/600
    const [{ Chalk }, { createSupportsColor }] = await Promise.all([
      import('chalk'),
      import('supports-color'),
    ])
    // we get the chalk level based on a null stream meaning chalk will only use
    // what it knows about the environment to get color support since we already
    // determined in our definitions that we want to show colors.
    const level = Math.max(createSupportsColor(null).level, 1)
    this.#noColorChalk = new Chalk({ level: 0 })
    this.#stdoutColor = stdoutColor
    this.#stdoutChalk = stdoutColor ? new Chalk({ level }) : this.#noColorChalk
    this.#stderrColor = stderrColor
    this.#stderrChalk = stderrColor ? new Chalk({ level }) : this.#noColorChalk
    this.#logColors = COLOR_PALETTE({ chalk: this.#stderrChalk })

    this.#command = command
    this.#levelIndex = LEVEL_OPTIONS[loglevel].index
    this.#timing = timing
    this.#json = json
    this.#heading = heading
    this.#silent = this.#levelIndex <= 0

    // Emit resume event on the logs which will flush output
    log.resume()
    output.flush()
    this.#progress.load({
      unicode,
      enabled: !!progress && !this.#silent,
    })
  }

  // STREAM WRITES

  // Write formatted and (non-)colorized output to streams
  #write (stream, options, ...args) {
    const colors = stream === this.#stdout ? this.#stdoutColor : this.#stderrColor
    const value = formatWithOptions({ colors, ...options }, ...args)
    this.#progress.write(() => stream.write(value))
  }

  // HANDLERS

  // Arrow function assigned to a private class field so it can be passed
  // directly as a listener and still reference "this"
  #logHandler = withMeta((level, meta, ...args) => {
    switch (level) {
      case log.KEYS.resume:
        this.#logState.buffering = false
        this.#logState.buffer.forEach((item) => this.#tryWriteLog(...item))
        this.#logState.buffer.length = 0
        break

      case log.KEYS.pause:
        this.#logState.buffering = true
        break

      default:
        if (this.#logState.buffering) {
          this.#logState.buffer.push([level, meta, ...args])
        } else {
          this.#tryWriteLog(level, meta, ...args)
        }
        break
    }
  })

  // Arrow function assigned to a private class field so it can be passed
  // directly as a listener and still reference "this"
  #outputHandler = withMeta((level, meta, ...args) => {
    this.#json = typeof meta.json === 'boolean' ? meta.json : this.#json
    switch (level) {
      case output.KEYS.flush: {
        this.#outputState.buffering = false
        if (this.#json) {
          const json = getJsonBuffer(meta, this.#outputState.buffer)
          if (json) {
            this.#writeOutput(output.KEYS.standard, meta, JSON.stringify(json, null, 2))
          }
        } else {
          this.#outputState.buffer.forEach((item) => this.#writeOutput(...item))
        }
        this.#outputState.buffer.length = 0
        break
      }

      case output.KEYS.buffer:
        this.#outputState.buffer.push([output.KEYS.standard, meta, ...args])
        break

      default:
        if (this.#outputState.buffering) {
          this.#outputState.buffer.push([level, meta, ...args])
        } else {
          // HACK: Check if the argument looks like a run-script banner. This can be
          // replaced with proc-log.META in @npmcli/run-script
          if (typeof args[0] === 'string' && args[0].startsWith('\n> ') && args[0].endsWith('\n')) {
            if (this.#silent || ['exec', 'explore'].includes(this.#command)) {
              // Silent mode and some specific commands always hide run script banners
              break
            } else if (this.#json) {
              // In json mode, change output to stderr since we dont want to break json
              // parsing on stdout if the user is piping to jq or something.
              // XXX: in a future (breaking?) change it might make sense for run-script to
              // always output these banners with proc-log.output.error if we think they
              // align closer with "logging" instead of "output"
              level = output.KEYS.error
            }
          }
          this.#writeOutput(level, meta, ...args)
        }
        break
    }
  })

  #inputHandler = withMeta((level, meta, ...args) => {
    switch (level) {
      case input.KEYS.start:
        log.pause()
        this.#outputState.buffering = true
        this.#progress.off()
        break

      case input.KEYS.end:
        log.resume()
        output.flush()
        this.#progress.resume()
        break

      case input.KEYS.read: {
        // The convention when calling input.read is to pass in a single fn that returns
        // the promise to await. resolve and reject are provided by proc-log
        const [res, rej, p] = args
        return input.start(() => p()
          .then(res)
          .catch(rej)
          // Any call to procLog.input.read will render a prompt to the user, so we always
          // add a single newline of output to stdout to move the cursor to the next line
          .finally(() => output.standard('')))
      }
    }
  })

  // OUTPUT

  #writeOutput (level, meta, ...args) {
    switch (level) {
      case output.KEYS.standard:
        this.#write(this.#stdout, {}, ...args)
        break

      case output.KEYS.error:
        this.#write(this.#stderr, {}, ...args)
        break
    }
  }

  // LOGS

  #tryWriteLog (level, meta, ...args) {
    try {
      // Also (and this is a really inexcusable kludge), we patch the
      // log.warn() method so that when we see a peerDep override
      // explanation from Arborist, we can replace the object with a
      // highly abbreviated explanation of what's being overridden.
      // TODO: this could probably be moved to arborist now that display is refactored
      const [heading, message, expl] = args
      if (level === log.KEYS.warn && heading === 'ERESOLVE' && expl && typeof expl === 'object') {
        this.#writeLog(level, meta, heading, message)
        this.#writeLog(level, meta, '', explain(expl, this.#stderrChalk, 2))
        return
      }
      this.#writeLog(level, meta, ...args)
    } catch (ex) {
      try {
        // if it crashed once, it might again!
        this.#writeLog(log.KEYS.verbose, meta, '', `attempt to log crashed`, ...args, ex)
      } catch (ex2) {
        // This happens if the object has an inspect method that crashes so just console.error
        // with the errors but don't do anything else that might error again.
        // eslint-disable-next-line no-console
        console.error(`attempt to log crashed`, ex, ex2)
      }
    }
  }

  #writeLog (level, meta, ...args) {
    const levelOpts = LEVEL_METHODS[level]
    const show = levelOpts.show ?? (({ index }) => levelOpts.index <= index)
    const force = meta.force && !this.#silent

    if (force || show({ index: this.#levelIndex, timing: this.#timing })) {
      // this mutates the array so we can pass args directly to format later
      const title = args.shift()
      const prefix = [
        this.#logColors.heading(this.#heading),
        this.#logColors[level](level),
        title ? this.#logColors.title(title) : null,
      ]
      this.#write(this.#stderr, { prefix }, ...args)
    }
  }
}

class Progress {
  // Taken from https://github.com/sindresorhus/cli-spinners
  // MIT License
  // Copyright (c) Sindre Sorhus <sindresorhus@gmail.com> (https://sindresorhus.com)
  static dots = { duration: 80, frames: ['⠋', '⠙', '⠹', '⠸', '⠼', '⠴', '⠦', '⠧', '⠇', '⠏'] }
  static lines = { duration: 130, frames: ['-', '\\', '|', '/'] }

  #stream
  #spinner
  #enabled = false

  #frameIndex = 0
  #lastUpdate = 0
  #interval
  #timeout

  // We are rendering is enabled option is set and we are not waiting for the render timeout
  get #rendering () {
    return this.#enabled && !this.#timeout
  }

  // We are spinning if enabled option is set and the render interval has been set
  get #spinning () {
    return this.#enabled && this.#interval
  }

  constructor ({ stream }) {
    this.#stream = stream
  }

  load ({ enabled, unicode }) {
    this.#enabled = enabled
    this.#spinner = unicode ? Progress.dots : Progress.lines
    // Dont render the spinner for short durations
    this.#render(200)
  }

  off () {
    if (!this.#enabled) {
      return
    }
    clearTimeout(this.#timeout)
    this.#timeout = null
    clearInterval(this.#interval)
    this.#interval = null
    this.#frameIndex = 0
    this.#lastUpdate = 0
    this.#clearSpinner()
  }

  resume () {
    this.#render()
  }

  // If we are currenting rendering the spinner we clear it
  // before writing our line and then re-render the spinner after.
  // If not then all we need to do is write the line
  write (write) {
    if (this.#spinning) {
      this.#clearSpinner()
    }
    write()
    if (this.#spinning) {
      this.#render()
    }
  }

  #render (ms) {
    if (ms) {
      this.#timeout = setTimeout(() => {
        this.#timeout = null
        this.#renderSpinner()
      }, ms)
      // Make sure this timeout does not keep the process open
      this.#timeout.unref()
    } else {
      this.#renderSpinner()
    }
  }

  #renderSpinner () {
    if (!this.#rendering) {
      return
    }
    // We always attempt to render immediately but we only request to move to the next
    // frame if it has been longer than our spinner frame duration since our last update
    this.#renderFrame(Date.now() - this.#lastUpdate >= this.#spinner.duration)
    clearInterval(this.#interval)
    this.#interval = setInterval(() => this.#renderFrame(true), this.#spinner.duration)
  }

  #renderFrame (next) {
    if (next) {
      this.#lastUpdate = Date.now()
      this.#frameIndex++
      if (this.#frameIndex >= this.#spinner.frames.length) {
        this.#frameIndex = 0
      }
    }
    this.#clearSpinner()
    this.#stream.write(this.#spinner.frames[this.#frameIndex])
  }

  #clearSpinner () {
    // Move to the start of the line and clear the rest of the line
    this.#stream.cursorTo(0)
    this.#stream.clearLine(1)
  }
}

module.exports = Display
