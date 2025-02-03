const cacache = require('cacache')
const git = require('@npmcli/git')
const npa = require('npm-package-arg')
const pickManifest = require('npm-pick-manifest')
const { Minipass } = require('minipass')
const { log } = require('proc-log')
const DirFetcher = require('./dir.js')
const Fetcher = require('./fetcher.js')
const FileFetcher = require('./file.js')
const RemoteFetcher = require('./remote.js')
const _ = require('./util/protected.js')
const addGitSha = require('./util/add-git-sha.js')
const npm = require('./util/npm.js')

const hashre = /^[a-f0-9]{40}$/

// get the repository url.
// prefer https if there's auth, since ssh will drop that.
// otherwise, prefer ssh if available (more secure).
// We have to add the git+ back because npa suppresses it.
const repoUrl = (h, opts) =>
  h.sshurl && !(h.https && h.auth) && addGitPlus(h.sshurl(opts)) ||
  h.https && addGitPlus(h.https(opts))

// add git+ to the url, but only one time.
const addGitPlus = url => url && `git+${url}`.replace(/^(git\+)+/, 'git+')

class GitFetcher extends Fetcher {
  constructor (spec, opts) {
    super(spec, opts)

    // we never want to compare integrity for git dependencies: npm/rfcs#525
    if (this.opts.integrity) {
      delete this.opts.integrity
      log.warn(`skipping integrity check for git dependency ${this.spec.fetchSpec}`)
    }

    this.resolvedRef = null
    if (this.spec.hosted) {
      this.from = this.spec.hosted.shortcut({ noCommittish: false })
    }

    // shortcut: avoid full clone when we can go straight to the tgz
    // if we have the full sha and it's a hosted git platform
    if (this.spec.gitCommittish && hashre.test(this.spec.gitCommittish)) {
      this.resolvedSha = this.spec.gitCommittish
      // use hosted.tarball() when we shell to RemoteFetcher later
      this.resolved = this.spec.hosted
        ? repoUrl(this.spec.hosted, { noCommittish: false })
        : this.spec.rawSpec
    } else {
      this.resolvedSha = ''
    }

    this.Arborist = opts.Arborist || null
  }

  // just exposed to make it easier to test all the combinations
  static repoUrl (hosted, opts) {
    return repoUrl(hosted, opts)
  }

  get types () {
    return ['git']
  }

  resolve () {
    // likely a hosted git repo with a sha, so get the tarball url
    // but in general, no reason to resolve() more than necessary!
    if (this.resolved) {
      return super.resolve()
    }

    // fetch the git repo and then look at the current hash
    const h = this.spec.hosted
    // try to use ssh, fall back to git.
    return h
      ? this.#resolvedFromHosted(h)
      : this.#resolvedFromRepo(this.spec.fetchSpec)
  }

  // first try https, since that's faster and passphrase-less for
  // public repos, and supports private repos when auth is provided.
  // Fall back to SSH to support private repos
  // NB: we always store the https url in resolved field if auth
  // is present, otherwise ssh if the hosted type provides it
  #resolvedFromHosted (hosted) {
    return this.#resolvedFromRepo(hosted.https && hosted.https()).catch(er => {
      // Throw early since we know pathspec errors will fail again if retried
      if (er instanceof git.errors.GitPathspecError) {
        throw er
      }
      const ssh = hosted.sshurl && hosted.sshurl()
      // no fallthrough if we can't fall through or have https auth
      if (!ssh || hosted.auth) {
        throw er
      }
      return this.#resolvedFromRepo(ssh)
    })
  }

  #resolvedFromRepo (gitRemote) {
    // XXX make this a custom error class
    if (!gitRemote) {
      return Promise.reject(new Error(`No git url for ${this.spec}`))
    }
    const gitRange = this.spec.gitRange
    const name = this.spec.name
    return git.revs(gitRemote, this.opts).then(remoteRefs => {
      return gitRange ? pickManifest({
        versions: remoteRefs.versions,
        'dist-tags': remoteRefs['dist-tags'],
        name,
      }, gitRange, this.opts)
        : this.spec.gitCommittish ?
          remoteRefs.refs[this.spec.gitCommittish] ||
          remoteRefs.refs[remoteRefs.shas[this.spec.gitCommittish]]
          : remoteRefs.refs.HEAD // no git committish, get default head
    }).then(revDoc => {
      // the committish provided isn't in the rev list
      // things like HEAD~3 or @yesterday can land here.
      if (!revDoc || !revDoc.sha) {
        return this.#resolvedFromClone()
      }

      this.resolvedRef = revDoc
      this.resolvedSha = revDoc.sha
      this.#addGitSha(revDoc.sha)
      return this.resolved
    })
  }

  #setResolvedWithSha (withSha) {
    // we haven't cloned, so a tgz download is still faster
    // of course, if it's not a known host, we can't do that.
    this.resolved = !this.spec.hosted ? withSha
      : repoUrl(npa(withSha).hosted, { noCommittish: false })
  }

  // when we get the git sha, we affix it to our spec to build up
  // either a git url with a hash, or a tarball download URL
  #addGitSha (sha) {
    this.#setResolvedWithSha(addGitSha(this.spec, sha))
  }

  #resolvedFromClone () {
    // do a full or shallow clone, then look at the HEAD
    // kind of wasteful, but no other option, really
    return this.#clone(() => this.resolved)
  }

  #prepareDir (dir) {
    return this[_.readPackageJson](dir).then(mani => {
      // no need if we aren't going to do any preparation.
      const scripts = mani.scripts
      if (!mani.workspaces && (!scripts || !(
        scripts.postinstall ||
          scripts.build ||
          scripts.preinstall ||
          scripts.install ||
          scripts.prepack ||
          scripts.prepare))) {
        return
      }

      // to avoid cases where we have an cycle of git deps that depend
      // on one another, we only ever do preparation for one instance
      // of a given git dep along the chain of installations.
      // Note that this does mean that a dependency MAY in theory end up
      // trying to run its prepare script using a dependency that has not
      // been properly prepared itself, but that edge case is smaller
      // and less hazardous than a fork bomb of npm and git commands.
      const noPrepare = !process.env._PACOTE_NO_PREPARE_ ? []
        : process.env._PACOTE_NO_PREPARE_.split('\n')
      if (noPrepare.includes(this.resolved)) {
        log.info('prepare', 'skip prepare, already seen', this.resolved)
        return
      }
      noPrepare.push(this.resolved)

      // the DirFetcher will do its own preparation to run the prepare scripts
      // All we have to do is put the deps in place so that it can succeed.
      return npm(
        this.npmBin,
        [].concat(this.npmInstallCmd).concat(this.npmCliConfig),
        dir,
        { ...process.env, _PACOTE_NO_PREPARE_: noPrepare.join('\n') },
        { message: 'git dep preparation failed' }
      )
    })
  }

  [_.tarballFromResolved] () {
    const stream = new Minipass()
    stream.resolved = this.resolved
    stream.from = this.from

    // check it out and then shell out to the DirFetcher tarball packer
    this.#clone(dir => this.#prepareDir(dir)
      .then(() => new Promise((res, rej) => {
        if (!this.Arborist) {
          throw new Error('GitFetcher requires an Arborist constructor to pack a tarball')
        }
        const df = new DirFetcher(`file:${dir}`, {
          ...this.opts,
          Arborist: this.Arborist,
          resolved: null,
          integrity: null,
        })
        const dirStream = df[_.tarballFromResolved]()
        dirStream.on('error', rej)
        dirStream.on('end', res)
        dirStream.pipe(stream)
      }))).catch(
      /* istanbul ignore next: very unlikely and hard to test */
      er => stream.emit('error', er)
    )
    return stream
  }

  // clone a git repo into a temp folder (or fetch and unpack if possible)
  // handler accepts a directory, and returns a promise that resolves
  // when we're done with it, at which point, cacache deletes it
  //
  // TODO: after cloning, create a tarball of the folder, and add to the cache
  // with cacache.put.stream(), using a key that's deterministic based on the
  // spec and repo, so that we don't ever clone the same thing multiple times.
  #clone (handler, tarballOk = true) {
    const o = { tmpPrefix: 'git-clone' }
    const ref = this.resolvedSha || this.spec.gitCommittish
    const h = this.spec.hosted
    const resolved = this.resolved

    // can be set manually to false to fall back to actual git clone
    tarballOk = tarballOk &&
      h && resolved === repoUrl(h, { noCommittish: false }) && h.tarball

    return cacache.tmp.withTmp(this.cache, o, async tmp => {
      // if we're resolved, and have a tarball url, shell out to RemoteFetcher
      if (tarballOk) {
        const nameat = this.spec.name ? `${this.spec.name}@` : ''
        return new RemoteFetcher(h.tarball({ noCommittish: false }), {
          ...this.opts,
          allowGitIgnore: true,
          pkgid: `git:${nameat}${this.resolved}`,
          resolved: this.resolved,
          integrity: null, // it'll always be different, if we have one
        }).extract(tmp).then(() => handler(tmp), er => {
          // fall back to ssh download if tarball fails
          if (er.constructor.name.match(/^Http/)) {
            return this.#clone(handler, false)
          } else {
            throw er
          }
        })
      }

      const sha = await (
        h ? this.#cloneHosted(ref, tmp)
        : this.#cloneRepo(this.spec.fetchSpec, ref, tmp)
      )
      this.resolvedSha = sha
      if (!this.resolved) {
        await this.#addGitSha(sha)
      }
      return handler(tmp)
    })
  }

  // first try https, since that's faster and passphrase-less for
  // public repos, and supports private repos when auth is provided.
  // Fall back to SSH to support private repos
  // NB: we always store the https url in resolved field if auth
  // is present, otherwise ssh if the hosted type provides it
  #cloneHosted (ref, tmp) {
    const hosted = this.spec.hosted
    return this.#cloneRepo(hosted.https({ noCommittish: true }), ref, tmp)
      .catch(er => {
        // Throw early since we know pathspec errors will fail again if retried
        if (er instanceof git.errors.GitPathspecError) {
          throw er
        }
        const ssh = hosted.sshurl && hosted.sshurl({ noCommittish: true })
        // no fallthrough if we can't fall through or have https auth
        if (!ssh || hosted.auth) {
          throw er
        }
        return this.#cloneRepo(ssh, ref, tmp)
      })
  }

  #cloneRepo (repo, ref, tmp) {
    const { opts, spec } = this
    return git.clone(repo, ref, tmp, { ...opts, spec })
  }

  manifest () {
    if (this.package) {
      return Promise.resolve(this.package)
    }

    return this.spec.hosted && this.resolved
      ? FileFetcher.prototype.manifest.apply(this)
      : this.#clone(dir =>
        this[_.readPackageJson](dir)
          .then(mani => this.package = {
            ...mani,
            _resolved: this.resolved,
            _from: this.from,
          }))
  }

  packument () {
    return FileFetcher.prototype.packument.apply(this)
  }
}
module.exports = GitFetcher
