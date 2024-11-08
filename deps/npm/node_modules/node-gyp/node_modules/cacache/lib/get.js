'use strict'

const Collect = require('minipass-collect')
const { Minipass } = require('minipass')
const Pipeline = require('minipass-pipeline')

const index = require('./entry-index')
const memo = require('./memoization')
const read = require('./content/read')

async function getData (cache, key, opts = {}) {
  const { integrity, memoize, size } = opts
  const memoized = memo.get(cache, key, opts)
  if (memoized && memoize !== false) {
    return {
      metadata: memoized.entry.metadata,
      data: memoized.data,
      integrity: memoized.entry.integrity,
      size: memoized.entry.size,
    }
  }

  const entry = await index.find(cache, key, opts)
  if (!entry) {
    throw new index.NotFoundError(cache, key)
  }
  const data = await read(cache, entry.integrity, { integrity, size })
  if (memoize) {
    memo.put(cache, entry, data, opts)
  }

  return {
    data,
    metadata: entry.metadata,
    size: entry.size,
    integrity: entry.integrity,
  }
}
module.exports = getData

async function getDataByDigest (cache, key, opts = {}) {
  const { integrity, memoize, size } = opts
  const memoized = memo.get.byDigest(cache, key, opts)
  if (memoized && memoize !== false) {
    return memoized
  }

  const res = await read(cache, key, { integrity, size })
  if (memoize) {
    memo.put.byDigest(cache, key, res, opts)
  }
  return res
}
module.exports.byDigest = getDataByDigest

const getMemoizedStream = (memoized) => {
  const stream = new Minipass()
  stream.on('newListener', function (ev, cb) {
    ev === 'metadata' && cb(memoized.entry.metadata)
    ev === 'integrity' && cb(memoized.entry.integrity)
    ev === 'size' && cb(memoized.entry.size)
  })
  stream.end(memoized.data)
  return stream
}

function getStream (cache, key, opts = {}) {
  const { memoize, size } = opts
  const memoized = memo.get(cache, key, opts)
  if (memoized && memoize !== false) {
    return getMemoizedStream(memoized)
  }

  const stream = new Pipeline()
  // Set all this up to run on the stream and then just return the stream
  Promise.resolve().then(async () => {
    const entry = await index.find(cache, key)
    if (!entry) {
      throw new index.NotFoundError(cache, key)
    }

    stream.emit('metadata', entry.metadata)
    stream.emit('integrity', entry.integrity)
    stream.emit('size', entry.size)
    stream.on('newListener', function (ev, cb) {
      ev === 'metadata' && cb(entry.metadata)
      ev === 'integrity' && cb(entry.integrity)
      ev === 'size' && cb(entry.size)
    })

    const src = read.readStream(
      cache,
      entry.integrity,
      { ...opts, size: typeof size !== 'number' ? entry.size : size }
    )

    if (memoize) {
      const memoStream = new Collect.PassThrough()
      memoStream.on('collect', data => memo.put(cache, entry, data, opts))
      stream.unshift(memoStream)
    }
    stream.unshift(src)
    return stream
  }).catch((err) => stream.emit('error', err))

  return stream
}

module.exports.stream = getStream

function getStreamDigest (cache, integrity, opts = {}) {
  const { memoize } = opts
  const memoized = memo.get.byDigest(cache, integrity, opts)
  if (memoized && memoize !== false) {
    const stream = new Minipass()
    stream.end(memoized)
    return stream
  } else {
    const stream = read.readStream(cache, integrity, opts)
    if (!memoize) {
      return stream
    }

    const memoStream = new Collect.PassThrough()
    memoStream.on('collect', data => memo.put.byDigest(
      cache,
      integrity,
      data,
      opts
    ))
    return new Pipeline(stream, memoStream)
  }
}

module.exports.stream.byDigest = getStreamDigest

function info (cache, key, opts = {}) {
  const { memoize } = opts
  const memoized = memo.get(cache, key, opts)
  if (memoized && memoize !== false) {
    return Promise.resolve(memoized.entry)
  } else {
    return index.find(cache, key)
  }
}
module.exports.info = info

async function copy (cache, key, dest, opts = {}) {
  const entry = await index.find(cache, key, opts)
  if (!entry) {
    throw new index.NotFoundError(cache, key)
  }
  await read.copy(cache, entry.integrity, dest, opts)
  return {
    metadata: entry.metadata,
    size: entry.size,
    integrity: entry.integrity,
  }
}

module.exports.copy = copy

async function copyByDigest (cache, key, dest, opts = {}) {
  await read.copy(cache, key, dest, opts)
  return key
}

module.exports.copy.byDigest = copyByDigest

module.exports.hasContent = read.hasContent
