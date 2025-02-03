'use strict';
require('../common');
const assert = require('assert');
const path = require('path');

assert.strictEqual(path.win32.normalize('./fixtures///b/../b/c.js'),
                   'fixtures\\b\\c.js');
assert.strictEqual(path.win32.normalize('/foo/../../../bar'), '\\bar');
assert.strictEqual(path.win32.normalize('a//b//../b'), 'a\\b');
assert.strictEqual(path.win32.normalize('a//b//./c'), 'a\\b\\c');
assert.strictEqual(path.win32.normalize('a//b//.'), 'a\\b');
assert.strictEqual(path.win32.normalize('//server/share/dir/file.ext'),
                   '\\\\server\\share\\dir\\file.ext');
assert.strictEqual(path.win32.normalize('/a/b/c/../../../x/y/z'), '\\x\\y\\z');
assert.strictEqual(path.win32.normalize('C:'), 'C:.');
assert.strictEqual(path.win32.normalize('C:..\\abc'), 'C:..\\abc');
assert.strictEqual(path.win32.normalize('C:..\\..\\abc\\..\\def'),
                   'C:..\\..\\def');
assert.strictEqual(path.win32.normalize('C:\\.'), 'C:\\');
assert.strictEqual(path.win32.normalize('file:stream'), 'file:stream');
assert.strictEqual(path.win32.normalize('bar\\foo..\\..\\'), 'bar\\');
assert.strictEqual(path.win32.normalize('bar\\foo..\\..'), 'bar');
assert.strictEqual(path.win32.normalize('bar\\foo..\\..\\baz'), 'bar\\baz');
assert.strictEqual(path.win32.normalize('bar\\foo..\\'), 'bar\\foo..\\');
assert.strictEqual(path.win32.normalize('bar\\foo..'), 'bar\\foo..');
assert.strictEqual(path.win32.normalize('..\\foo..\\..\\..\\bar'),
                   '..\\..\\bar');
assert.strictEqual(path.win32.normalize('..\\...\\..\\.\\...\\..\\..\\bar'),
                   '..\\..\\bar');
assert.strictEqual(path.win32.normalize('../../../foo/../../../bar'),
                   '..\\..\\..\\..\\..\\bar');
assert.strictEqual(path.win32.normalize('../../../foo/../../../bar/../../'),
                   '..\\..\\..\\..\\..\\..\\');
assert.strictEqual(
  path.win32.normalize('../foobar/barfoo/foo/../../../bar/../../'),
  '..\\..\\'
);
assert.strictEqual(
  path.win32.normalize('../.../../foobar/../../../bar/../../baz'),
  '..\\..\\..\\..\\baz'
);
assert.strictEqual(path.win32.normalize('foo/bar\\baz'), 'foo\\bar\\baz');
assert.strictEqual(path.win32.normalize('\\\\.\\foo'), '\\\\.\\foo');
assert.strictEqual(path.win32.normalize('\\\\.\\foo\\'), '\\\\.\\foo\\');

// Tests related to CVE-2024-36139. Path traversal should not result in changing
// the root directory on Windows.
assert.strictEqual(path.win32.normalize('test/../C:/Windows'), '.\\C:\\Windows');
assert.strictEqual(path.win32.normalize('test/../C:Windows'), '.\\C:Windows');
assert.strictEqual(path.win32.normalize('./upload/../C:/Windows'), '.\\C:\\Windows');
assert.strictEqual(path.win32.normalize('./upload/../C:x'), '.\\C:x');
assert.strictEqual(path.win32.normalize('test/../??/D:/Test'), '.\\??\\D:\\Test');
assert.strictEqual(path.win32.normalize('test/C:/../../F:'), '.\\F:');
assert.strictEqual(path.win32.normalize('test/C:foo/../../F:'), '.\\F:');
assert.strictEqual(path.win32.normalize('test/C:/../../F:\\'), '.\\F:\\');
assert.strictEqual(path.win32.normalize('test/C:foo/../../F:\\'), '.\\F:\\');
assert.strictEqual(path.win32.normalize('test/C:/../../F:x'), '.\\F:x');
assert.strictEqual(path.win32.normalize('test/C:foo/../../F:x'), '.\\F:x');
assert.strictEqual(path.win32.normalize('/test/../??/D:/Test'), '\\??\\D:\\Test');
assert.strictEqual(path.win32.normalize('/test/../?/D:/Test'), '\\?\\D:\\Test');
assert.strictEqual(path.win32.normalize('//test/../??/D:/Test'), '\\\\test\\..\\??\\D:\\Test');
assert.strictEqual(path.win32.normalize('//test/../?/D:/Test'), '\\\\test\\..\\?\\D:\\Test');
assert.strictEqual(path.win32.normalize('\\\\?\\test/../?/D:/Test'), '\\\\?\\?\\D:\\Test');
assert.strictEqual(path.win32.normalize('\\\\?\\test/../../?/D:/Test'), '\\\\?\\?\\D:\\Test');
assert.strictEqual(path.win32.normalize('\\\\.\\test/../?/D:/Test'), '\\\\.\\?\\D:\\Test');
assert.strictEqual(path.win32.normalize('\\\\.\\test/../../?/D:/Test'), '\\\\.\\?\\D:\\Test');
assert.strictEqual(path.win32.normalize('//server/share/dir/../../../?/D:/file'),
                   '\\\\server\\share\\?\\D:\\file');
assert.strictEqual(path.win32.normalize('//server/goodshare/../badshare/file'),
                   '\\\\server\\goodshare\\badshare\\file');

assert.strictEqual(path.posix.normalize('./fixtures///b/../b/c.js'),
                   'fixtures/b/c.js');
assert.strictEqual(path.posix.normalize('/foo/../../../bar'), '/bar');
assert.strictEqual(path.posix.normalize('a//b//../b'), 'a/b');
assert.strictEqual(path.posix.normalize('a//b//./c'), 'a/b/c');
assert.strictEqual(path.posix.normalize('a//b//.'), 'a/b');
assert.strictEqual(path.posix.normalize('/a/b/c/../../../x/y/z'), '/x/y/z');
assert.strictEqual(path.posix.normalize('///..//./foo/.//bar'), '/foo/bar');
assert.strictEqual(path.posix.normalize('bar/foo../../'), 'bar/');
assert.strictEqual(path.posix.normalize('bar/foo../..'), 'bar');
assert.strictEqual(path.posix.normalize('bar/foo../../baz'), 'bar/baz');
assert.strictEqual(path.posix.normalize('bar/foo../'), 'bar/foo../');
assert.strictEqual(path.posix.normalize('bar/foo..'), 'bar/foo..');
assert.strictEqual(path.posix.normalize('../foo../../../bar'), '../../bar');
assert.strictEqual(path.posix.normalize('../.../.././.../../../bar'),
                   '../../bar');
assert.strictEqual(path.posix.normalize('../../../foo/../../../bar'),
                   '../../../../../bar');
assert.strictEqual(path.posix.normalize('../../../foo/../../../bar/../../'),
                   '../../../../../../');
assert.strictEqual(
  path.posix.normalize('../foobar/barfoo/foo/../../../bar/../../'),
  '../../'
);
assert.strictEqual(
  path.posix.normalize('../.../../foobar/../../../bar/../../baz'),
  '../../../../baz'
);
assert.strictEqual(path.posix.normalize('foo/bar\\baz'), 'foo/bar\\baz');
