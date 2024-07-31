const { resolve, basename } = require('node:path')
const { unlink } = require('node:fs/promises')
const { log } = require('proc-log')
const BaseCommand = require('../base-cmd.js')

class Shrinkwrap extends BaseCommand {
  static description = 'Lock down dependency versions for publication'
  static name = 'shrinkwrap'
  static ignoreImplicitWorkspace = false

  async exec () {
    // if has a npm-shrinkwrap.json, nothing to do
    // if has a package-lock.json, rename to npm-shrinkwrap.json
    // if has neither, load the actual tree and save that as npm-shrinkwrap.json
    //
    // loadVirtual, fall back to loadActual
    // rename shrinkwrap file type, and tree.meta.save()
    if (this.npm.global) {
      const er = new Error('`npm shrinkwrap` does not work for global packages')
      er.code = 'ESHRINKWRAPGLOBAL'
      throw er
    }

    const Arborist = require('@npmcli/arborist')
    const path = this.npm.prefix
    const sw = resolve(path, 'npm-shrinkwrap.json')
    const arb = new Arborist({ ...this.npm.flatOptions, path })
    const tree = await arb.loadVirtual().catch(() => arb.loadActual())
    const { meta } = tree
    const newFile = meta.hiddenLockfile || !meta.loadedFromDisk
    const oldFilename = meta.filename
    const notSW = !newFile && basename(oldFilename) !== 'npm-shrinkwrap.json'

    // The computed lockfile version of a hidden lockfile is always 3
    // even if the actual value of the property is a different.
    // When shrinkwrap is run with only a hidden lockfile we want to
    // set the shrinkwrap lockfile version as whatever was explicitly
    // requested with a fallback to the actual value from the hidden
    // lockfile.
    if (meta.hiddenLockfile) {
      meta.lockfileVersion = arb.options.lockfileVersion ||
        meta.originalLockfileVersion
    }
    meta.hiddenLockfile = false
    meta.filename = sw
    await meta.save()

    const updatedVersion = meta.originalLockfileVersion !== meta.lockfileVersion
      ? meta.lockfileVersion
      : null

    if (newFile) {
      let message = 'created a lockfile as npm-shrinkwrap.json'
      if (updatedVersion) {
        message += ` with version ${updatedVersion}`
      }
      log.notice('', message)
    } else if (notSW) {
      await unlink(oldFilename)
      let message = 'package-lock.json has been renamed to npm-shrinkwrap.json'
      if (updatedVersion) {
        message += ` and updated to version ${updatedVersion}`
      }
      log.notice('', message)
    } else if (updatedVersion) {
      log.notice('', `npm-shrinkwrap.json updated to version ${updatedVersion}`)
    } else {
      log.notice('', 'npm-shrinkwrap.json up to date')
    }
  }
}

module.exports = Shrinkwrap
