const { resolve } = require('node:path')
const { stat } = require('node:fs/promises')
const { walkUp } = require('walk-up-path')

const fileExists = async (file) => {
  try {
    const res = await stat(file)
    return res.isFile()
  } catch {
    return false
  }
}

const localFileExists = async (dir, binName, root) => {
  for (const path of walkUp(dir)) {
    const binDir = resolve(path, 'node_modules', '.bin')

    if (await fileExists(resolve(binDir, binName))) {
      return binDir
    }

    if (path.toLowerCase() === resolve(root).toLowerCase()) {
      return false
    }
  }

  return false
}

module.exports = {
  fileExists,
  localFileExists,
}
