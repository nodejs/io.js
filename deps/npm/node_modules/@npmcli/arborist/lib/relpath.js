const { relative } = require('node:path')
const relpath = (from, to) => relative(from, to).replace(/\\/g, '/')
module.exports = relpath
