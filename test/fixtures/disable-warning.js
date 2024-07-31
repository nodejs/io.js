'use strict';

process.emitWarning('Deprecation Warning 1', {
  code: 'DEP1',
  type: 'DeprecationWarning'
});

process.emitWarning('Deprecation Warning 2', {
  code: 'DEP2',
  type: 'DeprecationWarning'
});

process.emitWarning('Experimental Warning', {
  type: 'ExperimentalWarning'
});
