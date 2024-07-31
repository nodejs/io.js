// not an airtight indicator, but a good gut-check to even bother trying
const { stat } = require('fs/promises')
module.exports = ({ cwd = process.cwd() } = {}) =>
  stat(cwd + '/.git').then(() => true, () => false)
