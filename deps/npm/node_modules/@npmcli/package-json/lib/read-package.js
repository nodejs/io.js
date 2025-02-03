// This is JUST the code needed to open a package.json file and parse it.
// It's isolated out so that code needing to parse a package.json file can do so in the same way as this module does, without needing to require the whole module, or needing to require the underlying parsing library.

const { readFile } = require('fs/promises')
const parseJSON = require('json-parse-even-better-errors')

async function read (filename) {
  try {
    const data = await readFile(filename, 'utf8')
    return data
  } catch (err) {
    err.message = `Could not read package.json: ${err}`
    throw err
  }
}

function parse (data) {
  try {
    const content = parseJSON(data)
    return content
  } catch (err) {
    err.message = `Invalid package.json: ${err}`
    throw err
  }
}

// This is what most external libs will use.
// PackageJson will call read and parse separately
async function readPackage (filename) {
  const data = await read(filename)
  const content = parse(data)
  return content
}

module.exports = {
  read,
  parse,
  readPackage,
}
