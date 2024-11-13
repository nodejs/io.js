'use strict'

const { mkdir } = require('node:fs/promises')
const Arborist = require('@npmcli/arborist')
const ciInfo = require('ci-info')
const crypto = require('node:crypto')
const { log, input } = require('proc-log')
const npa = require('npm-package-arg')
const pacote = require('pacote')
const { read } = require('read')
const semver = require('semver')
const { fileExists, localFileExists } = require('./file-exists.js')
const getBinFromManifest = require('./get-bin-from-manifest.js')
const noTTY = require('./no-tty.js')
const runScript = require('./run-script.js')
const isWindows = require('./is-windows.js')
const { dirname, resolve } = require('node:path')

const binPaths = []

// when checking the local tree we look up manifests, cache those results by
// spec.raw so we don't have to fetch again when we check npxCache
const manifests = new Map()

const getManifest = async (spec, flatOptions) => {
  if (!manifests.has(spec.raw)) {
    const manifest = await pacote.manifest(spec, { ...flatOptions, preferOnline: true })
    manifests.set(spec.raw, manifest)
  }
  return manifests.get(spec.raw)
}

// Returns the required manifest if the spec is missing from the tree
// Returns the found node if it is in the tree
const missingFromTree = async ({ spec, tree, flatOptions, isNpxTree, shallow }) => {
  // If asking for a spec by name only (spec.raw === spec.name):
  //  - In local or global mode go with anything in the tree that matches
  //  - If looking in the npx cache check if a newer version is available
  const npxByNameOnly = isNpxTree && spec.name === spec.raw
  if (spec.registry && spec.type !== 'tag' && !npxByNameOnly) {
    // registry spec that is not a specific tag.
    const nodesBySpec = tree.inventory.query('packageName', spec.name)
    for (const node of nodesBySpec) {
      // continue if node is not a top level node
      if (shallow && node.depth) {
        continue
      }
      if (spec.rawSpec === '*') {
        return { node }
      }
      // package requested by specific version
      if (spec.type === 'version' && (node.pkgid === spec.raw)) {
        return { node }
      }
      // package requested by version range, only remaining registry type
      if (semver.satisfies(node.package.version, spec.rawSpec)) {
        return { node }
      }
    }
    const manifest = await getManifest(spec, flatOptions)
    return { manifest }
  } else {
    // non-registry spec, or a specific tag, or name only in npx tree.  Look up
    // manifest and check resolved to see if it's in the tree.
    const manifest = await getManifest(spec, flatOptions)
    if (spec.type === 'directory') {
      return { manifest }
    }
    const nodesByManifest = tree.inventory.query('packageName', manifest.name)
    for (const node of nodesByManifest) {
      if (node.package.resolved === manifest._resolved) {
        // we have a package by the same name and the same resolved destination, nothing to add.
        return { node }
      }
    }
    return { manifest }
  }
}

// see if the package.json at `path` has an entry that matches `cmd`
const hasPkgBin = (path, cmd, flatOptions) =>
  pacote.manifest(path, flatOptions)
    .then(manifest => manifest?.bin?.[cmd]).catch(() => null)

const exec = async (opts) => {
  const {
    args = [],
    call = '',
    localBin = resolve('./node_modules/.bin'),
    locationMsg = undefined,
    globalBin = '',
    globalPath,
    // dereference values because we manipulate it later
    packages: [...packages] = [],
    path = '.',
    runPath = '.',
    scriptShell = isWindows ? process.env.ComSpec || 'cmd' : 'sh',
    ...flatOptions
  } = opts

  let pkgPaths = opts.pkgPath
  if (typeof pkgPaths === 'string') {
    pkgPaths = [pkgPaths]
  }
  if (!pkgPaths) {
    pkgPaths = ['.']
  }
  let yes = opts.yes
  const run = () => runScript({
    args,
    call,
    flatOptions,
    locationMsg,
    path,
    binPaths,
    runPath,
    scriptShell,
  })

  // interactive mode
  if (!call && !args.length && !packages.length) {
    return run()
  }

  // Look in the local tree too
  pkgPaths.push(path)

  let needPackageCommandSwap = (args.length > 0) && (packages.length === 0)
  // If they asked for a command w/o specifying a package, see if there is a
  // bin that directly matches that name:
  // - in any local packages (pkgPaths can have workspaces in them or just the root)
  // - in the local tree (path)
  // - globally
  if (needPackageCommandSwap) {
    // Local packages and local tree
    for (const p of pkgPaths) {
      if (await hasPkgBin(p, args[0], flatOptions)) {
        // we have to install the local package into the npx cache so that its
        // bin links get set up
        flatOptions.installLinks = false
        // args[0] will exist when the package is installed
        packages.push(p)
        yes = true
        needPackageCommandSwap = false
        break
      }
    }
    if (needPackageCommandSwap) {
      // no bin entry in local packages or in tree, now we look for binPaths
      const dir = dirname(dirname(localBin))
      const localBinPath = await localFileExists(dir, args[0], '/')
      if (localBinPath) {
        binPaths.push(localBinPath)
        return await run()
      } else if (globalPath && await fileExists(`${globalBin}/${args[0]}`)) {
        binPaths.push(globalBin)
        return await run()
      }
      // We swap out args[0] with the bin from the manifest later
      packages.push(args[0])
    }
  }

  // Resolve any directory specs so that the npx directory is unique to the
  // resolved directory, not the potentially relative one (i.e. "npx .")
  for (const i in packages) {
    const pkg = packages[i]
    const spec = npa(pkg)
    if (spec.type === 'directory') {
      packages[i] = spec.fetchSpec
    }
  }

  const localArb = new Arborist({ ...flatOptions, path })
  const localTree = await localArb.loadActual()

  // Find anything that isn't installed locally
  const needInstall = []
  let commandManifest
  await Promise.all(packages.map(async (pkg, i) => {
    const spec = npa(pkg, path)
    const { manifest, node } = await missingFromTree({ spec, tree: localTree, flatOptions })
    if (manifest) {
      // Package does not exist in the local tree
      needInstall.push({ spec, manifest })
      if (i === 0) {
        commandManifest = manifest
      }
    } else if (i === 0) {
      // The node.package has enough to look up the bin
      commandManifest = node.package
    }
  }))

  if (needPackageCommandSwap) {
    const spec = npa(args[0])

    if (spec.type === 'directory') {
      yes = true
    }

    args[0] = getBinFromManifest(commandManifest)

    if (needInstall.length > 0 && globalPath) {
      // See if the package is installed globally. If it is, run the translated bin
      const globalArb = new Arborist({ ...flatOptions, path: globalPath, global: true })
      const globalTree = await globalArb.loadActual().catch(() => {
        log.verbose(`Could not read global path ${globalPath}, ignoring`)
        return null
      })
      if (globalTree) {
        const { manifest: globalManifest } =
          await missingFromTree({ spec, tree: globalTree, flatOptions, shallow: true })
        if (!globalManifest && await fileExists(`${globalBin}/${args[0]}`)) {
          binPaths.push(globalBin)
          return await run()
        }
      }
    }
  }

  const add = []
  if (needInstall.length > 0) {
    // Install things to the npx cache, if needed
    const { npxCache } = flatOptions
    if (!npxCache) {
      throw new Error('Must provide a valid npxCache path')
    }
    const hash = crypto.createHash('sha512')
      .update(packages.map(p => {
        // Keeps the npx directory unique to the resolved directory, not the
        // potentially relative one (i.e. "npx .")
        const spec = npa(p)
        if (spec.type === 'directory') {
          return spec.fetchSpec
        }
        return p
      }).sort((a, b) => a.localeCompare(b, 'en')).join('\n'))
      .digest('hex')
      .slice(0, 16)
    const installDir = resolve(npxCache, hash)
    await mkdir(installDir, { recursive: true })
    const npxArb = new Arborist({
      ...flatOptions,
      path: installDir,
    })
    const npxTree = await npxArb.loadActual()
    await Promise.all(needInstall.map(async ({ spec }) => {
      const { manifest } = await missingFromTree({
        spec,
        tree: npxTree,
        flatOptions,
        isNpxTree: true,
      })
      if (manifest) {
        // Manifest is not in npxCache, we need to install it there
        if (!spec.registry) {
          add.push(manifest._from)
        } else {
          add.push(manifest._id)
        }
      }
    }))

    if (add.length) {
      if (!yes) {
        const addList = add.map(a => `${a.replace(/@$/, '')}`)

        // set -n to always say no
        if (yes === false) {
          // Error message lists missing package(s) when process is canceled
          /* eslint-disable-next-line max-len */
          throw new Error(`npx canceled due to missing packages and no YES option: ${JSON.stringify(addList)}`)
        }

        if (noTTY() || ciInfo.isCI) {
          /* eslint-disable-next-line max-len */
          log.warn('exec', `The following package${add.length === 1 ? ' was' : 's were'} not found and will be installed: ${addList.join(', ')}`)
        } else {
          const confirm = await input.read(() => read({
            /* eslint-disable-next-line max-len */
            prompt: `Need to install the following packages:\n${addList.join('\n')}\nOk to proceed? `,
            default: 'y',
          }))
          if (confirm.trim().toLowerCase().charAt(0) !== 'y') {
            throw new Error('canceled')
          }
        }
      }
      await npxArb.reify({
        ...flatOptions,
        add,
      })
    }
    binPaths.push(resolve(installDir, 'node_modules/.bin'))
  }

  return await run()
}

module.exports = exec
