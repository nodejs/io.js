const { inspect } = require('node:util')
const log = require('./logging.js')

module.exports = tree => log.info(inspect(tree.toJSON(), { depth: Infinity }))
