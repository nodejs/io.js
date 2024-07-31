const { URL } = require('node:url')

/**
 * Maps a URL to an identifier.
 *
 * Name courtesy schiffertronix media LLC, a New Jersey corporation
 *
 * @param {String} uri The URL to be nerfed.
 *
 * @returns {String} A nerfed URL.
 */
module.exports = (url) => {
  const parsed = new URL(url)
  const from = `${parsed.protocol}//${parsed.host}${parsed.pathname}`
  const rel = new URL('.', from)
  const res = `//${rel.host}${rel.pathname}`
  return res
}
