const cacache = require('cacache')
const pacote = require('pacote')
const fs = require('node:fs/promises')
const { join } = require('node:path')
const semver = require('semver')
const BaseCommand = require('../base-cmd.js')
const npa = require('npm-package-arg')
const jsonParse = require('json-parse-even-better-errors')
const localeCompare = require('@isaacs/string-locale-compare')('en')
const { log, output } = require('proc-log')

const searchCachePackage = async (path, parsed, cacheKeys) => {
  /* eslint-disable-next-line max-len */
  const searchMFH = new RegExp(`^make-fetch-happen:request-cache:.*(?<!/[@a-zA-Z]+)/${parsed.name}/-/(${parsed.name}[^/]+.tgz)$`)
  const searchPack = new RegExp(`^make-fetch-happen:request-cache:.*/${parsed.escapedName}$`)
  const results = new Set()
  cacheKeys = new Set(cacheKeys)
  for (const key of cacheKeys) {
    // match on the public key registry url format
    if (searchMFH.test(key)) {
      // extract the version from the filename
      const filename = key.match(searchMFH)[1]
      const noExt = filename.slice(0, -4)
      const noScope = `${parsed.name.split('/').pop()}-`
      const ver = noExt.slice(noScope.length)
      if (semver.satisfies(ver, parsed.rawSpec)) {
        results.add(key)
      }
      continue
    }
    // is this key a packument?
    if (!searchPack.test(key)) {
      continue
    }

    results.add(key)
    let packument, details
    try {
      details = await cacache.get(path, key)
      packument = jsonParse(details.data)
    } catch (_) {
      // if we couldn't parse the packument, abort
      continue
    }
    if (!packument.versions || typeof packument.versions !== 'object') {
      continue
    }

    // assuming this is a packument
    for (const ver of Object.keys(packument.versions)) {
      if (semver.satisfies(ver, parsed.rawSpec)) {
        if (packument.versions[ver].dist &&
          typeof packument.versions[ver].dist === 'object' &&
          packument.versions[ver].dist.tarball !== undefined &&
          cacheKeys.has(`make-fetch-happen:request-cache:${packument.versions[ver].dist.tarball}`)
        ) {
          results.add(`make-fetch-happen:request-cache:${packument.versions[ver].dist.tarball}`)
        }
      }
    }
  }
  return results
}

class Cache extends BaseCommand {
  static description = 'Manipulates packages cache'
  static name = 'cache'
  static params = ['cache']
  static usage = [
    'add <package-spec>',
    'clean [<key>]',
    'ls [<name>@<version>]',
    'verify',
  ]

  static async completion (opts) {
    const argv = opts.conf.argv.remain
    if (argv.length === 2) {
      return ['add', 'clean', 'verify', 'ls']
    }

    // TODO - eventually...
    switch (argv[2]) {
      case 'verify':
      case 'clean':
      case 'add':
      case 'ls':
        return []
    }
  }

  async exec (args) {
    const cmd = args.shift()
    switch (cmd) {
      case 'rm': case 'clear': case 'clean':
        return await this.clean(args)
      case 'add':
        return await this.add(args)
      case 'verify': case 'check':
        return await this.verify()
      case 'ls':
        return await this.ls(args)
      default:
        throw this.usageError()
    }
  }

  // npm cache clean [pkg]*
  async clean (args) {
    const cachePath = join(this.npm.cache, '_cacache')
    if (args.length === 0) {
      if (!this.npm.config.get('force')) {
        throw new Error(`As of npm@5, the npm cache self-heals from corruption issues
  by treating integrity mismatches as cache misses.  As a result,
  data extracted from the cache is guaranteed to be valid.  If you
  want to make sure everything is consistent, use \`npm cache verify\`
  instead.  Deleting the cache can only make npm go slower, and is
  not likely to correct any problems you may be encountering!

  On the other hand, if you're debugging an issue with the installer,
  or race conditions that depend on the timing of writing to an empty
  cache, you can use \`npm install --cache /tmp/empty-cache\` to use a
  temporary cache instead of nuking the actual one.

  If you're sure you want to delete the entire cache, rerun this command
  with --force.`)
      }
      return fs.rm(cachePath, { recursive: true, force: true })
    }
    for (const key of args) {
      let entry
      try {
        entry = await cacache.get(cachePath, key)
      } catch (err) {
        log.warn('cache', `Not Found: ${key}`)
        break
      }
      output.standard(`Deleted: ${key}`)
      await cacache.rm.entry(cachePath, key)
      // XXX this could leave other entries without content!
      await cacache.rm.content(cachePath, entry.integrity)
    }
  }

  // npm cache add <tarball-url>...
  // npm cache add <pkg> <ver>...
  // npm cache add <tarball>...
  // npm cache add <folder>...
  async add (args) {
    log.silly('cache add', 'args', args)
    if (args.length === 0) {
      throw this.usageError('First argument to `add` is required')
    }

    await Promise.all(args.map(async spec => {
      log.silly('cache add', 'spec', spec)
      // we ask pacote for the thing, and then just throw the data
      // away so that it tee-pipes it into the cache like it does
      // for a normal request.
      await pacote.tarball.stream(spec, stream => {
        stream.resume()
        return stream.promise()
      }, { ...this.npm.flatOptions })

      await pacote.manifest(spec, {
        ...this.npm.flatOptions,
        fullMetadata: true,
      })
    }))
  }

  async verify () {
    const cache = join(this.npm.cache, '_cacache')
    const prefix = cache.indexOf(process.env.HOME) === 0
      ? `~${cache.slice(process.env.HOME.length)}`
      : cache
    const stats = await cacache.verify(cache)
    output.standard(`Cache verified and compressed (${prefix})`)
    output.standard(`Content verified: ${stats.verifiedContent} (${stats.keptSize} bytes)`)
    if (stats.badContentCount) {
      output.standard(`Corrupted content removed: ${stats.badContentCount}`)
    }
    if (stats.reclaimedCount) {
      /* eslint-disable-next-line max-len */
      output.standard(`Content garbage-collected: ${stats.reclaimedCount} (${stats.reclaimedSize} bytes)`)
    }
    if (stats.missingContent) {
      output.standard(`Missing content: ${stats.missingContent}`)
    }
    output.standard(`Index entries: ${stats.totalEntries}`)
    output.standard(`Finished in ${stats.runTime.total / 1000}s`)
  }

  // npm cache ls [--package <spec> ...]
  async ls (specs) {
    const cachePath = join(this.npm.cache, '_cacache')
    const cacheKeys = Object.keys(await cacache.ls(cachePath))
    if (specs.length > 0) {
      // get results for each package spec specified
      const results = new Set()
      for (const spec of specs) {
        const parsed = npa(spec)
        if (parsed.rawSpec !== '' && parsed.type === 'tag') {
          throw this.usageError('Cannot list cache keys for a tagged package.')
        }
        const keySet = await searchCachePackage(cachePath, parsed, cacheKeys)
        for (const key of keySet) {
          results.add(key)
        }
      }
      [...results].sort(localeCompare).forEach(key => output.standard(key))
      return
    }
    cacheKeys.sort(localeCompare).forEach(key => output.standard(key))
  }
}

module.exports = Cache
