const Parser = require('jsonparse')
const { Minipass } = require('minipass')

class JSONStreamError extends Error {
  constructor (err, caller) {
    super(err.message)
    Error.captureStackTrace(this, caller || this.constructor)
  }

  get name () {
    return 'JSONStreamError'
  }
}

const check = (x, y) =>
  typeof x === 'string' ? String(y) === x
  : x && typeof x.test === 'function' ? x.test(y)
  : typeof x === 'boolean' || typeof x === 'object' ? x
  : typeof x === 'function' ? x(y)
  : false

class JSONStream extends Minipass {
  #count = 0
  #ending = false
  #footer = null
  #header = null
  #map = null
  #onTokenOriginal
  #parser
  #path = null
  #root = null

  constructor (opts) {
    super({
      ...opts,
      objectMode: true,
    })

    const parser = this.#parser = new Parser()
    parser.onValue = value => this.#onValue(value)
    this.#onTokenOriginal = parser.onToken
    parser.onToken = (token, value) => this.#onToken(token, value)
    parser.onError = er => this.#onError(er)

    this.#path = typeof opts.path === 'string'
      ? opts.path.split('.').map(e =>
        e === '$*' ? { emitKey: true }
        : e === '*' ? true
        : e === '' ? { recurse: true }
        : e)
      : Array.isArray(opts.path) && opts.path.length ? opts.path
      : null

    if (typeof opts.map === 'function') {
      this.#map = opts.map
    }
  }

  #setHeaderFooter (key, value) {
    // header has not been emitted yet
    if (this.#header !== false) {
      this.#header = this.#header || {}
      this.#header[key] = value
    }

    // footer has not been emitted yet but header has
    if (this.#footer !== false && this.#header === false) {
      this.#footer = this.#footer || {}
      this.#footer[key] = value
    }
  }

  #onError (er) {
    // error will always happen during a write() call.
    const caller = this.#ending ? this.end : this.write
    this.#ending = false
    return this.emit('error', new JSONStreamError(er, caller))
  }

  #onToken (token, value) {
    const parser = this.#parser
    this.#onTokenOriginal.call(this.#parser, token, value)
    if (parser.stack.length === 0) {
      if (this.#root) {
        const root = this.#root
        if (!this.#path) {
          super.write(root)
        }
        this.#root = null
        this.#count = 0
      }
    }
  }

  #onValue (value) {
    const parser = this.#parser
    // the LAST onValue encountered is the root object.
    // just overwrite it each time.
    this.#root = value

    if (!this.#path) {
      return
    }

    let i = 0 // iterates on path
    let j = 0 // iterates on stack
    let emitKey = false
    while (i < this.#path.length) {
      const key = this.#path[i]
      j++

      if (key && !key.recurse) {
        const c = (j === parser.stack.length) ? parser : parser.stack[j]
        if (!c) {
          return
        }
        if (!check(key, c.key)) {
          this.#setHeaderFooter(c.key, value)
          return
        }
        emitKey = !!key.emitKey
        i++
      } else {
        i++
        if (i >= this.#path.length) {
          return
        }
        const nextKey = this.#path[i]
        if (!nextKey) {
          return
        }
        while (true) {
          const c = (j === parser.stack.length) ? parser : parser.stack[j]
          if (!c) {
            return
          }
          if (check(nextKey, c.key)) {
            i++
            if (!Object.isFrozen(parser.stack[j])) {
              parser.stack[j].value = null
            }
            break
          } else {
            this.#setHeaderFooter(c.key, value)
          }
          j++
        }
      }
    }

    // emit header
    if (this.#header) {
      const header = this.#header
      this.#header = false
      this.emit('header', header)
    }
    if (j !== parser.stack.length) {
      return
    }

    this.#count++
    const actualPath = parser.stack.slice(1)
      .map(e => e.key).concat([parser.key])
    if (value !== null && value !== undefined) {
      const data = this.#map ? this.#map(value, actualPath) : value
      if (data !== null && data !== undefined) {
        const emit = emitKey ? { value: data } : data
        if (emitKey) {
          emit.key = parser.key
        }
        super.write(emit)
      }
    }

    if (parser.value) {
      delete parser.value[parser.key]
    }

    for (const k of parser.stack) {
      k.value = null
    }
  }

  write (chunk, encoding) {
    if (typeof chunk === 'string') {
      chunk = Buffer.from(chunk, encoding)
    } else if (!Buffer.isBuffer(chunk)) {
      return this.emit('error', new TypeError(
        'Can only parse JSON from string or buffer input'))
    }
    this.#parser.write(chunk)
    return this.flowing
  }

  end (chunk, encoding) {
    this.#ending = true
    if (chunk) {
      this.write(chunk, encoding)
    }

    const h = this.#header
    this.#header = null
    const f = this.#footer
    this.#footer = null
    if (h) {
      this.emit('header', h)
    }
    if (f) {
      this.emit('footer', f)
    }
    return super.end()
  }

  static get JSONStreamError () {
    return JSONStreamError
  }

  static parse (path, map) {
    return new JSONStream({ path, map })
  }
}

module.exports = JSONStream
