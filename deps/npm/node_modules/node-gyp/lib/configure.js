'use strict'

const { promises: fs, readFileSync } = require('graceful-fs')
const path = require('path')
const log = require('./log')
const os = require('os')
const processRelease = require('./process-release')
const win = process.platform === 'win32'
const findNodeDirectory = require('./find-node-directory')
const { createConfigGypi } = require('./create-config-gypi')
const { format: msgFormat } = require('util')
const { findAccessibleSync } = require('./util')
const { findPython } = require('./find-python')
const { findVisualStudio } = win ? require('./find-visualstudio') : {}

const majorRe = /^#define NODE_MAJOR_VERSION (\d+)/m
const minorRe = /^#define NODE_MINOR_VERSION (\d+)/m
const patchRe = /^#define NODE_PATCH_VERSION (\d+)/m

async function configure (gyp, argv) {
  const buildDir = path.resolve('build')
  const configNames = ['config.gypi', 'common.gypi']
  const configs = []
  let nodeDir
  const release = processRelease(argv, gyp, process.version, process.release)

  const python = await findPython(gyp.opts.python)
  return getNodeDir()

  async function getNodeDir () {
    // 'python' should be set by now
    process.env.PYTHON = python

    if (!gyp.opts.nodedir &&
        process.config.variables.use_prefix_to_find_headers) {
      // check if the headers can be found using the prefix specified
      // at build time. Use them if they match the version expected
      const prefix = process.config.variables.node_prefix
      let availVersion
      try {
        const nodeVersionH = readFileSync(path.join(prefix,
          'include', 'node', 'node_version.h'), { encoding: 'utf8' })
        const major = nodeVersionH.match(majorRe)[1]
        const minor = nodeVersionH.match(minorRe)[1]
        const patch = nodeVersionH.match(patchRe)[1]
        availVersion = major + '.' + minor + '.' + patch
      } catch {}
      if (availVersion === release.version) {
        // ok version matches, use the headers
        gyp.opts.nodedir = prefix
        log.verbose('using local node headers based on prefix',
          'setting nodedir to ' + gyp.opts.nodedir)
      }
    }

    if (gyp.opts.nodedir) {
      // --nodedir was specified. use that for the dev files
      nodeDir = gyp.opts.nodedir.replace(/^~/, os.homedir())
      log.verbose('get node dir', 'compiling against specified --nodedir dev files: %s', nodeDir)
    } else {
      // if no --nodedir specified, ensure node dependencies are installed
      if ('v' + release.version !== process.version) {
        // if --target was given, then determine a target version to compile for
        log.verbose('get node dir', 'compiling against --target node version: %s', release.version)
      } else {
        // if no --target was specified then use the current host node version
        log.verbose('get node dir', 'no --target version specified, falling back to host node version: %s', release.version)
      }

      if (!release.semver) {
        // could not parse the version string with semver
        throw new Error('Invalid version number: ' + release.version)
      }

      // If the tarball option is set, always remove and reinstall the headers
      // into devdir. Otherwise only install if they're not already there.
      gyp.opts.ensure = !gyp.opts.tarball

      await gyp.commands.install([release.version])

      log.verbose('get node dir', 'target node version installed:', release.versionDir)
      nodeDir = path.resolve(gyp.devDir, release.versionDir)
    }

    return createBuildDir()
  }

  async function createBuildDir () {
    log.verbose('build dir', 'attempting to create "build" dir: %s', buildDir)

    const isNew = await fs.mkdir(buildDir, { recursive: true })
    log.verbose(
      'build dir', '"build" dir needed to be created?', isNew ? 'Yes' : 'No'
    )
    if (win) {
      let usingMakeGenerator = false
      for (let i = argv.length - 1; i >= 0; --i) {
        const arg = argv[i]
        if (arg === '-f' || arg === '--format') {
          const format = argv[i + 1]
          if (typeof format === 'string' && format.startsWith('make')) {
            usingMakeGenerator = true
            break
          }
        } else if (arg.startsWith('--format=make')) {
          usingMakeGenerator = true
          break
        }
      }
      let vsInfo = {}
      if (!usingMakeGenerator) {
        vsInfo = await findVisualStudio(release.semver, gyp.opts['msvs-version'])
      }
      return createConfigFile(vsInfo)
    }
    return createConfigFile(null)
  }

  async function createConfigFile (vsInfo) {
    if (win) {
      process.env.GYP_MSVS_VERSION = Math.min(vsInfo.versionYear, 2015)
      process.env.GYP_MSVS_OVERRIDE_PATH = vsInfo.path
    }
    const configPath = await createConfigGypi({ gyp, buildDir, nodeDir, vsInfo, python })
    configs.push(configPath)
    return findConfigs()
  }

  async function findConfigs () {
    const name = configNames.shift()
    if (!name) {
      return runGyp()
    }

    const fullPath = path.resolve(name)
    log.verbose(name, 'checking for gypi file: %s', fullPath)
    try {
      await fs.stat(fullPath)
      log.verbose(name, 'found gypi file')
      configs.push(fullPath)
    } catch (err) {
      // ENOENT will check next gypi filename
      if (err.code !== 'ENOENT') {
        throw err
      }
    }

    return findConfigs()
  }

  async function runGyp () {
    if (!~argv.indexOf('-f') && !~argv.indexOf('--format')) {
      if (win) {
        log.verbose('gyp', 'gyp format was not specified; forcing "msvs"')
        // force the 'make' target for non-Windows
        argv.push('-f', 'msvs')
      } else {
        log.verbose('gyp', 'gyp format was not specified; forcing "make"')
        // force the 'make' target for non-Windows
        argv.push('-f', 'make')
      }
    }

    // include all the ".gypi" files that were found
    configs.forEach(function (config) {
      argv.push('-I', config)
    })

    // For AIX and z/OS we need to set up the path to the exports file
    // which contains the symbols needed for linking.
    let nodeExpFile
    let nodeRootDir
    let candidates
    let logprefix = 'find exports file'
    if (process.platform === 'aix' || process.platform === 'os390' || process.platform === 'os400') {
      const ext = process.platform === 'os390' ? 'x' : 'exp'
      nodeRootDir = findNodeDirectory()

      if (process.platform === 'aix' || process.platform === 'os400') {
        candidates = [
          'include/node/node',
          'out/Release/node',
          'out/Debug/node',
          'node'
        ].map(function (file) {
          return file + '.' + ext
        })
      } else {
        candidates = [
          'out/Release/lib.target/libnode',
          'out/Debug/lib.target/libnode',
          'out/Release/obj.target/libnode',
          'out/Debug/obj.target/libnode',
          'lib/libnode'
        ].map(function (file) {
          return file + '.' + ext
        })
      }

      nodeExpFile = findAccessibleSync(logprefix, nodeRootDir, candidates)
      if (nodeExpFile !== undefined) {
        log.verbose(logprefix, 'Found exports file: %s', nodeExpFile)
      } else {
        const msg = msgFormat('Could not find node.%s file in %s', ext, nodeRootDir)
        log.error(logprefix, 'Could not find exports file')
        throw new Error(msg)
      }
    }

    // For z/OS we need to set up the path to zoslib include directory,
    // which contains headers included in v8config.h.
    let zoslibIncDir
    if (process.platform === 'os390') {
      logprefix = "find zoslib's zos-base.h:"
      let msg
      let zoslibIncPath = process.env.ZOSLIB_INCLUDES
      if (zoslibIncPath) {
        zoslibIncPath = findAccessibleSync(logprefix, zoslibIncPath, ['zos-base.h'])
        if (zoslibIncPath === undefined) {
          msg = msgFormat('Could not find zos-base.h file in the directory set ' +
                          'in ZOSLIB_INCLUDES environment variable: %s; set it ' +
                          'to the correct path, or unset it to search %s', process.env.ZOSLIB_INCLUDES, nodeRootDir)
        }
      } else {
        candidates = [
          'include/node/zoslib/zos-base.h',
          'include/zoslib/zos-base.h',
          'zoslib/include/zos-base.h',
          'install/include/node/zoslib/zos-base.h'
        ]
        zoslibIncPath = findAccessibleSync(logprefix, nodeRootDir, candidates)
        if (zoslibIncPath === undefined) {
          msg = msgFormat('Could not find any of %s in directory %s; set ' +
                          'environmant variable ZOSLIB_INCLUDES to the path ' +
                          'that contains zos-base.h', candidates.toString(), nodeRootDir)
        }
      }
      if (zoslibIncPath !== undefined) {
        zoslibIncDir = path.dirname(zoslibIncPath)
        log.verbose(logprefix, "Found zoslib's zos-base.h in: %s", zoslibIncDir)
      } else if (release.version.split('.')[0] >= 16) {
        // zoslib is only shipped in Node v16 and above.
        log.error(logprefix, msg)
        throw new Error(msg)
      }
    }

    // this logic ported from the old `gyp_addon` python file
    const gypScript = path.resolve(__dirname, '..', 'gyp', 'gyp_main.py')
    const addonGypi = path.resolve(__dirname, '..', 'addon.gypi')
    let commonGypi = path.resolve(nodeDir, 'include/node/common.gypi')
    try {
      await fs.stat(commonGypi)
    } catch (err) {
      commonGypi = path.resolve(nodeDir, 'common.gypi')
    }

    let outputDir = 'build'
    if (win) {
      // Windows expects an absolute path
      outputDir = buildDir
    }
    const nodeGypDir = path.resolve(__dirname, '..')

    let nodeLibFile = path.join(nodeDir,
      !gyp.opts.nodedir ? '<(target_arch)' : '$(Configuration)',
      release.name + '.lib')

    argv.push('-I', addonGypi)
    argv.push('-I', commonGypi)
    argv.push('-Dlibrary=shared_library')
    argv.push('-Dvisibility=default')
    argv.push('-Dnode_root_dir=' + nodeDir)
    if (process.platform === 'aix' || process.platform === 'os390' || process.platform === 'os400') {
      argv.push('-Dnode_exp_file=' + nodeExpFile)
      if (process.platform === 'os390' && zoslibIncDir) {
        argv.push('-Dzoslib_include_dir=' + zoslibIncDir)
      }
    }
    argv.push('-Dnode_gyp_dir=' + nodeGypDir)

    // Do this to keep Cygwin environments happy, else the unescaped '\' gets eaten up,
    // resulting in bad paths, Ex c:parentFolderfolderanotherFolder instead of c:\parentFolder\folder\anotherFolder
    if (win) {
      nodeLibFile = nodeLibFile.replace(/\\/g, '\\\\')
    }
    argv.push('-Dnode_lib_file=' + nodeLibFile)
    argv.push('-Dmodule_root_dir=' + process.cwd())
    argv.push('-Dnode_engine=' +
        (gyp.opts.node_engine || process.jsEngine || 'v8'))
    argv.push('--depth=.')
    argv.push('--no-parallel')

    // tell gyp to write the Makefile/Solution files into output_dir
    argv.push('--generator-output', outputDir)

    // tell make to write its output into the same dir
    argv.push('-Goutput_dir=.')

    // enforce use of the "binding.gyp" file
    argv.unshift('binding.gyp')

    // execute `gyp` from the current target nodedir
    argv.unshift(gypScript)

    // make sure python uses files that came with this particular node package
    const pypath = [path.join(__dirname, '..', 'gyp', 'pylib')]
    if (process.env.PYTHONPATH) {
      pypath.push(process.env.PYTHONPATH)
    }
    process.env.PYTHONPATH = pypath.join(win ? ';' : ':')

    await new Promise((resolve, reject) => {
      const cp = gyp.spawn(python, argv)
      cp.on('exit', (code) => {
        if (code !== 0) {
          reject(new Error('`gyp` failed with exit code: ' + code))
        } else {
          // we're done
          resolve()
        }
      })
    })
  }
}

module.exports = configure
module.exports.usage = 'Generates ' + (win ? 'MSVC project files' : 'a Makefile') + ' for the current module'
