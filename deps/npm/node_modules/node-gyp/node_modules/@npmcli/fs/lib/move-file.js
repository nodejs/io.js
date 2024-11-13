const { dirname, join, resolve, relative, isAbsolute } = require('path')
const fs = require('fs/promises')

const pathExists = async path => {
  try {
    await fs.access(path)
    return true
  } catch (er) {
    return er.code !== 'ENOENT'
  }
}

const moveFile = async (source, destination, options = {}, root = true, symlinks = []) => {
  if (!source || !destination) {
    throw new TypeError('`source` and `destination` file required')
  }

  options = {
    overwrite: true,
    ...options,
  }

  if (!options.overwrite && await pathExists(destination)) {
    throw new Error(`The destination file exists: ${destination}`)
  }

  await fs.mkdir(dirname(destination), { recursive: true })

  try {
    await fs.rename(source, destination)
  } catch (error) {
    if (error.code === 'EXDEV' || error.code === 'EPERM') {
      const sourceStat = await fs.lstat(source)
      if (sourceStat.isDirectory()) {
        const files = await fs.readdir(source)
        await Promise.all(files.map((file) =>
          moveFile(join(source, file), join(destination, file), options, false, symlinks)
        ))
      } else if (sourceStat.isSymbolicLink()) {
        symlinks.push({ source, destination })
      } else {
        await fs.copyFile(source, destination)
      }
    } else {
      throw error
    }
  }

  if (root) {
    await Promise.all(symlinks.map(async ({ source: symSource, destination: symDestination }) => {
      let target = await fs.readlink(symSource)
      // junction symlinks in windows will be absolute paths, so we need to
      // make sure they point to the symlink destination
      if (isAbsolute(target)) {
        target = resolve(symDestination, relative(symSource, target))
      }
      // try to determine what the actual file is so we can create the correct
      // type of symlink in windows
      let targetStat = 'file'
      try {
        targetStat = await fs.stat(resolve(dirname(symSource), target))
        if (targetStat.isDirectory()) {
          targetStat = 'junction'
        }
      } catch {
        // targetStat remains 'file'
      }
      await fs.symlink(
        target,
        symDestination,
        targetStat
      )
    }))
    await fs.rm(source, { recursive: true, force: true })
  }
}

module.exports = moveFile
