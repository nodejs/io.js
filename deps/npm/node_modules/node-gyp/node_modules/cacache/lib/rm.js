'use strict'

const { rm } = require('fs/promises')
const glob = require('./util/glob.js')
const index = require('./entry-index')
const memo = require('./memoization')
const path = require('path')
const rmContent = require('./content/rm')

module.exports = entry
module.exports.entry = entry

function entry (cache, key, opts) {
  memo.clearMemoized()
  return index.delete(cache, key, opts)
}

module.exports.content = content

function content (cache, integrity) {
  memo.clearMemoized()
  return rmContent(cache, integrity)
}

module.exports.all = all

async function all (cache) {
  memo.clearMemoized()
  const paths = await glob(path.join(cache, '*(content-*|index-*)'), { silent: true, nosort: true })
  return Promise.all(paths.map((p) => rm(p, { recursive: true, force: true })))
}
