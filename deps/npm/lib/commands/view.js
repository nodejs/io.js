const columns = require('cli-columns')
const { readFile } = require('node:fs/promises')
const jsonParse = require('json-parse-even-better-errors')
const { log, output, META } = require('proc-log')
const npa = require('npm-package-arg')
const { resolve } = require('node:path')
const formatBytes = require('../utils/format-bytes.js')
const relativeDate = require('tiny-relative-date')
const semver = require('semver')
const { inspect } = require('node:util')
const { packument } = require('pacote')
const Queryable = require('../utils/queryable.js')
const BaseCommand = require('../base-cmd.js')
const { getError } = require('../utils/error-message.js')
const { jsonError, outputError } = require('../utils/output-error.js')

const readJson = file => readFile(file, 'utf8').then(jsonParse)

class View extends BaseCommand {
  static description = 'View registry info'
  static name = 'view'
  static params = [
    'json',
    'workspace',
    'workspaces',
    'include-workspace-root',
  ]

  static workspaces = true
  static ignoreImplicitWorkspace = false
  static usage = ['[<package-spec>] [<field>[.subfield]...]']

  static async completion (opts, npm) {
    if (opts.conf.argv.remain.length <= 2) {
      // There used to be registry completion here, but it stopped
      // making sense somewhere around 50,000 packages on the registry
      return
    }
    // have the package, get the fields
    const config = {
      ...npm.flatOptions,
      fullMetadata: true,
      preferOnline: true,
    }
    const spec = npa(opts.conf.argv.remain[2])
    const pckmnt = await packument(spec, config)
    const defaultTag = npm.config.get('tag')
    const dv = pckmnt.versions[pckmnt['dist-tags'][defaultTag]]
    pckmnt.versions = Object.keys(pckmnt.versions).sort(semver.compareLoose)

    return getCompletionFields(pckmnt).concat(getCompletionFields(dv))
  }

  async exec (args) {
    let { pkg, local, rest } = parseArgs(args)

    if (local) {
      if (this.npm.global) {
        throw new Error('Cannot use view command in global mode.')
      }
      const dir = this.npm.prefix
      const manifest = await readJson(resolve(dir, 'package.json'))
      if (!manifest.name) {
        throw new Error('Invalid package.json, no "name" field')
      }
      // put the version back if it existed
      pkg = `${manifest.name}${pkg.slice(1)}`
    }

    await this.#viewPackage(pkg, rest)
  }

  async execWorkspaces (args) {
    const { pkg, local, rest } = parseArgs(args)

    if (!local) {
      log.warn('Ignoring workspaces for specified package(s)')
      return this.exec([pkg, ...rest])
    }

    const json = this.npm.config.get('json')
    await this.setWorkspaces()

    for (const name of this.workspaceNames) {
      try {
        await this.#viewPackage(`${name}${pkg.slice(1)}`, rest, { workspace: true })
      } catch (e) {
        const err = getError(e, { npm: this.npm, command: this })
        if (err.code !== 'E404') {
          throw e
        }
        if (json) {
          output.buffer({ [META]: true, jsonError: { [name]: jsonError(err, this.npm) } })
        } else {
          outputError(err)
        }
        process.exitCode = err.exitCode
      }
    }
  }

  async #viewPackage (name, args, { workspace } = {}) {
    const wholePackument = !args.length
    const json = this.npm.config.get('json')

    // If we are viewing many packages and outputting individual fields then
    // output the name before doing any async activity
    if (!json && !wholePackument && workspace) {
      output.standard(`${name}:`)
    }

    const [pckmnt, data] = await this.#getData(name, args, wholePackument)

    if (!json && wholePackument) {
      // pretty view (entire packument)
      for (const v of data) {
        output.standard(this.#prettyView(pckmnt, Object.values(v)[0][Queryable.ALL]))
      }
      return
    }

    const res = this.#packageOutput(cleanData(data, wholePackument), pckmnt._id)
    if (res) {
      if (json) {
        output.buffer(workspace ? { [name]: res } : res)
      } else {
        output.standard(res)
      }
    }
  }

  async #getData (pkg, args) {
    const spec = npa(pkg)

    const pckmnt = await packument(spec, {
      ...this.npm.flatOptions,
      preferOnline: true,
      fullMetadata: true,
    })

    // get the data about this package
    let version = this.npm.config.get('tag')
    // rawSpec is the git url if this is from git
    if (spec.type !== 'git' && spec.type !== 'directory' && spec.rawSpec !== '*') {
      version = spec.rawSpec
    }

    if (pckmnt['dist-tags']?.[version]) {
      version = pckmnt['dist-tags'][version]
    }

    if (pckmnt.time?.unpublished) {
      const u = pckmnt.time.unpublished
      throw Object.assign(new Error(`Unpublished on ${u.time}`), {
        statusCode: 404,
        code: 'E404',
        pkgid: pckmnt._id,
      })
    }

    const versions = pckmnt.versions || {}
    pckmnt.versions = Object.keys(versions).filter(v => {
      if (semver.valid(v)) {
        return true
      }
      log.info('view', `Ignoring invalid version: ${v}`)
      return false
    }).sort(semver.compareLoose)

    // remove readme unless we asked for it
    if (args.indexOf('readme') === -1) {
      delete pckmnt.readme
    }

    const data = Object.entries(versions)
      .filter(([v]) => semver.satisfies(v, version, true))
      .flatMap(([, v]) => {
        // remove readme unless we asked for it
        if (args.indexOf('readme') !== -1) {
          delete v.readme
        }
        return showFields({
          data: pckmnt,
          version: v,
          fields: args,
          json: this.npm.config.get('json'),
        })
      })

    // No data has been pushed because no data is matching the specified version
    if (!data.length && version !== 'latest') {
      throw Object.assign(new Error(`No match found for version ${version}`), {
        statusCode: 404,
        code: 'E404',
        pkgid: `${pckmnt._id}@${version}`,
      })
    }

    return [pckmnt, data]
  }

  #packageOutput (data, name) {
    const json = this.npm.config.get('json')
    const versions = Object.keys(data)
    const includeVersions = versions.length > 1

    let includeFields
    const res = versions.flatMap((v) => {
      const fields = Object.entries(data[v])

      includeFields ||= (fields.length > 1)

      const msg = json ? {} : []

      for (let [f, d] of fields) {
        d = cleanup(d)

        if (json) {
          msg[f] = d
          continue
        }

        if (includeVersions || includeFields || typeof d !== 'string') {
          d = inspect(d, {
            showHidden: false,
            depth: 5,
            colors: this.npm.color,
            maxArrayLength: null,
          })
        }

        if (f && includeFields) {
          f += ' = '
        }

        msg.push(`${includeVersions ? `${name}@${v} ` : ''}${includeFields ? f : ''}${d}`)
      }

      return msg
    })

    if (json) {
      // TODO(BREAKING_CHANGE): all unwrapping should be removed. Users should know
      // based on their arguments if they can expect an array or an object. And this
      // unwrapping can break that assumption. Eg `npm view abbrev@^2` should always
      // return an array, but currently since there is only one version matching `^2`
      // this will return a single object instead.
      const first = Object.keys(res[0] || {})
      const jsonRes = first.length === 1 ? res.map(m => m[first[0]]) : res
      if (jsonRes.length === 0) {
        return
      }
      if (jsonRes.length === 1) {
        return jsonRes[0]
      }
      return jsonRes
    }

    return res.join('\n').trim()
  }

  #prettyView (packu, manifest) {
    // More modern, pretty printing of default view
    const unicode = this.npm.config.get('unicode')
    const chalk = this.npm.chalk
    const deps = Object.entries(manifest.dependencies || {}).map(([k, dep]) =>
      `${chalk.blue(k)}: ${dep}`
    )
    // Sort dist-tags by publish time, then tag name, keeping latest at the top of the list
    const distTags = Object.entries(packu['dist-tags'])
      .sort(([aTag, aVer], [bTag, bVer]) => {
        const aTime = aTag === 'latest' ? Infinity : Date.parse(packu.time[aVer])
        const bTime = bTag === 'latest' ? Infinity : Date.parse(packu.time[bVer])
        if (aTime === bTime) {
          return aTag > bTag ? -1 : 1
        }
        return aTime > bTime ? -1 : 1
      })
      .map(([k, t]) => `${chalk.blue(k)}: ${t}`)

    const site = manifest.homepage?.url || manifest.homepage
    const bins = Object.keys(manifest.bin || {})
    const licenseField = manifest.license || 'Proprietary'
    const license = typeof licenseField === 'string'
      ? licenseField
      : (licenseField.type || 'Proprietary')

    const res = []

    res.push('')
    res.push([
      chalk.underline.cyan(`${manifest.name}@${manifest.version}`),
      license.toLowerCase().trim() === 'proprietary'
        ? chalk.red(license)
        : chalk.green(license),
      `deps: ${deps.length ? chalk.cyan(deps.length) : chalk.cyan('none')}`,
      `versions: ${chalk.cyan(packu.versions.length + '')}`,
    ].join(' | '))

    manifest.description && res.push(manifest.description)
    if (site) {
      res.push(chalk.blue(site))
    }

    manifest.deprecated && res.push(
      `\n${chalk.redBright('DEPRECATED')}${unicode ? ' ⚠️ ' : '!!'} - ${manifest.deprecated}`
    )

    if (packu.keywords?.length) {
      res.push(`\nkeywords: ${
        packu.keywords.map(k => chalk.cyan(k)).join(', ')
      }`)
    }

    if (bins.length) {
      res.push(`\nbin: ${chalk.cyan(bins.join(', '))}`)
    }

    res.push('\ndist')
    res.push(`.tarball: ${chalk.blue(manifest.dist.tarball)}`)
    res.push(`.shasum: ${chalk.green(manifest.dist.shasum)}`)
    if (manifest.dist.integrity) {
      res.push(`.integrity: ${chalk.green(manifest.dist.integrity)}`)
    }
    if (manifest.dist.unpackedSize) {
      res.push(`.unpackedSize: ${chalk.blue(formatBytes(manifest.dist.unpackedSize, true))}`)
    }

    if (deps.length) {
      const maxDeps = 24
      res.push('\ndependencies:')
      res.push(columns(deps.slice(0, maxDeps), { padding: 1 }))
      if (deps.length > maxDeps) {
        res.push(chalk.dim(`(...and ${deps.length - maxDeps} more.)`))
      }
    }

    if (packu.maintainers?.length) {
      res.push('\nmaintainers:')
      packu.maintainers.forEach(u =>
        res.push(`- ${unparsePerson({
          name: chalk.blue(u.name),
          email: chalk.dim(u.email) })}`)
      )
    }

    res.push('\ndist-tags:')
    const maxTags = 12
    res.push(columns(distTags.slice(0, maxTags), { padding: 1, sort: false }))
    if (distTags.length > maxTags) {
      res.push(chalk.dim(`(...and ${distTags.length - maxTags} more.)`))
    }

    const publisher = manifest._npmUser && unparsePerson({
      name: chalk.blue(manifest._npmUser.name),
      email: chalk.dim(manifest._npmUser.email),
    })
    if (publisher || packu.time) {
      let publishInfo = 'published'
      if (packu.time) {
        publishInfo += ` ${chalk.cyan(relativeDate(packu.time[manifest.version]))}`
      }
      if (publisher) {
        publishInfo += ` by ${publisher}`
      }
      res.push('')
      res.push(publishInfo)
    }

    return res.join('\n')
  }
}

module.exports = View

function parseArgs (args) {
  if (!args.length) {
    args = ['.']
  }

  const pkg = args.shift()

  return {
    pkg,
    local: /^\.@/.test(pkg) || pkg === '.',
    rest: args,
  }
}

function cleanData (obj, wholePackument) {
  // JSON formatted output (JSON or specific attributes from packument)
  const data = obj.reduce((acc, cur) => {
    if (cur) {
      Object.entries(cur).forEach(([k, v]) => {
        acc[k] ||= {}
        Object.keys(v).forEach((t) => {
          acc[k][t] = cur[k][t]
        })
      })
    }
    return acc
  }, {})

  if (wholePackument) {
    const cleaned = Object.entries(data).reduce((acc, [k, v]) => {
      acc[k] = v[Queryable.ALL]
      return acc
    }, {})
    log.silly('view', cleaned)
    return cleaned
  }

  return data
}

// return whatever was printed
function showFields ({ data, version, fields, json }) {
  const o = [data, version].reduce((acc, s) => {
    Object.entries(s).forEach(([k, v]) => {
      acc[k] = v
    })
    return acc
  }, {})

  const queryable = new Queryable(o)

  if (!fields.length) {
    return { [version.version]: queryable.query(Queryable.ALL) }
  }

  return fields.map((field) => {
    const s = queryable.query(field, { unwrapSingleItemArrays: !json })
    if (s) {
      return { [version.version]: s }
    }
  })
}

function cleanup (data) {
  if (Array.isArray(data)) {
    return data.map(cleanup)
  }

  if (!data || typeof data !== 'object') {
    return data
  }

  const keys = Object.keys(data)
  if (keys.length <= 3 && data.name && (
    (keys.length === 1) ||
    (keys.length === 3 && data.email && data.url) ||
    (keys.length === 2 && (data.email || data.url))
  )) {
    data = unparsePerson(data)
  }

  return data
}

const unparsePerson = (d) =>
  `${d.name}${d.email ? ` <${d.email}>` : ''}${d.url ? ` (${d.url})` : ''}`

function getCompletionFields (d, f = [], pref = []) {
  Object.entries(d).forEach(([k, v]) => {
    if (k.charAt(0) === '_' || k.indexOf('.') !== -1) {
      return
    }
    const p = pref.concat(k).join('.')
    f.push(p)
    if (Array.isArray(v)) {
      v.forEach((val, i) => {
        const pi = p + '[' + i + ']'
        if (val && typeof val === 'object') {
          getCompletionFields(val, f, [p])
        } else {
          f.push(pi)
        }
      })
      return
    }
    if (typeof v === 'object') {
      getCompletionFields(v, f, [p])
    }
  })
  return f
}
