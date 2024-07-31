// write the json back, preserving the line breaks and indent
const { writeFile } = require('node:fs/promises')
const kIndent = Symbol.for('indent')
const kNewline = Symbol.for('newline')

module.exports = async (path, pkg) => {
  const {
    [kIndent]: indent = 2,
    [kNewline]: newline = '\n',
  } = pkg
  delete pkg._id
  const raw = JSON.stringify(pkg, null, indent) + '\n'
  const data = newline === '\n' ? raw : raw.split('\n').join(newline)
  return writeFile(path, data)
}
