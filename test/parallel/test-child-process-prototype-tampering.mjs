import * as common from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import { EOL } from 'node:os';
import { strictEqual, notStrictEqual, throws } from 'node:assert';
import cp from 'node:child_process';

// TODO(LiviaMedeiros): test on different platforms
if (!common.isLinux)
  common.skip();

const expectedCWD = process.cwd();
const expectedUID = process.getuid();

for (const tamperedCwd of ['', '/tmp', '/not/existing/malicious/path', 42n]) {
  Object.prototype.cwd = tamperedCwd;

  cp.exec('pwd', common.mustSucceed((out) => {
    strictEqual(`${out}`, `${expectedCWD}${EOL}`);
  }));
  strictEqual(`${cp.execSync('pwd')}`, `${expectedCWD}${EOL}`);
  cp.execFile('pwd', common.mustSucceed((out) => {
    strictEqual(`${out}`, `${expectedCWD}${EOL}`);
  }));
  strictEqual(`${cp.execFileSync('pwd')}`, `${expectedCWD}${EOL}`);
  cp.spawn('pwd').stdout.on('data', common.mustCall((out) => {
    strictEqual(`${out}`, `${expectedCWD}${EOL}`);
  }));
  strictEqual(`${cp.spawnSync('pwd').stdout}`, `${expectedCWD}${EOL}`);

  delete Object.prototype.cwd;
}

for (const tamperedUID of [0, 1, 999, 1000, 0n, 'gwak']) {
  Object.prototype.uid = tamperedUID;

  cp.exec('id -u', common.mustSucceed((out) => {
    strictEqual(`${out}`, `${expectedUID}${EOL}`);
  }));
  strictEqual(`${cp.execSync('id -u')}`, `${expectedUID}${EOL}`);
  cp.execFile('id', ['-u'], common.mustSucceed((out) => {
    strictEqual(`${out}`, `${expectedUID}${EOL}`);
  }));
  strictEqual(`${cp.execFileSync('id', ['-u'])}`, `${expectedUID}${EOL}`);
  cp.spawn('id', ['-u']).stdout.on('data', common.mustCall((out) => {
    strictEqual(`${out}`, `${expectedUID}${EOL}`);
  }));
  strictEqual(`${cp.spawnSync('id', ['-u']).stdout}`, `${expectedUID}${EOL}`);

  delete Object.prototype.uid;
}

{
  Object.prototype.execPath = '/not/existing/malicious/path';

  // Does not throw ENOENT
  cp.fork(fixtures.path('empty.js'));

  delete Object.prototype.execPath;
}

for (const shellCommandArgument of ['-L && echo "tampered"']) {
  Object.prototype.shell = true;
  const cmd = 'pwd';
  let cmdExitCode = '';

  const program = cp.spawn(cmd, [shellCommandArgument], { cwd: expectedCWD });
  program.stderr.on('data', common.mustCall());
  program.stdout.on('data', common.mustNotCall());

  program.on('exit', common.mustCall((code) => {
    notStrictEqual(code, 0);
  }));

  cp.execFile(cmd, [shellCommandArgument], { cwd: expectedCWD },
              common.mustCall((err) => {
                notStrictEqual(err.code, 0);
              })
  );

  throws(() => {
    cp.execFileSync(cmd, [shellCommandArgument], { cwd: expectedCWD });
  }, (e) => {
    notStrictEqual(e.status, 0);
    return true;
  });

  cmdExitCode = cp.spawnSync(cmd, [shellCommandArgument], { cwd: expectedCWD }).status;
  notStrictEqual(cmdExitCode, 0);

  delete Object.prototype.shell;
}
