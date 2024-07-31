'use strict';
const common = require('../common');
const process = require('process');

let defaultShell;
if (process.platform === 'linux' || process.platform === 'darwin') {
  defaultShell = '/bin/sh';
} else if (process.platform === 'win32') {
  defaultShell = 'cmd.exe';
} else {
  common.skip('This is test exists only on Linux/Win32/macOS');
}

const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const tmpdir = require('../common/tmpdir');

const tmpDir = tmpdir.path;
tmpdir.refresh();
const tmpCmdFile = path.join(tmpDir, 'test-stdin-from-file-spawn-cmd');
const tmpJsFile = path.join(tmpDir, 'test-stdin-from-file-spawn.js');
fs.writeFileSync(tmpCmdFile, 'echo hello');
fs.writeFileSync(tmpJsFile, `
'use strict';
const { spawn } = require('child_process');
// Reference the object to invoke the getter
process.stdin;
setTimeout(() => {
  let ok = false;
  const child = spawn(process.env.SHELL || '${defaultShell}',
    [], { stdio: ['inherit', 'pipe'] });
  child.stdout.on('data', () => {
    ok = true;
  });
  child.on('close', () => {
    process.exit(ok ? 0 : -1);
  });
}, 100);
`);

execSync(`${process.argv[0]} ${tmpJsFile} < ${tmpCmdFile}`);
