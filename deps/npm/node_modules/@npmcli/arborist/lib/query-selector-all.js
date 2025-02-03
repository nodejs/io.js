'use strict'

const { resolve } = require('node:path')
const { parser, arrayDelimiter } = require('@npmcli/query')
const localeCompare = require('@isaacs/string-locale-compare')('en')
const { log } = require('proc-log')
const { minimatch } = require('minimatch')
const npa = require('npm-package-arg')
const pacote = require('pacote')
const semver = require('semver')
const npmFetch = require('npm-registry-fetch')

// handle results for parsed query asts, results are stored in a map that has a
// key that points to each ast selector node and stores the resulting array of
// arborist nodes as its value, that is essential to how we handle multiple
// query selectors, e.g: `#a, #b, #c` <- 3 diff ast selector nodes
class Results {
  #currentAstSelector
  #initialItems
  #inventory
  #outdatedCache = new Map()
  #vulnCache
  #pendingCombinator
  #results = new Map()
  #targetNode

  constructor (opts) {
    this.#currentAstSelector = opts.rootAstNode.nodes[0]
    this.#inventory = opts.inventory
    this.#initialItems = opts.initialItems
    this.#vulnCache = opts.vulnCache
    this.#targetNode = opts.targetNode

    this.currentResults = this.#initialItems

    // We get this when first called and need to pass it to pacote
    this.flatOptions = opts.flatOptions || {}

    // reset by rootAstNode walker
    this.currentAstNode = opts.rootAstNode
  }

  get currentResults () {
    return this.#results.get(this.#currentAstSelector)
  }

  set currentResults (value) {
    this.#results.set(this.#currentAstSelector, value)
  }

  // retrieves the initial items to which start the filtering / matching
  // for most of the different types of recognized ast nodes, e.g: class (aka
  // depType), id, *, etc in different contexts we need to start with the
  // current list of filtered results, for example a query for `.workspace`
  // actually means the same as `*.workspace` so we want to start with the full
  // inventory if that's the first ast node we're reading but if it appears in
  // the middle of a query it should respect the previous filtered results,
  // combinators are a special case in which we always want to have the
  // complete inventory list in order to use the left-hand side ast node as a
  // filter combined with the element on its right-hand side
  get initialItems () {
    const firstParsed =
      (this.currentAstNode.parent.nodes[0] === this.currentAstNode) &&
      (this.currentAstNode.parent.parent.type === 'root')

    if (firstParsed) {
      return this.#initialItems
    }

    if (this.currentAstNode.prev().type === 'combinator') {
      return this.#inventory
    }
    return this.currentResults
  }

  // combinators need information about previously filtered items along
  // with info of the items parsed / retrieved from the selector right
  // past the combinator, for this reason combinators are stored and
  // only ran as the last part of each selector logic
  processPendingCombinator (nextResults) {
    if (this.#pendingCombinator) {
      const res = this.#pendingCombinator(this.currentResults, nextResults)
      this.#pendingCombinator = null
      this.currentResults = res
    } else {
      this.currentResults = nextResults
    }
  }

  // when collecting results to a root astNode, we traverse the list of child
  // selector nodes and collect all of their resulting arborist nodes into a
  // single/flat Set of items, this ensures we also deduplicate items
  collect (rootAstNode) {
    return new Set(rootAstNode.nodes.flatMap(n => this.#results.get(n)))
  }

  // selector types map to the '.type' property of the ast nodes via `${astNode.type}Type`
  //
  // attribute selector [name=value], etc
  attributeType () {
    const nextResults = this.initialItems.filter(node =>
      attributeMatch(this.currentAstNode, node.package)
    )
    this.processPendingCombinator(nextResults)
  }

  // dependency type selector (i.e. .prod, .dev, etc)
  // css calls this class, we interpret is as dependency type
  classType () {
    const depTypeFn = depTypes[String(this.currentAstNode)]
    if (!depTypeFn) {
      throw Object.assign(
        new Error(`\`${String(this.currentAstNode)}\` is not a supported dependency type.`),
        { code: 'EQUERYNODEPTYPE' }
      )
    }
    const nextResults = depTypeFn(this.initialItems)
    this.processPendingCombinator(nextResults)
  }

  // combinators (i.e. '>', ' ', '~')
  combinatorType () {
    this.#pendingCombinator = combinators[String(this.currentAstNode)]
  }

  // name selectors (i.e. #foo)
  // css calls this id, we interpret it as name
  idType () {
    const name = this.currentAstNode.value
    const nextResults = this.initialItems.filter(node =>
      (name === node.name) || (name === node.package.name)
    )
    this.processPendingCombinator(nextResults)
  }

  // pseudo selectors (prefixed with :)
  async pseudoType () {
    const pseudoFn = `${this.currentAstNode.value.slice(1)}Pseudo`
    if (!this[pseudoFn]) {
      throw Object.assign(
        new Error(`\`${this.currentAstNode.value
        }\` is not a supported pseudo selector.`),
        { code: 'EQUERYNOPSEUDO' }
      )
    }
    const nextResults = await this[pseudoFn]()
    this.processPendingCombinator(nextResults)
  }

  selectorType () {
    this.#currentAstSelector = this.currentAstNode
    // starts a new array in which resulting items
    // can be stored for each given ast selector
    if (!this.currentResults) {
      this.currentResults = []
    }
  }

  universalType () {
    this.processPendingCombinator(this.initialItems)
  }

  // pseudo selectors map to the 'value' property of the pseudo selectors in the ast nodes
  // via selectors via `${value.slice(1)}Pseudo`
  attrPseudo () {
    const { lookupProperties, attributeMatcher } = this.currentAstNode

    return this.initialItems.filter(node => {
      let objs = [node.package]
      for (const prop of lookupProperties) {
        // if an isArray symbol is found that means we'll need to iterate
        // over the previous found array to basically make sure we traverse
        // all its indexes testing for possible objects that may eventually
        // hold more keys specified in a selector
        if (prop === arrayDelimiter) {
          objs = objs.flat()
          continue
        }

        // otherwise just maps all currently found objs
        // to the next prop from the lookup properties list,
        // filters out any empty key lookup
        objs = objs.flatMap(obj => obj[prop] || [])

        // in case there's no property found in the lookup
        // just filters that item out
        const noAttr = objs.every(obj => !obj)
        if (noAttr) {
          return false
        }
      }

      // if any of the potential object matches
      // that item should be in the final result
      return objs.some(obj => attributeMatch(attributeMatcher, obj))
    })
  }

  emptyPseudo () {
    return this.initialItems.filter(node => node.edgesOut.size === 0)
  }

  extraneousPseudo () {
    return this.initialItems.filter(node => node.extraneous)
  }

  async hasPseudo () {
    const found = []
    for (const item of this.initialItems) {
      // This is the one time initialItems differs from inventory
      const res = await retrieveNodesFromParsedAst({
        flatOptions: this.flatOptions,
        initialItems: [item],
        inventory: this.#inventory,
        rootAstNode: this.currentAstNode.nestedNode,
        targetNode: item,
        vulnCache: this.#vulnCache,
      })
      if (res.size > 0) {
        found.push(item)
      }
    }
    return found
  }

  invalidPseudo () {
    const found = []
    for (const node of this.initialItems) {
      for (const edge of node.edgesIn) {
        if (edge.invalid) {
          found.push(node)
          break
        }
      }
    }
    return found
  }

  async isPseudo () {
    const res = await retrieveNodesFromParsedAst({
      flatOptions: this.flatOptions,
      initialItems: this.initialItems,
      inventory: this.#inventory,
      rootAstNode: this.currentAstNode.nestedNode,
      targetNode: this.currentAstNode,
      vulnCache: this.#vulnCache,
    })
    return [...res]
  }

  linkPseudo () {
    return this.initialItems.filter(node => node.isLink || (node.isTop && !node.isRoot))
  }

  missingPseudo () {
    return this.#inventory.reduce((res, node) => {
      for (const edge of node.edgesOut.values()) {
        if (edge.missing) {
          const pkg = { name: edge.name, version: edge.spec }
          const item = new this.#targetNode.constructor({ pkg })
          item.queryContext = {
            missing: true,
          }
          item.edgesIn = new Set([edge])
          res.push(item)
        }
      }
      return res
    }, [])
  }

  async notPseudo () {
    const res = await retrieveNodesFromParsedAst({
      flatOptions: this.flatOptions,
      initialItems: this.initialItems,
      inventory: this.#inventory,
      rootAstNode: this.currentAstNode.nestedNode,
      targetNode: this.currentAstNode,
      vulnCache: this.#vulnCache,
    })
    const internalSelector = new Set(res)
    return this.initialItems.filter(node =>
      !internalSelector.has(node))
  }

  overriddenPseudo () {
    return this.initialItems.filter(node => node.overridden)
  }

  pathPseudo () {
    return this.initialItems.filter(node => {
      if (!this.currentAstNode.pathValue) {
        return true
      }
      return minimatch(
        node.realpath.replace(/\\+/g, '/'),
        resolve(node.root.realpath, this.currentAstNode.pathValue).replace(/\\+/g, '/')
      )
    })
  }

  privatePseudo () {
    return this.initialItems.filter(node => node.package.private)
  }

  rootPseudo () {
    return this.initialItems.filter(node => node === this.#targetNode.root)
  }

  scopePseudo () {
    return this.initialItems.filter(node => node === this.#targetNode)
  }

  semverPseudo () {
    const {
      attributeMatcher,
      lookupProperties,
      semverFunc = 'infer',
      semverValue,
    } = this.currentAstNode
    const { qualifiedAttribute } = attributeMatcher

    if (!semverValue) {
      // DEPRECATED: remove this warning and throw an error as part of @npmcli/arborist@6
      log.warn('query', 'usage of :semver() with no parameters is deprecated')
      return this.initialItems
    }

    if (!semver.valid(semverValue) && !semver.validRange(semverValue)) {
      throw Object.assign(
        new Error(`\`${semverValue}\` is not a valid semver version or range`),
        { code: 'EQUERYINVALIDSEMVER' })
    }

    const valueIsVersion = !!semver.valid(semverValue)

    const nodeMatches = (node, obj) => {
      // if we already have an operator, the user provided some test as part of the selector
      // we evaluate that first because if it fails we don't want this node anyway
      if (attributeMatcher.operator) {
        if (!attributeMatch(attributeMatcher, obj)) {
          // if the initial operator doesn't match, we're done
          return false
        }
      }

      const attrValue = obj[qualifiedAttribute]
      // both valid and validRange return null for undefined, so this will skip both nodes that
      // do not have the attribute defined as well as those where the attribute value is invalid
      // and those where the value from the package.json is not a string
      if ((!semver.valid(attrValue) && !semver.validRange(attrValue)) ||
          typeof attrValue !== 'string') {
        return false
      }

      const attrIsVersion = !!semver.valid(attrValue)

      let actualFunc = semverFunc

      // if we're asked to infer, we examine outputs to make a best guess
      if (actualFunc === 'infer') {
        if (valueIsVersion && attrIsVersion) {
          // two versions -> semver.eq
          actualFunc = 'eq'
        } else if (!valueIsVersion && !attrIsVersion) {
          // two ranges -> semver.intersects
          actualFunc = 'intersects'
        } else {
          // anything else -> semver.satisfies
          actualFunc = 'satisfies'
        }
      }

      if (['eq', 'neq', 'gt', 'gte', 'lt', 'lte'].includes(actualFunc)) {
        // both sides must be versions, but one is not
        if (!valueIsVersion || !attrIsVersion) {
          return false
        }

        return semver[actualFunc](attrValue, semverValue)
      } else if (['gtr', 'ltr', 'satisfies'].includes(actualFunc)) {
        // at least one side must be a version, but neither is
        if (!valueIsVersion && !attrIsVersion) {
          return false
        }

        return valueIsVersion
          ? semver[actualFunc](semverValue, attrValue)
          : semver[actualFunc](attrValue, semverValue)
      } else if (['intersects', 'subset'].includes(actualFunc)) {
        // these accept two ranges and since a version is also a range, anything goes
        return semver[actualFunc](attrValue, semverValue)
      } else {
        // user provided a function we don't know about, throw an error
        throw Object.assign(new Error(`\`semver.${actualFunc}\` is not a supported operator.`),
          { code: 'EQUERYINVALIDOPERATOR' })
      }
    }

    return this.initialItems.filter((node) => {
      // no lookupProperties just means its a top level property, see if it matches
      if (!lookupProperties.length) {
        return nodeMatches(node, node.package)
      }

      // this code is mostly duplicated from attrPseudo to traverse into the package until we get
      // to our deepest requested object
      let objs = [node.package]
      for (const prop of lookupProperties) {
        if (prop === arrayDelimiter) {
          objs = objs.flat()
          continue
        }

        objs = objs.flatMap(obj => obj[prop] || [])
        const noAttr = objs.every(obj => !obj)
        if (noAttr) {
          return false
        }

        return objs.some(obj => nodeMatches(node, obj))
      }
    })
  }

  typePseudo () {
    if (!this.currentAstNode.typeValue) {
      return this.initialItems
    }
    return this.initialItems
      .flatMap(node => {
        const found = []
        for (const edge of node.edgesIn) {
          if (npa(`${edge.name}@${edge.spec}`).type === this.currentAstNode.typeValue) {
            found.push(edge.to)
          }
        }
        return found
      })
  }

  dedupedPseudo () {
    return this.initialItems.filter(node => node.target.edgesIn.size > 1)
  }

  async vulnPseudo () {
    if (!this.initialItems.length) {
      return this.initialItems
    }
    if (!this.#vulnCache) {
      const packages = {}
      // We have to map the items twice, once to get the request, and a second time to filter out the results of that request
      this.initialItems.map((node) => {
        if (node.isProjectRoot || node.package.private) {
          return
        }
        if (!packages[node.name]) {
          packages[node.name] = []
        }
        if (!packages[node.name].includes(node.version)) {
          packages[node.name].push(node.version)
        }
      })
      const res = await npmFetch('/-/npm/v1/security/advisories/bulk', {
        ...this.flatOptions,
        registry: this.flatOptions.auditRegistry || this.flatOptions.registry,
        method: 'POST',
        gzip: true,
        body: packages,
      })
      this.#vulnCache = await res.json()
    }
    const advisories = this.#vulnCache
    const { vulns } = this.currentAstNode
    return this.initialItems.filter(item => {
      const vulnerable = advisories[item.name]?.filter(advisory => {
        // This could be for another version of this package elsewhere in the tree
        if (!semver.intersects(advisory.vulnerable_versions, item.version)) {
          return false
        }
        if (!vulns) {
          return true
        }
        // vulns are OR with each other, if any one matches we're done
        for (const vuln of vulns) {
          if (vuln.severity && !vuln.severity.includes('*')) {
            if (!vuln.severity.includes(advisory.severity)) {
              continue
            }
          }

          if (vuln?.cwe) {
            // * is special, it means "has a cwe"
            if (vuln.cwe.includes('*')) {
              if (!advisory.cwe.length) {
                continue
              }
            } else if (!vuln.cwe.every(cwe => advisory.cwe.includes(`CWE-${cwe}`))) {
              continue
            }
          }
          return true
        }
      })
      if (vulnerable?.length) {
        item.queryContext = {
          advisories: vulnerable,
        }
        return true
      }
      return false
    })
  }

  async outdatedPseudo () {
    const { outdatedKind = 'any' } = this.currentAstNode

    // filter the initialItems
    // NOTE: this uses a Promise.all around a map without in-line concurrency handling
    // since the only async action taken is retrieving the packument, which is limited
    // based on the max-sockets config in make-fetch-happen
    const initialResults = await Promise.all(this.initialItems.map(async (node) => {
      // the root can't be outdated, skip it
      if (node.isProjectRoot) {
        return false
      }

      // private packages can't be published, skip them
      if (node.package.private) {
        return false
      }

      // we cache the promise representing the full versions list, this helps reduce the
      // number of requests we send by keeping population of the cache in a single tick
      // making it less likely that multiple requests for the same package will be inflight
      if (!this.#outdatedCache.has(node.name)) {
        this.#outdatedCache.set(node.name, getPackageVersions(node.name, this.flatOptions))
      }
      const availableVersions = await this.#outdatedCache.get(node.name)

      // we attach _all_ versions to the queryContext to allow consumers to do their own
      // filtering and comparisons
      node.queryContext.versions = availableVersions

      // next we further reduce the set to versions that are greater than the current one
      const greaterVersions = availableVersions.filter((available) => {
        return semver.gt(available, node.version)
      })

      // no newer versions than the current one, drop this node from the result set
      if (!greaterVersions.length) {
        return false
      }

      // if we got here, we know that newer versions exist, if the kind is 'any' we're done
      if (outdatedKind === 'any') {
        return node
      }

      // look for newer versions that differ from current by a specific part of the semver version
      if (['major', 'minor', 'patch'].includes(outdatedKind)) {
        // filter the versions greater than our current one based on semver.diff
        const filteredVersions = greaterVersions.filter((version) => {
          return semver.diff(node.version, version) === outdatedKind
        })

        // no available versions are of the correct diff type
        if (!filteredVersions.length) {
          return false
        }

        return node
      }

      // look for newer versions that satisfy at least one edgeIn to this node
      if (outdatedKind === 'in-range') {
        const inRangeContext = []
        for (const edge of node.edgesIn) {
          const inRangeVersions = greaterVersions.filter((version) => {
            return semver.satisfies(version, edge.spec)
          })

          // this edge has no in-range candidates, just move on
          if (!inRangeVersions.length) {
            continue
          }

          inRangeContext.push({
            from: edge.from.location,
            versions: inRangeVersions,
          })
        }

        // if we didn't find at least one match, drop this node
        if (!inRangeContext.length) {
          return false
        }

        // now add to the context each version that is in-range for each edgeIn
        node.queryContext.outdated = {
          ...node.queryContext.outdated,
          inRange: inRangeContext,
        }

        return node
      }

      // look for newer versions that _do not_ satisfy at least one edgeIn
      if (outdatedKind === 'out-of-range') {
        const outOfRangeContext = []
        for (const edge of node.edgesIn) {
          const outOfRangeVersions = greaterVersions.filter((version) => {
            return !semver.satisfies(version, edge.spec)
          })

          // this edge has no out-of-range candidates, skip it
          if (!outOfRangeVersions.length) {
            continue
          }

          outOfRangeContext.push({
            from: edge.from.location,
            versions: outOfRangeVersions,
          })
        }

        // if we didn't add at least one thing to the context, this node is not a match
        if (!outOfRangeContext.length) {
          return false
        }

        // attach the out-of-range context to the node
        node.queryContext.outdated = {
          ...node.queryContext.outdated,
          outOfRange: outOfRangeContext,
        }

        return node
      }

      // any other outdatedKind is unknown and will never match
      return false
    }))

    // return an array with the holes for non-matching nodes removed
    return initialResults.filter(Boolean)
  }
}

// operators for attribute selectors
const attributeOperators = {
  // attribute value is equivalent
  '=' ({ attr, value }) {
    return attr === value
  },
  // attribute value contains word
  '~=' ({ attr, value }) {
    return (attr.match(/\w+/g) || []).includes(value)
  },
  // attribute value contains string
  '*=' ({ attr, value }) {
    return attr.includes(value)
  },
  // attribute value is equal or starts with
  '|=' ({ attr, value }) {
    return attr.startsWith(`${value}-`)
  },
  // attribute value starts with
  '^=' ({ attr, value }) {
    return attr.startsWith(value)
  },
  // attribute value ends with
  '$=' ({ attr, value }) {
    return attr.endsWith(value)
  },
}

const attributeOperator = ({ attr, value, insensitive, operator }) => {
  if (typeof attr === 'number') {
    attr = String(attr)
  }
  if (typeof attr !== 'string') {
    // It's an object or an array, bail
    return false
  }
  if (insensitive) {
    attr = attr.toLowerCase()
  }

  return attributeOperators[operator]({
    attr,
    insensitive,
    value,
  })
}

const attributeMatch = (matcher, obj) => {
  const insensitive = !!matcher.insensitive
  const operator = matcher.operator || ''
  const attribute = matcher.qualifiedAttribute
  let value = matcher.value || ''
  // return early if checking existence
  if (operator === '') {
    return Boolean(obj[attribute])
  }
  if (insensitive) {
    value = value.toLowerCase()
  }
  // in case the current object is an array
  // then we try to match every item in the array
  if (Array.isArray(obj[attribute])) {
    return obj[attribute].find((i, index) => {
      const attr = obj[attribute][index] || ''
      return attributeOperator({ attr, value, insensitive, operator })
    })
  } else {
    const attr = obj[attribute] || ''
    return attributeOperator({ attr, value, insensitive, operator })
  }
}

const edgeIsType = (node, type, seen = new Set()) => {
  for (const edgeIn of node.edgesIn) {
    // TODO Need a test with an infinite loop
    if (seen.has(edgeIn)) {
      continue
    }
    seen.add(edgeIn)
    if (edgeIn.type === type || edgeIn.from[type] || edgeIsType(edgeIn.from, type, seen)) {
      return true
    }
  }
  return false
}

const filterByType = (nodes, type) => {
  const found = []
  for (const node of nodes) {
    if (node[type] || edgeIsType(node, type)) {
      found.push(node)
    }
  }
  return found
}

const depTypes = {
  // dependency
  '.prod' (prevResults) {
    const found = []
    for (const node of prevResults) {
      if (!node.dev) {
        found.push(node)
      }
    }
    return found
  },
  // devDependency
  '.dev' (prevResults) {
    return filterByType(prevResults, 'dev')
  },
  // optionalDependency
  '.optional' (prevResults) {
    return filterByType(prevResults, 'optional')
  },
  // peerDependency
  '.peer' (prevResults) {
    return filterByType(prevResults, 'peer')
  },
  // workspace
  '.workspace' (prevResults) {
    return prevResults.filter(node => node.isWorkspace)
  },
  // bundledDependency
  '.bundled' (prevResults) {
    return prevResults.filter(node => node.inBundle)
  },
}

// checks if a given node has a direct parent in any of the nodes provided in
// the compare nodes array
const hasParent = (node, compareNodes) => {
  // All it takes is one so we loop and return on the first hit
  for (let compareNode of compareNodes) {
    if (compareNode.isLink) {
      compareNode = compareNode.target
    }

    // follows logical parent for link anscestors
    if (node.isTop && (node.resolveParent === compareNode)) {
      return true
    }
    // follows edges-in to check if they match a possible parent
    for (const edge of node.edgesIn) {
      if (edge && edge.from === compareNode) {
        return true
      }
    }
  }
  return false
}

// checks if a given node is a descendant of any of the nodes provided in the
// compareNodes array
const hasAscendant = (node, compareNodes, seen = new Set()) => {
  // TODO (future) loop over ancestry property
  if (hasParent(node, compareNodes)) {
    return true
  }

  if (node.isTop && node.resolveParent) {
    /* istanbul ignore if - investigate if linksIn check obviates need for this */
    if (hasAscendant(node.resolveParent, compareNodes)) {
      return true
    }
  }
  for (const edge of node.edgesIn) {
    // TODO Need a test with an infinite loop
    if (seen.has(edge)) {
      continue
    }
    seen.add(edge)
    if (edge && edge.from && hasAscendant(edge.from, compareNodes, seen)) {
      return true
    }
  }
  for (const linkNode of node.linksIn) {
    if (hasAscendant(linkNode, compareNodes, seen)) {
      return true
    }
  }
  return false
}

const combinators = {
  // direct descendant
  '>' (prevResults, nextResults) {
    return nextResults.filter(node => hasParent(node, prevResults))
  },
  // any descendant
  ' ' (prevResults, nextResults) {
    return nextResults.filter(node => hasAscendant(node, prevResults))
  },
  // sibling
  '~' (prevResults, nextResults) {
    // Return any node in nextResults that is a sibling of (aka shares a
    // parent with) a node in prevResults
    const parentNodes = new Set() // Parents of everything in prevResults
    for (const node of prevResults) {
      for (const edge of node.edgesIn) {
        // edge.from always exists cause it's from another node's edgesIn
        parentNodes.add(edge.from)
      }
    }
    return nextResults.filter(node =>
      !prevResults.includes(node) && hasParent(node, [...parentNodes])
    )
  },
}

// get a list of available versions of a package filtered to respect --before
// NOTE: this runs over each node and should not throw
const getPackageVersions = async (name, opts) => {
  let packument
  try {
    packument = await pacote.packument(name, {
      ...opts,
      fullMetadata: false, // we only need the corgi
    })
  } catch (err) {
    // if the fetch fails, log a warning and pretend there are no versions
    log.warn('query', `could not retrieve packument for ${name}: ${err.message}`)
    return []
  }

  // start with a sorted list of all versions (lowest first)
  let candidates = Object.keys(packument.versions).sort(semver.compare)

  // if the packument has a time property, and the user passed a before flag, then
  // we filter this list down to only those versions that existed before the specified date
  if (packument.time && opts.before) {
    candidates = candidates.filter((version) => {
      // this version isn't found in the times at all, drop it
      if (!packument.time[version]) {
        return false
      }

      return Date.parse(packument.time[version]) <= opts.before
    })
  }

  return candidates
}

const retrieveNodesFromParsedAst = async (opts) => {
  // when we first call this it's the parsed query.  all other times it's
  // results.currentNode.nestedNode
  const rootAstNode = opts.rootAstNode

  if (!rootAstNode.nodes) {
    return new Set()
  }

  const results = new Results(opts)

  const astNodeQueue = new Set()
  // walk is sync, so we have to build up our async functions and then await them later
  rootAstNode.walk((nextAstNode) => {
    astNodeQueue.add(nextAstNode)
  })

  for (const nextAstNode of astNodeQueue) {
    // This is the only place we reset currentAstNode
    results.currentAstNode = nextAstNode
    const updateFn = `${results.currentAstNode.type}Type`
    if (typeof results[updateFn] !== 'function') {
      throw Object.assign(
        new Error(`\`${results.currentAstNode.type}\` is not a supported selector.`),
        { code: 'EQUERYNOSELECTOR' }
      )
    }
    await results[updateFn]()
  }

  return results.collect(rootAstNode)
}

const querySelectorAll = async (targetNode, query, flatOptions) => {
  // This never changes ever we just pass it around. But we can't scope it to
  // this whole file if we ever want to support concurrent calls to this
  // function.
  const inventory = [...targetNode.root.inventory.values()]
  // res is a Set of items returned for each parsed css ast selector
  const res = await retrieveNodesFromParsedAst({
    initialItems: inventory,
    inventory,
    flatOptions,
    rootAstNode: parser(query),
    targetNode,
  })

  // returns nodes ordered by realpath
  return [...res].sort((a, b) => localeCompare(a.location, b.location))
}

module.exports = querySelectorAll
