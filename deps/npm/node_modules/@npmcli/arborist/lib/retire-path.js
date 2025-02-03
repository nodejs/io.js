const crypto = require('node:crypto')
const { dirname, basename, resolve } = require('node:path')

// use sha1 because it's faster, and collisions extremely unlikely anyway
const pathSafeHash = s =>
  crypto.createHash('sha1')
    .update(s)
    .digest('base64')
    .replace(/[^a-zA-Z0-9]+/g, '')
    .slice(0, 8)

const retirePath = from => {
  const d = dirname(from)
  const b = basename(from)
  const hash = pathSafeHash(from)
  return resolve(d, `.${b}-${hash}`)
}

module.exports = retirePath
