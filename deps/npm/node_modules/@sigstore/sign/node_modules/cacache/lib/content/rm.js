'use strict'

const fs = require('fs/promises')
const contentPath = require('./path')
const { hasContent } = require('./read')

module.exports = rm

async function rm (cache, integrity) {
  const content = await hasContent(cache, integrity)
  // ~pretty~ sure we can't end up with a content lacking sri, but be safe
  if (content && content.sri) {
    await fs.rm(contentPath(cache, content.sri), { recursive: true, force: true })
    return true
  } else {
    return false
  }
}
