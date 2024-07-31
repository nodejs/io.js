'use strict'

const { createWriteStream, promises: fs } = require('graceful-fs')
const os = require('os')
const { backOff } = require('exponential-backoff')
const tar = require('tar')
const path = require('path')
const { Transform, promises: { pipeline } } = require('stream')
const crypto = require('crypto')
const log = require('./log')
const semver = require('semver')
const { download } = require('./download')
const processRelease = require('./process-release')

const win = process.platform === 'win32'

async function install (gyp, argv) {
  log.stdout()
  const release = processRelease(argv, gyp, process.version, process.release)
  // Detecting target_arch based on logic from create-cnfig-gyp.js. Used on Windows only.
  const arch = win ? (gyp.opts.target_arch || gyp.opts.arch || process.arch || 'ia32') : ''
  // Used to prevent downloading tarball if only new node.lib is required on Windows.
  let shouldDownloadTarball = true

  // Determine which node dev files version we are installing
  log.verbose('install', 'input version string %j', release.version)

  if (!release.semver) {
    // could not parse the version string with semver
    throw new Error('Invalid version number: ' + release.version)
  }

  if (semver.lt(release.version, '0.8.0')) {
    throw new Error('Minimum target version is `0.8.0` or greater. Got: ' + release.version)
  }

  // 0.x.y-pre versions are not published yet and cannot be installed. Bail.
  if (release.semver.prerelease[0] === 'pre') {
    log.verbose('detected "pre" node version', release.version)
    if (!gyp.opts.nodedir) {
      throw new Error('"pre" versions of node cannot be installed, use the --nodedir flag instead')
    }
    log.verbose('--nodedir flag was passed; skipping install', gyp.opts.nodedir)
    return
  }

  // flatten version into String
  log.verbose('install', 'installing version: %s', release.versionDir)

  // the directory where the dev files will be installed
  const devDir = path.resolve(gyp.devDir, release.versionDir)

  // If '--ensure' was passed, then don't *always* install the version;
  // check if it is already installed, and only install when needed
  if (gyp.opts.ensure) {
    log.verbose('install', '--ensure was passed, so won\'t reinstall if already installed')
    try {
      await fs.stat(devDir)
    } catch (err) {
      if (err.code === 'ENOENT') {
        log.verbose('install', 'version not already installed, continuing with install', release.version)
        try {
          return await go()
        } catch (err) {
          return rollback(err)
        }
      } else if (err.code === 'EACCES') {
        return eaccesFallback(err)
      }
      throw err
    }
    log.verbose('install', 'version is already installed, need to check "installVersion"')
    const installVersionFile = path.resolve(devDir, 'installVersion')
    let installVersion = 0
    try {
      const ver = await fs.readFile(installVersionFile, 'ascii')
      installVersion = parseInt(ver, 10) || 0
    } catch (err) {
      if (err.code !== 'ENOENT') {
        throw err
      }
    }
    log.verbose('got "installVersion"', installVersion)
    log.verbose('needs "installVersion"', gyp.package.installVersion)
    if (installVersion < gyp.package.installVersion) {
      log.verbose('install', 'version is no good; reinstalling')
      try {
        return await go()
      } catch (err) {
        return rollback(err)
      }
    }
    log.verbose('install', 'version is good')
    if (win) {
      log.verbose('on Windows; need to check node.lib')
      const nodeLibPath = path.resolve(devDir, arch, 'node.lib')
      try {
        await fs.stat(nodeLibPath)
      } catch (err) {
        if (err.code === 'ENOENT') {
          log.verbose('install', `version not already installed for ${arch}, continuing with install`, release.version)
          try {
            shouldDownloadTarball = false
            return await go()
          } catch (err) {
            return rollback(err)
          }
        } else if (err.code === 'EACCES') {
          return eaccesFallback(err)
        }
        throw err
      }
    }
  } else {
    try {
      return await go()
    } catch (err) {
      return rollback(err)
    }
  }

  async function copyDirectory (src, dest) {
    try {
      await fs.stat(src)
    } catch {
      throw new Error(`Missing source directory for copy: ${src}`)
    }
    await fs.mkdir(dest, { recursive: true })
    const entries = await fs.readdir(src, { withFileTypes: true })
    for (const entry of entries) {
      if (entry.isDirectory()) {
        await copyDirectory(path.join(src, entry.name), path.join(dest, entry.name))
      } else if (entry.isFile()) {
        // with parallel installs, copying files may cause file errors on
        // Windows so use an exponential backoff to resolve collisions
        await backOff(async () => {
          try {
            await fs.copyFile(path.join(src, entry.name), path.join(dest, entry.name))
          } catch (err) {
            // if ensure, check if file already exists and that's good enough
            if (gyp.opts.ensure && err.code === 'EBUSY') {
              try {
                await fs.stat(path.join(dest, entry.name))
                return
              } catch {}
            }
            throw err
          }
        })
      } else {
        throw new Error('Unexpected file directory entry type')
      }
    }
  }

  async function go () {
    log.verbose('ensuring devDir is created', devDir)

    // first create the dir for the node dev files
    try {
      const created = await fs.mkdir(devDir, { recursive: true })

      if (created) {
        log.verbose('created devDir', created)
      }
    } catch (err) {
      if (err.code === 'EACCES') {
        return eaccesFallback(err)
      }

      throw err
    }

    // now download the node tarball
    const tarPath = gyp.opts.tarball
    let extractErrors = false
    let extractCount = 0
    const contentShasums = {}
    const expectShasums = {}

    // checks if a file to be extracted from the tarball is valid.
    // only .h header files and the gyp files get extracted
    function isValid (path) {
      const isValid = valid(path)
      if (isValid) {
        log.verbose('extracted file from tarball', path)
        extractCount++
      } else {
        // invalid
        log.silly('ignoring from tarball', path)
      }
      return isValid
    }

    function onwarn (code, message) {
      extractErrors = true
      log.error('error while extracting tarball', code, message)
    }

    // download the tarball and extract!
    // Ommited on Windows if only new node.lib is required

    // on Windows there can be file errors from tar if parallel installs
    // are happening (not uncommon with multiple native modules) so
    // extract the tarball to a temp directory first and then copy over
    const tarExtractDir = win ? await fs.mkdtemp(path.join(os.tmpdir(), 'node-gyp-tmp-')) : devDir

    try {
      if (shouldDownloadTarball) {
        if (tarPath) {
          await tar.extract({
            file: tarPath,
            strip: 1,
            filter: isValid,
            onwarn,
            cwd: tarExtractDir
          })
        } else {
          try {
            const res = await download(gyp, release.tarballUrl)

            if (res.status !== 200) {
              throw new Error(`${res.status} response downloading ${release.tarballUrl}`)
            }

            await pipeline(
              res.body,
              // content checksum
              new ShaSum((_, checksum) => {
                const filename = path.basename(release.tarballUrl).trim()
                contentShasums[filename] = checksum
                log.verbose('content checksum', filename, checksum)
              }),
              tar.extract({
                strip: 1,
                cwd: tarExtractDir,
                filter: isValid,
                onwarn
              })
            )
          } catch (err) {
          // something went wrong downloading the tarball?
            if (err.code === 'ENOTFOUND') {
              throw new Error('This is most likely not a problem with node-gyp or the package itself and\n' +
              'is related to network connectivity. In most cases you are behind a proxy or have bad \n' +
              'network settings.')
            }
            throw err
          }
        }

        // invoked after the tarball has finished being extracted
        if (extractErrors || extractCount === 0) {
          throw new Error('There was a fatal problem while downloading/extracting the tarball')
        }

        log.verbose('tarball', 'done parsing tarball')
      }

      const installVersionPath = path.resolve(tarExtractDir, 'installVersion')
      await Promise.all([
      // need to download node.lib
        ...(win ? [downloadNodeLib()] : []),
        // write the "installVersion" file
        fs.writeFile(installVersionPath, gyp.package.installVersion + '\n'),
        // Only download SHASUMS.txt if we downloaded something in need of SHA verification
        ...(!tarPath || win ? [downloadShasums()] : [])
      ])

      log.verbose('download contents checksum', JSON.stringify(contentShasums))
      // check content shasums
      for (const k in contentShasums) {
        log.verbose('validating download checksum for ' + k, '(%s == %s)', contentShasums[k], expectShasums[k])
        if (contentShasums[k] !== expectShasums[k]) {
          throw new Error(k + ' local checksum ' + contentShasums[k] + ' not match remote ' + expectShasums[k])
        }
      }

      // copy over the files from the temp tarball extract directory to devDir
      if (tarExtractDir !== devDir) {
        await copyDirectory(tarExtractDir, devDir)
      }
    } finally {
      if (tarExtractDir !== devDir) {
        try {
          // try to cleanup temp dir
          await fs.rm(tarExtractDir, { recursive: true })
        } catch {
          log.warn('failed to clean up temp tarball extract directory')
        }
      }
    }

    async function downloadShasums () {
      log.verbose('check download content checksum, need to download `SHASUMS256.txt`...')
      log.verbose('checksum url', release.shasumsUrl)

      const res = await download(gyp, release.shasumsUrl)

      if (res.status !== 200) {
        throw new Error(`${res.status}  status code downloading checksum`)
      }

      for (const line of (await res.text()).trim().split('\n')) {
        const items = line.trim().split(/\s+/)
        if (items.length !== 2) {
          return
        }

        // 0035d18e2dcf9aad669b1c7c07319e17abfe3762  ./node-v0.11.4.tar.gz
        const name = items[1].replace(/^\.\//, '')
        expectShasums[name] = items[0]
      }

      log.verbose('checksum data', JSON.stringify(expectShasums))
    }

    async function downloadNodeLib () {
      log.verbose('on Windows; need to download `' + release.name + '.lib`...')
      const dir = path.resolve(tarExtractDir, arch)
      const targetLibPath = path.resolve(dir, release.name + '.lib')
      const { libUrl, libPath } = release[arch]
      const name = `${arch} ${release.name}.lib`
      log.verbose(name, 'dir', dir)
      log.verbose(name, 'url', libUrl)

      await fs.mkdir(dir, { recursive: true })
      log.verbose('streaming', name, 'to:', targetLibPath)

      const res = await download(gyp, libUrl)

      // Since only required node.lib is downloaded throw error if it is not fetched
      if (res.status !== 200) {
        throw new Error(`${res.status} status code downloading ${name}`)
      }

      return pipeline(
        res.body,
        new ShaSum((_, checksum) => {
          contentShasums[libPath] = checksum
          log.verbose('content checksum', libPath, checksum)
        }),
        createWriteStream(targetLibPath)
      )
    } // downloadNodeLib()
  } // go()

  /**
   * Checks if a given filename is "valid" for this installation.
   */

  function valid (file) {
    // header files
    const extname = path.extname(file)
    return extname === '.h' || extname === '.gypi'
  }

  async function rollback (err) {
    log.warn('install', 'got an error, rolling back install')
    // roll-back the install if anything went wrong
    await gyp.commands.remove([release.versionDir])
    throw err
  }

  /**
   * The EACCES fallback is a workaround for npm's `sudo` behavior, where
   * it drops the permissions before invoking any child processes (like
   * node-gyp). So what happens is the "nobody" user doesn't have
   * permission to create the dev dir. As a fallback, make the tmpdir() be
   * the dev dir for this installation. This is not ideal, but at least
   * the compilation will succeed...
   */

  async function eaccesFallback (err) {
    const noretry = '--node_gyp_internal_noretry'
    if (argv.indexOf(noretry) !== -1) {
      throw err
    }
    const tmpdir = os.tmpdir()
    gyp.devDir = path.resolve(tmpdir, '.node-gyp')
    let userString = ''
    try {
      // os.userInfo can fail on some systems, it's not critical here
      userString = ` ("${os.userInfo().username}")`
    } catch (e) {}
    log.warn('EACCES', 'current user%s does not have permission to access the dev dir "%s"', userString, devDir)
    log.warn('EACCES', 'attempting to reinstall using temporary dev dir "%s"', gyp.devDir)
    if (process.cwd() === tmpdir) {
      log.verbose('tmpdir == cwd', 'automatically will remove dev files after to save disk space')
      gyp.todo.push({ name: 'remove', args: argv })
    }
    return gyp.commands.install([noretry].concat(argv))
  }
}

class ShaSum extends Transform {
  constructor (callback) {
    super()
    this._callback = callback
    this._digester = crypto.createHash('sha256')
  }

  _transform (chunk, _, callback) {
    this._digester.update(chunk)
    callback(null, chunk)
  }

  _flush (callback) {
    this._callback(null, this._digester.digest('hex'))
    callback()
  }
}

module.exports = install
module.exports.usage = 'Install node development files for the specified node version.'
