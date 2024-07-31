'use strict'

const fs = require('fs/promises')
const fsm = require('fs-minipass')
const ssri = require('ssri')
const contentPath = require('./path')
const Pipeline = require('minipass-pipeline')

module.exports = read

const MAX_SINGLE_READ_SIZE = 64 * 1024 * 1024
async function read (cache, integrity, opts = {}) {
  const { size } = opts
  const { stat, cpath, sri } = await withContentSri(cache, integrity, async (cpath, sri) => {
    // get size
    const stat = size ? { size } : await fs.stat(cpath)
    return { stat, cpath, sri }
  })

  if (stat.size > MAX_SINGLE_READ_SIZE) {
    return readPipeline(cpath, stat.size, sri, new Pipeline()).concat()
  }

  const data = await fs.readFile(cpath, { encoding: null })

  if (stat.size !== data.length) {
    throw sizeError(stat.size, data.length)
  }

  if (!ssri.checkData(data, sri)) {
    throw integrityError(sri, cpath)
  }

  return data
}

const readPipeline = (cpath, size, sri, stream) => {
  stream.push(
    new fsm.ReadStream(cpath, {
      size,
      readSize: MAX_SINGLE_READ_SIZE,
    }),
    ssri.integrityStream({
      integrity: sri,
      size,
    })
  )
  return stream
}

module.exports.stream = readStream
module.exports.readStream = readStream

function readStream (cache, integrity, opts = {}) {
  const { size } = opts
  const stream = new Pipeline()
  // Set all this up to run on the stream and then just return the stream
  Promise.resolve().then(async () => {
    const { stat, cpath, sri } = await withContentSri(cache, integrity, async (cpath, sri) => {
      // get size
      const stat = size ? { size } : await fs.stat(cpath)
      return { stat, cpath, sri }
    })

    return readPipeline(cpath, stat.size, sri, stream)
  }).catch(err => stream.emit('error', err))

  return stream
}

module.exports.copy = copy

function copy (cache, integrity, dest) {
  return withContentSri(cache, integrity, (cpath) => {
    return fs.copyFile(cpath, dest)
  })
}

module.exports.hasContent = hasContent

async function hasContent (cache, integrity) {
  if (!integrity) {
    return false
  }

  try {
    return await withContentSri(cache, integrity, async (cpath, sri) => {
      const stat = await fs.stat(cpath)
      return { size: stat.size, sri, stat }
    })
  } catch (err) {
    if (err.code === 'ENOENT') {
      return false
    }

    if (err.code === 'EPERM') {
      /* istanbul ignore else */
      if (process.platform !== 'win32') {
        throw err
      } else {
        return false
      }
    }
  }
}

async function withContentSri (cache, integrity, fn) {
  const sri = ssri.parse(integrity)
  // If `integrity` has multiple entries, pick the first digest
  // with available local data.
  const algo = sri.pickAlgorithm()
  const digests = sri[algo]

  if (digests.length <= 1) {
    const cpath = contentPath(cache, digests[0])
    return fn(cpath, digests[0])
  } else {
    // Can't use race here because a generic error can happen before
    // a ENOENT error, and can happen before a valid result
    const results = await Promise.all(digests.map(async (meta) => {
      try {
        return await withContentSri(cache, meta, fn)
      } catch (err) {
        if (err.code === 'ENOENT') {
          return Object.assign(
            new Error('No matching content found for ' + sri.toString()),
            { code: 'ENOENT' }
          )
        }
        return err
      }
    }))
    // Return the first non error if it is found
    const result = results.find((r) => !(r instanceof Error))
    if (result) {
      return result
    }

    // Throw the No matching content found error
    const enoentError = results.find((r) => r.code === 'ENOENT')
    if (enoentError) {
      throw enoentError
    }

    // Throw generic error
    throw results.find((r) => r instanceof Error)
  }
}

function sizeError (expected, found) {
  /* eslint-disable-next-line max-len */
  const err = new Error(`Bad data size: expected inserted data to be ${expected} bytes, but got ${found} instead`)
  err.expected = expected
  err.found = found
  err.code = 'EBADSIZE'
  return err
}

function integrityError (sri, path) {
  const err = new Error(`Integrity verification failed for ${sri} (${path})`)
  err.code = 'EINTEGRITY'
  err.sri = sri
  err.path = path
  return err
}
