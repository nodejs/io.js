// can't use read-package-json-fast, because we want to ensure
// that we make as few changes as possible, even for safety issues.
const { readFile } = require('node:fs/promises')
const parse = require('json-parse-even-better-errors')

module.exports = async path => parse(await readFile(path))
