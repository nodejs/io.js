const { join, sep } = require('path')

const getOptions = require('./common/get-options.js')
const { mkdir, mkdtemp, rm } = require('fs/promises')

// create a temp directory, ensure its permissions match its parent, then call
// the supplied function passing it the path to the directory. clean up after
// the function finishes, whether it throws or not
const withTempDir = async (root, fn, opts) => {
  const options = getOptions(opts, {
    copy: ['tmpPrefix'],
  })
  // create the directory
  await mkdir(root, { recursive: true })

  const target = await mkdtemp(join(`${root}${sep}`, options.tmpPrefix || ''))
  let err
  let result

  try {
    result = await fn(target)
  } catch (_err) {
    err = _err
  }

  try {
    await rm(target, { force: true, recursive: true })
  } catch {
    // ignore errors
  }

  if (err) {
    throw err
  }

  return result
}

module.exports = withTempDir
