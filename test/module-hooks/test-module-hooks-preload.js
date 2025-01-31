'use strict';

require('../common');
const fixtures = require('../common/fixtures.js');
const { spawnSyncAndAssert } = require('../common/child_process.js');

spawnSyncAndAssert(process.execPath,
                   [
                     '--require',
                     fixtures.path('module-hooks', 'register-typescript-hooks.js'),
                     fixtures.path('module-hooks', 'log-user.cts'),
                   ], {
                     trim: true,
                     stdout: 'UserAccount { name: \'john\', id: 100, type: 1 }',
                   });

spawnSyncAndAssert(process.execPath,
                   [
                     '--no-experimental-transform-types',
                     '--require',
                     fixtures.path('module-hooks', 'register-typescript-hooks.js'),
                     fixtures.path('module-hooks', 'log-user.cts'),
                   ], {
                     trim: true,
                     stdout: 'UserAccount { name: \'john\', id: 100, type: 1 }',
                   });

spawnSyncAndAssert(process.execPath,
                   [
                     '--import',
                     fixtures.fileURL('module-hooks', 'register-typescript-hooks.js'),
                     fixtures.path('module-hooks', 'log-user.mts'),
                   ], {
                     trim: true,
                     stdout: 'UserAccount { name: \'john\', id: 100, type: 1 }',
                   });

spawnSyncAndAssert(process.execPath,
                   [
                     '--no-experimental-transform-types',
                     '--import',
                     fixtures.fileURL('module-hooks', 'register-typescript-hooks.js'),
                     fixtures.path('module-hooks', 'log-user.mts'),
                   ], {
                     trim: true,
                     stdout: 'UserAccount { name: \'john\', id: 100, type: 1 }',
                   });
