const { basename, extname } = require('node:path')

// we should try to print patches as long as the
// extension is not identified as binary files
const shouldPrintPatch = async (path, opts = {}) => {
  if (opts.diffText) {
    return true
  }

  const { default: binaryExtensions } = await import('binary-extensions')

  const filename = basename(path)
  const extension = (
    filename.startsWith('.')
      ? filename
      : extname(filename)
  ).slice(1)

  return !binaryExtensions.includes(extension)
}

module.exports = shouldPrintPatch
