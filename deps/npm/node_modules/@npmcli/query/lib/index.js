'use strict'

const parser = require('postcss-selector-parser')

const arrayDelimiter = Symbol('arrayDelimiter')

const escapeSlashes = str =>
  str.replace(/\//g, '\\/')

const unescapeSlashes = str =>
  str.replace(/\\\//g, '/')

// recursively fixes up any :attr pseudo-class found
const fixupAttr = astNode => {
  const properties = []
  const matcher = {}
  for (const selectorAstNode of astNode.nodes) {
    const [firstAstNode] = selectorAstNode.nodes
    if (firstAstNode.type === 'tag') {
      properties.push(firstAstNode.value)
    }
  }

  const lastSelectorAstNode = astNode.nodes.pop()
  const [attributeAstNode] = lastSelectorAstNode.nodes

  if (attributeAstNode.value === ':attr') {
    const appendParts = fixupAttr(attributeAstNode)
    properties.push(arrayDelimiter, ...appendParts.lookupProperties)
    matcher.qualifiedAttribute = appendParts.attributeMatcher.qualifiedAttribute
    matcher.operator = appendParts.attributeMatcher.operator
    matcher.value = appendParts.attributeMatcher.value

    // backwards compatibility
    matcher.attribute = appendParts.attributeMatcher.attribute

    if (appendParts.attributeMatcher.insensitive) {
      matcher.insensitive = true
    }
  } else {
    if (attributeAstNode.type !== 'attribute') {
      throw Object.assign(
        new Error('`:attr` pseudo-class expects an attribute matcher as the last value'),
        { code: 'EQUERYATTR' }
      )
    }

    matcher.qualifiedAttribute = unescapeSlashes(attributeAstNode.qualifiedAttribute)
    matcher.operator = attributeAstNode.operator
    matcher.value = attributeAstNode.value

    // backwards compatibility
    matcher.attribute = matcher.qualifiedAttribute

    if (attributeAstNode.insensitive) {
      matcher.insensitive = true
    }
  }

  astNode.lookupProperties = properties
  astNode.attributeMatcher = matcher
  astNode.nodes.length = 0
  return astNode
}

// fixed up nested pseudo nodes will have their internal selectors moved
// to a new root node that will be referenced by the `nestedNode` property,
// this tweak makes it simpler to reuse `retrieveNodesFromParsedAst` to
// recursively parse and extract results from the internal selectors
const fixupNestedPseudo = astNode => {
  // create a new ast root node and relocate any children
  // selectors of the current ast node to this new root
  const newRootNode = parser.root()
  astNode.nestedNode = newRootNode
  newRootNode.nodes = [...astNode.nodes]

  // clean up the ast by removing the children nodes from the
  // current ast node while also cleaning up their `parent` refs
  astNode.nodes.length = 0
  for (const currAstNode of newRootNode.nodes) {
    currAstNode.parent = newRootNode
  }

  // recursively fixup nodes of any nested selector
  transformAst(newRootNode)
}

// :semver(<version|range|selector>, [version|range|selector], [function])
// note: the first or second parameter must be a static version or range
const fixupSemverSpecs = astNode => {
  // if we have three nodes, the last is the semver function to use, pull that out first
  if (astNode.nodes.length === 3) {
    const funcNode = astNode.nodes.pop().nodes[0]
    if (funcNode.type === 'tag') {
      astNode.semverFunc = funcNode.value
    } else if (funcNode.type === 'string') {
      // a string is always in some type of quotes, we don't want those so slice them off
      astNode.semverFunc = funcNode.value.slice(1, -1)
    } else {
      // anything that isn't a tag or a string isn't a function name
      throw Object.assign(
        new Error('`:semver` pseudo-class expects a function name as last value'),
        { code: 'ESEMVERFUNC' }
      )
    }
  }

  // now if we have 1 node, it's a static value
  // istanbul ignore else
  if (astNode.nodes.length === 1) {
    const semverNode = astNode.nodes.pop()
    astNode.semverValue = semverNode.nodes.reduce((res, next) => `${res}${String(next)}`, '')
  } else if (astNode.nodes.length === 2) {
    // and if we have two nodes, one of them is a static value and we need to determine which it is
    for (let i = 0; i < astNode.nodes.length; ++i) {
      const type = astNode.nodes[i].nodes[0].type
      // the type of the first child may be combinator for ranges, such as >14
      if (type === 'tag' || type === 'combinator') {
        const semverNode = astNode.nodes.splice(i, 1)[0]
        astNode.semverValue = semverNode.nodes.reduce((res, next) => `${res}${String(next)}`, '')
        astNode.semverPosition = i
        break
      }
    }

    if (typeof astNode.semverValue === 'undefined') {
      throw Object.assign(
        new Error('`:semver` pseudo-class expects a static value in the first or second position'),
        { code: 'ESEMVERVALUE' }
      )
    }
  }

  // if we got here, the last remaining child should be attribute selector
  if (astNode.nodes.length === 1) {
    fixupAttr(astNode)
  } else {
    // if we don't have a selector, we default to `[version]`
    astNode.attributeMatcher = {
      insensitive: false,
      attribute: 'version',
      qualifiedAttribute: 'version',
    }
    astNode.lookupProperties = []
  }

  astNode.nodes.length = 0
}

const fixupTypes = astNode => {
  const [valueAstNode] = astNode.nodes[0].nodes
  const { value } = valueAstNode || {}
  astNode.typeValue = value
  astNode.nodes.length = 0
}

const fixupPaths = astNode => {
  astNode.pathValue = unescapeSlashes(String(astNode.nodes[0]))
  astNode.nodes.length = 0
}

const fixupOutdated = astNode => {
  if (astNode.nodes.length) {
    astNode.outdatedKind = String(astNode.nodes[0])
    astNode.nodes.length = 0
  }
}

const fixupVuln = astNode => {
  const vulns = []
  if (astNode.nodes.length) {
    for (const selector of astNode.nodes) {
      const vuln = {}
      for (const node of selector.nodes) {
        if (node.type !== 'attribute') {
          throw Object.assign(
            new Error(':vuln pseudo-class only accepts attribute matchers or "cwe" tag'),
            { code: 'EQUERYATTR' }
          )
        }
        if (!['severity', 'cwe'].includes(node._attribute)) {
          throw Object.assign(
            new Error(':vuln pseudo-class only matches "severity" and "cwe" attributes'),
            { code: 'EQUERYATTR' }
          )
        }
        if (!node.operator) {
          node.operator = '='
          node.value = '*'
        }
        if (node.operator !== '=') {
          throw Object.assign(
            new Error(':vuln pseudo-class attribute selector only accepts "=" operator', node),
            { code: 'EQUERYATTR' }
          )
        }
        if (!vuln[node._attribute]) {
          vuln[node._attribute] = []
        }
        vuln[node._attribute].push(node._value)
      }
      vulns.push(vuln)
    }
    astNode.vulns = vulns
    astNode.nodes.length = 0
  }
}

// a few of the supported ast nodes need to be tweaked in order to properly be
// interpreted as proper arborist query selectors, namely semver ranges from
// both ids and :semver pseudo-class selectors need to be translated from what
// are usually multiple ast nodes, such as: tag:1, class:.0, class:.0 to a
// single `1.0.0` value, other pseudo-class selectors also get preprocessed in
// order to make it simpler to execute later when traversing each ast node
// using rootNode.walk(), such as :path, :type, etc. transformAst handles all
// these modifications to the parsed ast by doing an extra, initial traversal
// of the parsed ast from the query and modifying the parsed nodes accordingly
const transformAst = selector => {
  selector.walk((nextAstNode) => {
    switch (nextAstNode.value) {
      case ':attr':
        return fixupAttr(nextAstNode)
      case ':is':
      case ':has':
      case ':not':
        return fixupNestedPseudo(nextAstNode)
      case ':path':
        return fixupPaths(nextAstNode)
      case ':semver':
        return fixupSemverSpecs(nextAstNode)
      case ':type':
        return fixupTypes(nextAstNode)
      case ':outdated':
        return fixupOutdated(nextAstNode)
      case ':vuln':
        return fixupVuln(nextAstNode)
    }
  })
}

const queryParser = (query) => {
  // if query is an empty string or any falsy
  // value, just returns an empty result
  if (!query) {
    return []
  }

  return parser(transformAst)
    .astSync(escapeSlashes(query), { lossless: false })
}

module.exports = {
  parser: queryParser,
  arrayDelimiter,
}
