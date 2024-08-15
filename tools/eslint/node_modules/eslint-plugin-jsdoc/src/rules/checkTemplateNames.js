import {
  parse as parseType,
  traverse,
  tryParse as tryParseType,
} from '@es-joy/jsdoccomment';
import iterateJsdoc from '../iterateJsdoc.js';

export default iterateJsdoc(({
  context,
  utils,
  node,
  settings,
  report,
}) => {
  const {
    mode
  } = settings;

  const templateTags = utils.getTags('template');

  const usedNames = new Set();
  /**
   * @param {import('@typescript-eslint/types').TSESTree.TSTypeAliasDeclaration} aliasDeclaration
   */
  const checkParameters = (aliasDeclaration) => {
    /* c8 ignore next -- Guard */
    const {params} = aliasDeclaration.typeParameters ?? {params: []};
    for (const {name: {name}} of params) {
      usedNames.add(name);
    }
    for (const tag of templateTags) {
      const {name} = tag;
      const names = name.split(/,\s*/);
      for (const name of names) {
        if (!usedNames.has(name)) {
          report(`@template ${name} not in use`, null, tag);
        }
      }
    }
  };

  const handleTypeAliases = () => {
    const nde = /** @type {import('@typescript-eslint/types').TSESTree.Node} */ (
      node
    );
    if (!nde) {
      return;
    }
    switch (nde.type) {
    case 'ExportNamedDeclaration':
      if (nde.declaration?.type === 'TSTypeAliasDeclaration') {
        checkParameters(nde.declaration);
      }
      break;
    case 'TSTypeAliasDeclaration':
      checkParameters(nde);
      break;
    }
  };

  const typedefTags = utils.getTags('typedef');
  if (!typedefTags.length || typedefTags.length >= 2) {
    handleTypeAliases();
    return;
  }

  /**
   * @param {string} potentialType
   */
  const checkForUsedTypes = (potentialType) => {
    let parsedType;
    try {
      parsedType = mode === 'permissive' ?
        tryParseType(/** @type {string} */ (potentialType)) :
        parseType(/** @type {string} */ (potentialType), mode);
    } catch {
      return;
    }

    traverse(parsedType, (nde) => {
      const {
        type,
        value,
      } = /** @type {import('jsdoc-type-pratt-parser').NameResult} */ (nde);
      if (type === 'JsdocTypeName' && (/^[A-Z]$/).test(value)) {
        usedNames.add(value);
      }
    });
  };

  const potentialTypedefType = typedefTags[0].type;
  checkForUsedTypes(potentialTypedefType);

  const tagName = /** @type {string} */ (utils.getPreferredTagName({
    tagName: 'property',
  }));
  const propertyTags = utils.getTags(tagName);
  for (const propertyTag of propertyTags) {
    checkForUsedTypes(propertyTag.type);
  }

  for (const tag of templateTags) {
    const {name} = tag;
    const names = name.split(/,\s*/);
    for (const name of names) {
      if (!usedNames.has(name)) {
        report(`@template ${name} not in use`, null, tag);
      }
    }
  }
}, {
  iterateAllJsdocs: true,
  meta: {
    docs: {
      description: 'Checks that any `@template` names are actually used in the connected `@typedef` or type alias.',
      url: 'https://github.com/gajus/eslint-plugin-jsdoc/blob/main/docs/rules/require-template.md#repos-sticky-header',
    },
    schema: [],
    type: 'suggestion',
  },
});
