'use strict';

const {
  ArrayPrototypeMap,
  ArrayPrototypeSort,
  ObjectFromEntries,
  ObjectKeys,
  StringPrototypeReplace,
} = primordials;

const {
  getCLIOptionsValues,
  getCLIOptionsInfo,
  getEmbedderOptions: getEmbedderOptionsFromBinding,
  getEnvOptionsInputType,
} = internalBinding('options');

let warnOnAllowUnauthorized = true;

let optionsDict;
let cliInfo;
let embedderOptions;

// getCLIOptionsValues() would serialize the option values from C++ land.
// It would error if the values are queried before bootstrap is
// complete so that we don't accidentally include runtime-dependent
// states into a runtime-independent snapshot.
function getCLIOptionsFromBinding() {
  return optionsDict ??= getCLIOptionsValues();
}

function getCLIOptionsInfoFromBinding() {
  return cliInfo ??= getCLIOptionsInfo();
}

function getEmbedderOptions() {
  return embedderOptions ??= getEmbedderOptionsFromBinding();
}

function generateConfigJsonSchema() {
  const map = getEnvOptionsInputType();

  const schema = {
    __proto__: null,
    $schema: 'https://json-schema.org/draft/2020-12/schema',
    additionalProperties: false,
    properties: {
      __proto__: null,
    },
    type: 'object',
  };

  for (const { 0: key, 1: type } of map) {
    const keyWithoutPrefix = StringPrototypeReplace(key, '--', '');
    if (type === 'array') {
      schema.properties[keyWithoutPrefix] = {
        __proto__: null,
        oneOf: [
          { __proto__: null, type: 'string' },
          { __proto__: null, type: 'array', items: { __proto__: null, type: 'string', minItems: 1 } },
        ],
      };
    } else {
      schema.properties[keyWithoutPrefix] = { __proto__: null, type };
    }
  }

  // Sort the proerties by key alphabetically.
  const sortedKeys = ArrayPrototypeSort(ObjectKeys(schema.properties));
  const sortedProperties = ObjectFromEntries(
    ArrayPrototypeMap(sortedKeys, (key) => [key, schema.properties[key]]),
  );

  schema.properties = sortedProperties;

  return schema;
}

function refreshOptions() {
  optionsDict = undefined;
}

function getOptionValue(optionName) {
  return getCLIOptionsFromBinding()[optionName];
}

function getAllowUnauthorized() {
  const allowUnauthorized = process.env.NODE_TLS_REJECT_UNAUTHORIZED === '0';

  if (allowUnauthorized && warnOnAllowUnauthorized) {
    warnOnAllowUnauthorized = false;
    process.emitWarning(
      'Setting the NODE_TLS_REJECT_UNAUTHORIZED ' +
      'environment variable to \'0\' makes TLS connections ' +
      'and HTTPS requests insecure by disabling ' +
      'certificate verification.');
  }
  return allowUnauthorized;
}

module.exports = {
  getCLIOptionsInfo: getCLIOptionsInfoFromBinding,
  getOptionValue,
  getAllowUnauthorized,
  getEmbedderOptions,
  generateConfigJsonSchema,
  refreshOptions,
};
