'use strict'

const npa = require('npm-package-arg')
const semver = require('semver')
const { checkEngine } = require('npm-install-checks')
const normalizeBin = require('npm-normalize-package-bin')

const engineOk = (manifest, npmVersion, nodeVersion) => {
  try {
    checkEngine(manifest, npmVersion, nodeVersion)
    return true
  } catch (_) {
    return false
  }
}

const isBefore = (verTimes, ver, time) =>
  !verTimes || !verTimes[ver] || Date.parse(verTimes[ver]) <= time

const avoidSemverOpt = { includePrerelease: true, loose: true }
const shouldAvoid = (ver, avoid) =>
  avoid && semver.satisfies(ver, avoid, avoidSemverOpt)

const decorateAvoid = (result, avoid) =>
  result && shouldAvoid(result.version, avoid)
    ? { ...result, _shouldAvoid: true }
    : result

const pickManifest = (packument, wanted, opts) => {
  const {
    defaultTag = 'latest',
    before = null,
    nodeVersion = process.version,
    npmVersion = null,
    includeStaged = false,
    avoid = null,
    avoidStrict = false,
  } = opts

  const { name, time: verTimes } = packument
  const versions = packument.versions || {}

  if (avoidStrict) {
    const looseOpts = {
      ...opts,
      avoidStrict: false,
    }

    const result = pickManifest(packument, wanted, looseOpts)
    if (!result || !result._shouldAvoid) {
      return result
    }

    const caret = pickManifest(packument, `^${result.version}`, looseOpts)
    if (!caret || !caret._shouldAvoid) {
      return {
        ...caret,
        _outsideDependencyRange: true,
        _isSemVerMajor: false,
      }
    }

    const star = pickManifest(packument, '*', looseOpts)
    if (!star || !star._shouldAvoid) {
      return {
        ...star,
        _outsideDependencyRange: true,
        _isSemVerMajor: true,
      }
    }

    throw Object.assign(new Error(`No avoidable versions for ${name}`), {
      code: 'ETARGET',
      name,
      wanted,
      avoid,
      before,
      versions: Object.keys(versions),
    })
  }

  const staged = (includeStaged && packument.stagedVersions &&
    packument.stagedVersions.versions) || {}
  const restricted = (packument.policyRestrictions &&
    packument.policyRestrictions.versions) || {}

  const time = before && verTimes ? +(new Date(before)) : Infinity
  const spec = npa.resolve(name, wanted || defaultTag)
  const type = spec.type
  const distTags = packument['dist-tags'] || {}

  if (type !== 'tag' && type !== 'version' && type !== 'range') {
    throw new Error('Only tag, version, and range are supported')
  }

  // if the type is 'tag', and not just the implicit default, then it must
  // be that exactly, or nothing else will do.
  if (wanted && type === 'tag') {
    const ver = distTags[wanted]
    // if the version in the dist-tags is before the before date, then
    // we use that.  Otherwise, we get the highest precedence version
    // prior to the dist-tag.
    if (isBefore(verTimes, ver, time)) {
      return decorateAvoid(versions[ver] || staged[ver] || restricted[ver], avoid)
    } else {
      return pickManifest(packument, `<=${ver}`, opts)
    }
  }

  // similarly, if a specific version, then only that version will do
  if (wanted && type === 'version') {
    const ver = semver.clean(wanted, { loose: true })
    const mani = versions[ver] || staged[ver] || restricted[ver]
    return isBefore(verTimes, ver, time) ? decorateAvoid(mani, avoid) : null
  }

  // ok, sort based on our heuristics, and pick the best fit
  const range = type === 'range' ? wanted : '*'

  // if the range is *, then we prefer the 'latest' if available
  // but skip this if it should be avoided, in that case we have
  // to try a little harder.
  const defaultVer = distTags[defaultTag]
  if (defaultVer &&
      (range === '*' || semver.satisfies(defaultVer, range, { loose: true })) &&
      !restricted[defaultVer] &&
      !shouldAvoid(defaultVer, avoid)) {
    const mani = versions[defaultVer]
    const ok = mani &&
      isBefore(verTimes, defaultVer, time) &&
      engineOk(mani, npmVersion, nodeVersion) &&
      !mani.deprecated &&
      !staged[defaultVer]
    if (ok) {
      return mani
    }
  }

  // ok, actually have to sort the list and take the winner
  const allEntries = Object.entries(versions)
    .concat(Object.entries(staged))
    .concat(Object.entries(restricted))
    .filter(([ver]) => isBefore(verTimes, ver, time))

  if (!allEntries.length) {
    throw Object.assign(new Error(`No versions available for ${name}`), {
      code: 'ENOVERSIONS',
      name,
      type,
      wanted,
      before,
      versions: Object.keys(versions),
    })
  }

  const sortSemverOpt = { loose: true }
  const entries = allEntries.filter(([ver]) =>
    semver.satisfies(ver, range, { loose: true }))
    .sort((a, b) => {
      const [vera, mania] = a
      const [verb, manib] = b
      const notavoida = !shouldAvoid(vera, avoid)
      const notavoidb = !shouldAvoid(verb, avoid)
      const notrestra = !restricted[vera]
      const notrestrb = !restricted[verb]
      const notstagea = !staged[vera]
      const notstageb = !staged[verb]
      const notdepra = !mania.deprecated
      const notdeprb = !manib.deprecated
      const enginea = engineOk(mania, npmVersion, nodeVersion)
      const engineb = engineOk(manib, npmVersion, nodeVersion)
      // sort by:
      // - not an avoided version
      // - not restricted
      // - not staged
      // - not deprecated and engine ok
      // - engine ok
      // - not deprecated
      // - semver
      return (notavoidb - notavoida) ||
        (notrestrb - notrestra) ||
        (notstageb - notstagea) ||
        ((notdeprb && engineb) - (notdepra && enginea)) ||
        (engineb - enginea) ||
        (notdeprb - notdepra) ||
        semver.rcompare(vera, verb, sortSemverOpt)
    })

  return decorateAvoid(entries[0] && entries[0][1], avoid)
}

module.exports = (packument, wanted, opts = {}) => {
  const mani = pickManifest(packument, wanted, opts)
  const picked = mani && normalizeBin(mani)
  const policyRestrictions = packument.policyRestrictions
  const restricted = (policyRestrictions && policyRestrictions.versions) || {}

  if (picked && !restricted[picked.version]) {
    return picked
  }

  const { before = null, defaultTag = 'latest' } = opts
  const bstr = before ? new Date(before).toLocaleString() : ''
  const { name } = packument
  const pckg = `${name}@${wanted}` +
    (before ? ` with a date before ${bstr}` : '')

  const isForbidden = picked && !!restricted[picked.version]
  const polMsg = isForbidden ? policyRestrictions.message : ''

  const msg = !isForbidden ? `No matching version found for ${pckg}.`
    : `Could not download ${pckg} due to policy violations:\n${polMsg}`

  const code = isForbidden ? 'E403' : 'ETARGET'
  throw Object.assign(new Error(msg), {
    code,
    type: npa.resolve(packument.name, wanted).type,
    wanted,
    versions: Object.keys(packument.versions ?? {}),
    name,
    distTags: packument['dist-tags'],
    defaultTag,
  })
}
