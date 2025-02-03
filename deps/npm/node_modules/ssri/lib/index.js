'use strict'

const crypto = require('crypto')
const { Minipass } = require('minipass')

const SPEC_ALGORITHMS = ['sha512', 'sha384', 'sha256']
const DEFAULT_ALGORITHMS = ['sha512']

// TODO: this should really be a hardcoded list of algorithms we support,
// rather than [a-z0-9].
const BASE64_REGEX = /^[a-z0-9+/]+(?:=?=?)$/i
const SRI_REGEX = /^([a-z0-9]+)-([^?]+)([?\S*]*)$/
const STRICT_SRI_REGEX = /^([a-z0-9]+)-([A-Za-z0-9+/=]{44,88})(\?[\x21-\x7E]*)?$/
const VCHAR_REGEX = /^[\x21-\x7E]+$/

const getOptString = options => options?.length ? `?${options.join('?')}` : ''

class IntegrityStream extends Minipass {
  #emittedIntegrity
  #emittedSize
  #emittedVerified

  constructor (opts) {
    super()
    this.size = 0
    this.opts = opts

    // may be overridden later, but set now for class consistency
    this.#getOptions()

    // options used for calculating stream.  can't be changed.
    if (opts?.algorithms) {
      this.algorithms = [...opts.algorithms]
    } else {
      this.algorithms = [...DEFAULT_ALGORITHMS]
    }
    if (this.algorithm !== null && !this.algorithms.includes(this.algorithm)) {
      this.algorithms.push(this.algorithm)
    }

    this.hashes = this.algorithms.map(crypto.createHash)
  }

  #getOptions () {
    // For verification
    this.sri = this.opts?.integrity ? parse(this.opts?.integrity, this.opts) : null
    this.expectedSize = this.opts?.size

    if (!this.sri) {
      this.algorithm = null
    } else if (this.sri.isHash) {
      this.goodSri = true
      this.algorithm = this.sri.algorithm
    } else {
      this.goodSri = !this.sri.isEmpty()
      this.algorithm = this.sri.pickAlgorithm(this.opts)
    }

    this.digests = this.goodSri ? this.sri[this.algorithm] : null
    this.optString = getOptString(this.opts?.options)
  }

  on (ev, handler) {
    if (ev === 'size' && this.#emittedSize) {
      return handler(this.#emittedSize)
    }

    if (ev === 'integrity' && this.#emittedIntegrity) {
      return handler(this.#emittedIntegrity)
    }

    if (ev === 'verified' && this.#emittedVerified) {
      return handler(this.#emittedVerified)
    }

    return super.on(ev, handler)
  }

  emit (ev, data) {
    if (ev === 'end') {
      this.#onEnd()
    }
    return super.emit(ev, data)
  }

  write (data) {
    this.size += data.length
    this.hashes.forEach(h => h.update(data))
    return super.write(data)
  }

  #onEnd () {
    if (!this.goodSri) {
      this.#getOptions()
    }
    const newSri = parse(this.hashes.map((h, i) => {
      return `${this.algorithms[i]}-${h.digest('base64')}${this.optString}`
    }).join(' '), this.opts)
    // Integrity verification mode
    const match = this.goodSri && newSri.match(this.sri, this.opts)
    if (typeof this.expectedSize === 'number' && this.size !== this.expectedSize) {
      /* eslint-disable-next-line max-len */
      const err = new Error(`stream size mismatch when checking ${this.sri}.\n  Wanted: ${this.expectedSize}\n  Found: ${this.size}`)
      err.code = 'EBADSIZE'
      err.found = this.size
      err.expected = this.expectedSize
      err.sri = this.sri
      this.emit('error', err)
    } else if (this.sri && !match) {
      /* eslint-disable-next-line max-len */
      const err = new Error(`${this.sri} integrity checksum failed when using ${this.algorithm}: wanted ${this.digests} but got ${newSri}. (${this.size} bytes)`)
      err.code = 'EINTEGRITY'
      err.found = newSri
      err.expected = this.digests
      err.algorithm = this.algorithm
      err.sri = this.sri
      this.emit('error', err)
    } else {
      this.#emittedSize = this.size
      this.emit('size', this.size)
      this.#emittedIntegrity = newSri
      this.emit('integrity', newSri)
      if (match) {
        this.#emittedVerified = match
        this.emit('verified', match)
      }
    }
  }
}

class Hash {
  get isHash () {
    return true
  }

  constructor (hash, opts) {
    const strict = opts?.strict
    this.source = hash.trim()

    // set default values so that we make V8 happy to
    // always see a familiar object template.
    this.digest = ''
    this.algorithm = ''
    this.options = []

    // 3.1. Integrity metadata (called "Hash" by ssri)
    // https://w3c.github.io/webappsec-subresource-integrity/#integrity-metadata-description
    const match = this.source.match(
      strict
        ? STRICT_SRI_REGEX
        : SRI_REGEX
    )
    if (!match) {
      return
    }
    if (strict && !SPEC_ALGORITHMS.includes(match[1])) {
      return
    }
    this.algorithm = match[1]
    this.digest = match[2]

    const rawOpts = match[3]
    if (rawOpts) {
      this.options = rawOpts.slice(1).split('?')
    }
  }

  hexDigest () {
    return this.digest && Buffer.from(this.digest, 'base64').toString('hex')
  }

  toJSON () {
    return this.toString()
  }

  match (integrity, opts) {
    const other = parse(integrity, opts)
    if (!other) {
      return false
    }
    if (other.isIntegrity) {
      const algo = other.pickAlgorithm(opts, [this.algorithm])

      if (!algo) {
        return false
      }

      const foundHash = other[algo].find(hash => hash.digest === this.digest)

      if (foundHash) {
        return foundHash
      }

      return false
    }
    return other.digest === this.digest ? other : false
  }

  toString (opts) {
    if (opts?.strict) {
      // Strict mode enforces the standard as close to the foot of the
      // letter as it can.
      if (!(
        // The spec has very restricted productions for algorithms.
        // https://www.w3.org/TR/CSP2/#source-list-syntax
        SPEC_ALGORITHMS.includes(this.algorithm) &&
        // Usually, if someone insists on using a "different" base64, we
        // leave it as-is, since there's multiple standards, and the
        // specified is not a URL-safe variant.
        // https://www.w3.org/TR/CSP2/#base64_value
        this.digest.match(BASE64_REGEX) &&
        // Option syntax is strictly visual chars.
        // https://w3c.github.io/webappsec-subresource-integrity/#grammardef-option-expression
        // https://tools.ietf.org/html/rfc5234#appendix-B.1
        this.options.every(opt => opt.match(VCHAR_REGEX))
      )) {
        return ''
      }
    }
    return `${this.algorithm}-${this.digest}${getOptString(this.options)}`
  }
}

function integrityHashToString (toString, sep, opts, hashes) {
  const toStringIsNotEmpty = toString !== ''

  let shouldAddFirstSep = false
  let complement = ''

  const lastIndex = hashes.length - 1

  for (let i = 0; i < lastIndex; i++) {
    const hashString = Hash.prototype.toString.call(hashes[i], opts)

    if (hashString) {
      shouldAddFirstSep = true

      complement += hashString
      complement += sep
    }
  }

  const finalHashString = Hash.prototype.toString.call(hashes[lastIndex], opts)

  if (finalHashString) {
    shouldAddFirstSep = true
    complement += finalHashString
  }

  if (toStringIsNotEmpty && shouldAddFirstSep) {
    return toString + sep + complement
  }

  return toString + complement
}

class Integrity {
  get isIntegrity () {
    return true
  }

  toJSON () {
    return this.toString()
  }

  isEmpty () {
    return Object.keys(this).length === 0
  }

  toString (opts) {
    let sep = opts?.sep || ' '
    let toString = ''

    if (opts?.strict) {
      // Entries must be separated by whitespace, according to spec.
      sep = sep.replace(/\S+/g, ' ')

      for (const hash of SPEC_ALGORITHMS) {
        if (this[hash]) {
          toString = integrityHashToString(toString, sep, opts, this[hash])
        }
      }
    } else {
      for (const hash of Object.keys(this)) {
        toString = integrityHashToString(toString, sep, opts, this[hash])
      }
    }

    return toString
  }

  concat (integrity, opts) {
    const other = typeof integrity === 'string'
      ? integrity
      : stringify(integrity, opts)
    return parse(`${this.toString(opts)} ${other}`, opts)
  }

  hexDigest () {
    return parse(this, { single: true }).hexDigest()
  }

  // add additional hashes to an integrity value, but prevent
  // *changing* an existing integrity hash.
  merge (integrity, opts) {
    const other = parse(integrity, opts)
    for (const algo in other) {
      if (this[algo]) {
        if (!this[algo].find(hash =>
          other[algo].find(otherhash =>
            hash.digest === otherhash.digest))) {
          throw new Error('hashes do not match, cannot update integrity')
        }
      } else {
        this[algo] = other[algo]
      }
    }
  }

  match (integrity, opts) {
    const other = parse(integrity, opts)
    if (!other) {
      return false
    }
    const algo = other.pickAlgorithm(opts, Object.keys(this))
    return (
      !!algo &&
      this[algo] &&
      other[algo] &&
      this[algo].find(hash =>
        other[algo].find(otherhash =>
          hash.digest === otherhash.digest
        )
      )
    ) || false
  }

  // Pick the highest priority algorithm present, optionally also limited to a
  // set of hashes found in another integrity.  When limiting it may return
  // nothing.
  pickAlgorithm (opts, hashes) {
    const pickAlgorithm = opts?.pickAlgorithm || getPrioritizedHash
    const keys = Object.keys(this).filter(k => {
      if (hashes?.length) {
        return hashes.includes(k)
      }
      return true
    })
    if (keys.length) {
      return keys.reduce((acc, algo) => pickAlgorithm(acc, algo) || acc)
    }
    // no intersection between this and hashes,
    return null
  }
}

module.exports.parse = parse
function parse (sri, opts) {
  if (!sri) {
    return null
  }
  if (typeof sri === 'string') {
    return _parse(sri, opts)
  } else if (sri.algorithm && sri.digest) {
    const fullSri = new Integrity()
    fullSri[sri.algorithm] = [sri]
    return _parse(stringify(fullSri, opts), opts)
  } else {
    return _parse(stringify(sri, opts), opts)
  }
}

function _parse (integrity, opts) {
  // 3.4.3. Parse metadata
  // https://w3c.github.io/webappsec-subresource-integrity/#parse-metadata
  if (opts?.single) {
    return new Hash(integrity, opts)
  }
  const hashes = integrity.trim().split(/\s+/).reduce((acc, string) => {
    const hash = new Hash(string, opts)
    if (hash.algorithm && hash.digest) {
      const algo = hash.algorithm
      if (!acc[algo]) {
        acc[algo] = []
      }
      acc[algo].push(hash)
    }
    return acc
  }, new Integrity())
  return hashes.isEmpty() ? null : hashes
}

module.exports.stringify = stringify
function stringify (obj, opts) {
  if (obj.algorithm && obj.digest) {
    return Hash.prototype.toString.call(obj, opts)
  } else if (typeof obj === 'string') {
    return stringify(parse(obj, opts), opts)
  } else {
    return Integrity.prototype.toString.call(obj, opts)
  }
}

module.exports.fromHex = fromHex
function fromHex (hexDigest, algorithm, opts) {
  const optString = getOptString(opts?.options)
  return parse(
    `${algorithm}-${
      Buffer.from(hexDigest, 'hex').toString('base64')
    }${optString}`, opts
  )
}

module.exports.fromData = fromData
function fromData (data, opts) {
  const algorithms = opts?.algorithms || [...DEFAULT_ALGORITHMS]
  const optString = getOptString(opts?.options)
  return algorithms.reduce((acc, algo) => {
    const digest = crypto.createHash(algo).update(data).digest('base64')
    const hash = new Hash(
      `${algo}-${digest}${optString}`,
      opts
    )
    /* istanbul ignore else - it would be VERY strange if the string we
     * just calculated with an algo did not have an algo or digest.
     */
    if (hash.algorithm && hash.digest) {
      const hashAlgo = hash.algorithm
      if (!acc[hashAlgo]) {
        acc[hashAlgo] = []
      }
      acc[hashAlgo].push(hash)
    }
    return acc
  }, new Integrity())
}

module.exports.fromStream = fromStream
function fromStream (stream, opts) {
  const istream = integrityStream(opts)
  return new Promise((resolve, reject) => {
    stream.pipe(istream)
    stream.on('error', reject)
    istream.on('error', reject)
    let sri
    istream.on('integrity', s => {
      sri = s
    })
    istream.on('end', () => resolve(sri))
    istream.resume()
  })
}

module.exports.checkData = checkData
function checkData (data, sri, opts) {
  sri = parse(sri, opts)
  if (!sri || !Object.keys(sri).length) {
    if (opts?.error) {
      throw Object.assign(
        new Error('No valid integrity hashes to check against'), {
          code: 'EINTEGRITY',
        }
      )
    } else {
      return false
    }
  }
  const algorithm = sri.pickAlgorithm(opts)
  const digest = crypto.createHash(algorithm).update(data).digest('base64')
  const newSri = parse({ algorithm, digest })
  const match = newSri.match(sri, opts)
  opts = opts || {}
  if (match || !(opts.error)) {
    return match
  } else if (typeof opts.size === 'number' && (data.length !== opts.size)) {
    /* eslint-disable-next-line max-len */
    const err = new Error(`data size mismatch when checking ${sri}.\n  Wanted: ${opts.size}\n  Found: ${data.length}`)
    err.code = 'EBADSIZE'
    err.found = data.length
    err.expected = opts.size
    err.sri = sri
    throw err
  } else {
    /* eslint-disable-next-line max-len */
    const err = new Error(`Integrity checksum failed when using ${algorithm}: Wanted ${sri}, but got ${newSri}. (${data.length} bytes)`)
    err.code = 'EINTEGRITY'
    err.found = newSri
    err.expected = sri
    err.algorithm = algorithm
    err.sri = sri
    throw err
  }
}

module.exports.checkStream = checkStream
function checkStream (stream, sri, opts) {
  opts = opts || Object.create(null)
  opts.integrity = sri
  sri = parse(sri, opts)
  if (!sri || !Object.keys(sri).length) {
    return Promise.reject(Object.assign(
      new Error('No valid integrity hashes to check against'), {
        code: 'EINTEGRITY',
      }
    ))
  }
  const checker = integrityStream(opts)
  return new Promise((resolve, reject) => {
    stream.pipe(checker)
    stream.on('error', reject)
    checker.on('error', reject)
    let verified
    checker.on('verified', s => {
      verified = s
    })
    checker.on('end', () => resolve(verified))
    checker.resume()
  })
}

module.exports.integrityStream = integrityStream
function integrityStream (opts = Object.create(null)) {
  return new IntegrityStream(opts)
}

module.exports.create = createIntegrity
function createIntegrity (opts) {
  const algorithms = opts?.algorithms || [...DEFAULT_ALGORITHMS]
  const optString = getOptString(opts?.options)

  const hashes = algorithms.map(crypto.createHash)

  return {
    update: function (chunk, enc) {
      hashes.forEach(h => h.update(chunk, enc))
      return this
    },
    digest: function () {
      const integrity = algorithms.reduce((acc, algo) => {
        const digest = hashes.shift().digest('base64')
        const hash = new Hash(
          `${algo}-${digest}${optString}`,
          opts
        )
        /* istanbul ignore else - it would be VERY strange if the hash we
         * just calculated with an algo did not have an algo or digest.
         */
        if (hash.algorithm && hash.digest) {
          const hashAlgo = hash.algorithm
          if (!acc[hashAlgo]) {
            acc[hashAlgo] = []
          }
          acc[hashAlgo].push(hash)
        }
        return acc
      }, new Integrity())

      return integrity
    },
  }
}

const NODE_HASHES = crypto.getHashes()

// This is a Best Effort™ at a reasonable priority for hash algos
const DEFAULT_PRIORITY = [
  'md5', 'whirlpool', 'sha1', 'sha224', 'sha256', 'sha384', 'sha512',
  // TODO - it's unclear _which_ of these Node will actually use as its name
  //        for the algorithm, so we guesswork it based on the OpenSSL names.
  'sha3',
  'sha3-256', 'sha3-384', 'sha3-512',
  'sha3_256', 'sha3_384', 'sha3_512',
].filter(algo => NODE_HASHES.includes(algo))

function getPrioritizedHash (algo1, algo2) {
  /* eslint-disable-next-line max-len */
  return DEFAULT_PRIORITY.indexOf(algo1.toLowerCase()) >= DEFAULT_PRIORITY.indexOf(algo2.toLowerCase())
    ? algo1
    : algo2
}
