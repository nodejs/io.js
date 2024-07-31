// called with all the options already set to their defaults

const retrieveTag = require('./retrieve-tag.js')
const semver = require('semver')
const enforceClean = require('./enforce-clean.js')
const writeJson = require('./write-json.js')
const readJson = require('./read-json.js')
const git = require('@npmcli/git')
const commit = require('./commit.js')
const tag = require('./tag.js')
const { log } = require('proc-log')

const runScript = require('@npmcli/run-script')

module.exports = async (newversion, opts) => {
  const {
    path,
    allowSameVersion,
    gitTagVersion,
    ignoreScripts,
    preid,
    pkg,
  } = opts

  const { valid, clean, inc } = semver
  const current = pkg.version || '0.0.0'
  const currentClean = clean(current)

  let newV
  if (valid(newversion, { loose: true })) {
    newV = clean(newversion, { loose: true })
  } else if (newversion === 'from-git') {
    newV = await retrieveTag(opts)
  } else {
    newV = inc(currentClean, newversion, { loose: true }, preid)
  }

  if (!newV) {
    throw Object.assign(new Error('Invalid version: ' + newversion), {
      current,
      requested: newversion,
    })
  }

  if (newV === currentClean && !allowSameVersion) {
    throw Object.assign(new Error('Version not changed'), {
      current,
      requested: newversion,
      newVersion: newV,
    })
  }

  const isGitDir = newversion === 'from-git' || await git.is(opts)

  // ok!  now we know the new version, and the old version is in pkg

  // - check if git dir is clean
  // returns false if we should not keep doing git stuff
  const doGit = gitTagVersion && isGitDir && await enforceClean(opts)

  if (!ignoreScripts) {
    await runScript({
      ...opts,
      pkg,
      stdio: 'inherit',
      event: 'preversion',
      env: {
        npm_old_version: current,
        npm_new_version: newV,
      },
    })
  }

  // - update the files
  pkg.version = newV
  delete pkg._id
  await writeJson(`${path}/package.json`, pkg)

  // try to update shrinkwrap, but ok if this fails
  const locks = [`${path}/package-lock.json`, `${path}/npm-shrinkwrap.json`]
  const haveLocks = []
  for (const lock of locks) {
    try {
      const sw = await readJson(lock)
      sw.version = newV
      if (sw.packages && sw.packages['']) {
        sw.packages[''].version = newV
      }
      await writeJson(lock, sw)
      haveLocks.push(lock)
    } catch {
      // ignore errors
    }
  }

  if (!ignoreScripts) {
    await runScript({
      ...opts,
      pkg,
      stdio: 'inherit',
      event: 'version',
      env: {
        npm_old_version: current,
        npm_new_version: newV,
      },
    })
  }

  if (doGit) {
    // - git add, git commit, git tag
    await git.spawn(['add', `${path}/package.json`], opts)
    // sometimes people .gitignore their lockfiles
    for (const lock of haveLocks) {
      await git.spawn(['add', lock], opts).catch(() => {})
    }
    await commit(newV, opts)
    await tag(newV, opts)
  } else {
    log.verbose('version', 'Not tagging: not in a git repo or no git cmd')
  }

  if (!ignoreScripts) {
    await runScript({
      ...opts,
      pkg,
      stdio: 'inherit',
      event: 'postversion',
      env: {
        npm_old_version: current,
        npm_new_version: newV,
      },
    })
  }

  return newV
}
