"use strict";

const _excluded = ["babelOptions", "ecmaVersion", "sourceType", "requireConfigFile"];
function _objectWithoutPropertiesLoose(source, excluded) { if (source == null) return {}; var target = {}; var sourceKeys = Object.keys(source); var key, i; for (i = 0; i < sourceKeys.length; i++) { key = sourceKeys[i]; if (excluded.indexOf(key) >= 0) continue; target[key] = source[key]; } return target; }
module.exports = function normalizeESLintConfig(options) {
  const {
      babelOptions = {},
      ecmaVersion = 2020,
      sourceType = "module",
      requireConfigFile = true
    } = options,
    otherOptions = _objectWithoutPropertiesLoose(options, _excluded);
  return Object.assign({
    babelOptions: Object.assign({
      cwd: process.cwd()
    }, babelOptions),
    ecmaVersion: ecmaVersion === "latest" ? 1e8 : ecmaVersion,
    sourceType,
    requireConfigFile
  }, otherOptions);
};

//# sourceMappingURL=configuration.cjs.map
