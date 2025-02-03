'use strict';

const {
  getCLIOptionsValues,
  getCLIOptionsInfo,
  getEmbedderOptions: getEmbedderOptionsFromBinding,
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
  refreshOptions,
};
