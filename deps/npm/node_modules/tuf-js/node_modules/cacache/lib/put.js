'use strict'

const index = require('./entry-index')
const memo = require('./memoization')
const write = require('./content/write')
const Flush = require('minipass-flush')
const { PassThrough } = require('minipass-collect')
const Pipeline = require('minipass-pipeline')

const putOpts = (opts) => ({
  algorithms: ['sha512'],
  ...opts,
})

module.exports = putData

async function putData (cache, key, data, opts = {}) {
  const { memoize } = opts
  opts = putOpts(opts)
  const res = await write(cache, data, opts)
  const entry = await index.insert(cache, key, res.integrity, { ...opts, size: res.size })
  if (memoize) {
    memo.put(cache, entry, data, opts)
  }

  return res.integrity
}

module.exports.stream = putStream

function putStream (cache, key, opts = {}) {
  const { memoize } = opts
  opts = putOpts(opts)
  let integrity
  let size
  let error

  let memoData
  const pipeline = new Pipeline()
  // first item in the pipeline is the memoizer, because we need
  // that to end first and get the collected data.
  if (memoize) {
    const memoizer = new PassThrough().on('collect', data => {
      memoData = data
    })
    pipeline.push(memoizer)
  }

  // contentStream is a write-only, not a passthrough
  // no data comes out of it.
  const contentStream = write.stream(cache, opts)
    .on('integrity', (int) => {
      integrity = int
    })
    .on('size', (s) => {
      size = s
    })
    .on('error', (err) => {
      error = err
    })

  pipeline.push(contentStream)

  // last but not least, we write the index and emit hash and size,
  // and memoize if we're doing that
  pipeline.push(new Flush({
    async flush () {
      if (!error) {
        const entry = await index.insert(cache, key, integrity, { ...opts, size })
        if (memoize && memoData) {
          memo.put(cache, entry, memoData, opts)
        }
        pipeline.emit('integrity', integrity)
        pipeline.emit('size', size)
      }
    },
  }))

  return pipeline
}
