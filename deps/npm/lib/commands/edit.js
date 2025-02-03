const { resolve } = require('node:path')
const { lstat } = require('node:fs/promises')
const cp = require('node:child_process')
const completion = require('../utils/installed-shallow.js')
const BaseCommand = require('../base-cmd.js')

const splitPackageNames = (path) => path.split('/')
// combine scoped parts
  .reduce((parts, part) => {
    if (parts.length === 0) {
      return [part]
    }

    const lastPart = parts[parts.length - 1]
    // check if previous part is the first part of a scoped package
    if (lastPart[0] === '@' && !lastPart.includes('/')) {
      parts[parts.length - 1] += '/' + part
    } else {
      parts.push(part)
    }

    return parts
  }, [])
  .join('/node_modules/')
  .replace(/(\/node_modules)+/, '/node_modules')

// npm edit <pkg>
// open the package folder in the $EDITOR
class Edit extends BaseCommand {
  static description = 'Edit an installed package'
  static name = 'edit'
  static usage = ['<pkg>[/<subpkg>...]']
  static params = ['editor']
  static ignoreImplicitWorkspace = false

  // TODO
  /* istanbul ignore next */
  static async completion (opts, npm) {
    return completion(npm, opts)
  }

  async exec (args) {
    if (args.length !== 1) {
      throw this.usageError()
    }

    const path = splitPackageNames(args[0])
    const dir = resolve(this.npm.dir, path)

    await lstat(dir)
    await new Promise((res, rej) => {
      const [bin, ...spawnArgs] = this.npm.config.get('editor').split(/\s+/)
      const editor = cp.spawn(bin, [...spawnArgs, dir], { stdio: 'inherit' })
      editor.on('exit', async (code) => {
        if (code) {
          return rej(new Error(`editor process exited with code: ${code}`))
        }
        await this.npm.exec('rebuild', [dir]).then(res).catch(rej)
      })
    })
  }
}

module.exports = Edit
