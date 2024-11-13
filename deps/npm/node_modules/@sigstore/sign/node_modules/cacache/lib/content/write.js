'use strict'

const events = require('events')

const contentPath = require('./path')
const fs = require('fs/promises')
const { moveFile } = require('@npmcli/fs')
const { Minipass } = require('minipass')
const Pipeline = require('minipass-pipeline')
const Flush = require('minipass-flush')
const path = require('path')
const ssri = require('ssri')
const uniqueFilename = require('unique-filename')
const fsm = require('fs-minipass')

module.exports = write

// Cache of move operations in process so we don't duplicate
const moveOperations = new Map()

async function write (cache, data, opts = {}) {
  const { algorithms, size, integrity } = opts

  if (typeof size === 'number' && data.length !== size) {
    throw sizeError(size, data.length)
  }

  const sri = ssri.fromData(data, algorithms ? { algorithms } : {})
  if (integrity && !ssri.checkData(data, integrity, opts)) {
    throw checksumError(integrity, sri)
  }

  for (const algo in sri) {
    const tmp = await makeTmp(cache, opts)
    const hash = sri[algo].toString()
    try {
      await fs.writeFile(tmp.target, data, { flag: 'wx' })
      await moveToDestination(tmp, cache, hash, opts)
    } finally {
      if (!tmp.moved) {
        await fs.rm(tmp.target, { recursive: true, force: true })
      }
    }
  }
  return { integrity: sri, size: data.length }
}

module.exports.stream = writeStream

// writes proxied to the 'inputStream' that is passed to the Promise
// 'end' is deferred until content is handled.
class CacacheWriteStream extends Flush {
  constructor (cache, opts) {
    super()
    this.opts = opts
    this.cache = cache
    this.inputStream = new Minipass()
    this.inputStream.on('error', er => this.emit('error', er))
    this.inputStream.on('drain', () => this.emit('drain'))
    this.handleContentP = null
  }

  write (chunk, encoding, cb) {
    if (!this.handleContentP) {
      this.handleContentP = handleContent(
        this.inputStream,
        this.cache,
        this.opts
      )
      this.handleContentP.catch(error => this.emit('error', error))
    }
    return this.inputStream.write(chunk, encoding, cb)
  }

  flush (cb) {
    this.inputStream.end(() => {
      if (!this.handleContentP) {
        const e = new Error('Cache input stream was empty')
        e.code = 'ENODATA'
        // empty streams are probably emitting end right away.
        // defer this one tick by rejecting a promise on it.
        return Promise.reject(e).catch(cb)
      }
      // eslint-disable-next-line promise/catch-or-return
      this.handleContentP.then(
        (res) => {
          res.integrity && this.emit('integrity', res.integrity)
          // eslint-disable-next-line promise/always-return
          res.size !== null && this.emit('size', res.size)
          cb()
        },
        (er) => cb(er)
      )
    })
  }
}

function writeStream (cache, opts = {}) {
  return new CacacheWriteStream(cache, opts)
}

async function handleContent (inputStream, cache, opts) {
  const tmp = await makeTmp(cache, opts)
  try {
    const res = await pipeToTmp(inputStream, cache, tmp.target, opts)
    await moveToDestination(
      tmp,
      cache,
      res.integrity,
      opts
    )
    return res
  } finally {
    if (!tmp.moved) {
      await fs.rm(tmp.target, { recursive: true, force: true })
    }
  }
}

async function pipeToTmp (inputStream, cache, tmpTarget, opts) {
  const outStream = new fsm.WriteStream(tmpTarget, {
    flags: 'wx',
  })

  if (opts.integrityEmitter) {
    // we need to create these all simultaneously since they can fire in any order
    const [integrity, size] = await Promise.all([
      events.once(opts.integrityEmitter, 'integrity').then(res => res[0]),
      events.once(opts.integrityEmitter, 'size').then(res => res[0]),
      new Pipeline(inputStream, outStream).promise(),
    ])
    return { integrity, size }
  }

  let integrity
  let size
  const hashStream = ssri.integrityStream({
    integrity: opts.integrity,
    algorithms: opts.algorithms,
    size: opts.size,
  })
  hashStream.on('integrity', i => {
    integrity = i
  })
  hashStream.on('size', s => {
    size = s
  })

  const pipeline = new Pipeline(inputStream, hashStream, outStream)
  await pipeline.promise()
  return { integrity, size }
}

async function makeTmp (cache, opts) {
  const tmpTarget = uniqueFilename(path.join(cache, 'tmp'), opts.tmpPrefix)
  await fs.mkdir(path.dirname(tmpTarget), { recursive: true })
  return {
    target: tmpTarget,
    moved: false,
  }
}

async function moveToDestination (tmp, cache, sri) {
  const destination = contentPath(cache, sri)
  const destDir = path.dirname(destination)
  if (moveOperations.has(destination)) {
    return moveOperations.get(destination)
  }
  moveOperations.set(
    destination,
    fs.mkdir(destDir, { recursive: true })
      .then(async () => {
        await moveFile(tmp.target, destination, { overwrite: false })
        tmp.moved = true
        return tmp.moved
      })
      .catch(err => {
        if (!err.message.startsWith('The destination file exists')) {
          throw Object.assign(err, { code: 'EEXIST' })
        }
      }).finally(() => {
        moveOperations.delete(destination)
      })

  )
  return moveOperations.get(destination)
}

function sizeError (expected, found) {
  /* eslint-disable-next-line max-len */
  const err = new Error(`Bad data size: expected inserted data to be ${expected} bytes, but got ${found} instead`)
  err.expected = expected
  err.found = found
  err.code = 'EBADSIZE'
  return err
}

function checksumError (expected, found) {
  const err = new Error(`Integrity check failed:
  Wanted: ${expected}
   Found: ${found}`)
  err.code = 'EINTEGRITY'
  err.expected = expected
  err.found = found
  return err
}
