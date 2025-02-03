const fs = require('fs/promises')
const { runInThisContext } = require('vm')
const { promisify } = require('util')
const { randomBytes } = require('crypto')
const { Module } = require('module')
const { dirname, basename } = require('path')
const { read } = require('read')

const files = {}

class PromZard {
  #file = null
  #backupFile = null
  #ctx = null
  #unique = randomBytes(8).toString('hex')
  #prompts = []

  constructor (file, ctx = {}, options = {}) {
    this.#file = file
    this.#ctx = ctx
    this.#backupFile = options.backupFile
  }

  static async promzard (file, ctx, options) {
    const pz = new PromZard(file, ctx, options)
    return pz.load()
  }

  static async fromBuffer (buf, ctx, options) {
    let filename = 0
    do {
      filename = '\0' + Math.random()
    } while (files[filename])
    files[filename] = buf
    const ret = await PromZard.promzard(filename, ctx, options)
    delete files[filename]
    return ret
  }

  async load () {
    if (files[this.#file]) {
      return this.#loaded()
    }

    try {
      files[this.#file] = await fs.readFile(this.#file, 'utf8')
    } catch (er) {
      if (er && this.#backupFile) {
        this.#file = this.#backupFile
        this.#backupFile = null
        return this.load()
      }
      throw er
    }

    return this.#loaded()
  }

  async #loaded () {
    const mod = new Module(this.#file, module)
    mod.loaded = true
    mod.filename = this.#file
    mod.id = this.#file
    mod.paths = Module._nodeModulePaths(dirname(this.#file))

    this.#ctx.prompt = this.#makePrompt()
    this.#ctx.__filename = this.#file
    this.#ctx.__dirname = dirname(this.#file)
    this.#ctx.__basename = basename(this.#file)
    this.#ctx.module = mod
    this.#ctx.require = (p) => mod.require(p)
    this.#ctx.require.resolve = (p) => Module._resolveFilename(p, mod)
    this.#ctx.exports = mod.exports

    const body = `(function(${Object.keys(this.#ctx).join(', ')}) { ${files[this.#file]}\n })`
    runInThisContext(body, this.#file).apply(this.#ctx, Object.values(this.#ctx))
    this.#ctx.res = mod.exports

    return this.#walk()
  }

  #makePrompt () {
    return (...args) => {
      let p, d, t
      for (let i = 0; i < args.length; i++) {
        const a = args[i]
        if (typeof a === 'string') {
          if (p) {
            d = a
          } else {
            p = a
          }
        } else if (typeof a === 'function') {
          t = a
        } else if (a && typeof a === 'object') {
          p = a.prompt || p
          d = a.default || d
          t = a.transform || t
        }
      }
      try {
        return `${this.#unique}-${this.#prompts.length}`
      } finally {
        this.#prompts.push([p, d, t])
      }
    }
  }

  async #walk (o = this.#ctx.res) {
    const keys = Object.keys(o)

    const len = keys.length
    let i = 0

    while (i < len) {
      const k = keys[i]
      const v = o[k]
      i++

      if (v && typeof v === 'object') {
        o[k] = await this.#walk(v)
        continue
      }

      if (v && typeof v === 'string' && v.startsWith(this.#unique)) {
        const n = +v.slice(this.#unique.length + 1)

        // default to the key
        // default to the ctx value, if there is one
        const [prompt = k, def = this.#ctx[k], tx] = this.#prompts[n]

        try {
          o[k] = await this.#prompt(prompt, def, tx)
        } catch (er) {
          if (er.notValid) {
            // eslint-disable-next-line no-console
            console.log(er.message)
            i--
          } else {
            throw er
          }
        }
        continue
      }

      if (typeof v === 'function') {
        // XXX: remove v.length check to remove cb from functions
        // would be a breaking change for `npm init`
        // XXX: if cb is no longer an argument then this.#ctx should
        // be passed in to allow arrow fns to be used and still access ctx
        const fn = v.length ? promisify(v) : v
        o[k] = await fn.call(this.#ctx)
        // back up so that we process this one again.
        // this is because it might return a prompt() call in the cb.
        i--
        continue
      }
    }

    return o
  }

  async #prompt (prompt, def, tx) {
    const res = await read({ prompt: prompt + ':', default: def }).then((r) => tx ? tx(r) : r)
    // XXX: remove this to require throwing an error instead of
    // returning it. would be a breaking change for `npm init`
    if (res instanceof Error && res.notValid) {
      throw res
    }
    return res
  }
}

module.exports = PromZard.promzard
module.exports.fromBuffer = PromZard.fromBuffer
module.exports.PromZard = PromZard
