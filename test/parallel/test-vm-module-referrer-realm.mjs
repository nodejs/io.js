// Flags: --experimental-vm-modules
import * as common from '../common/index.mjs';
import assert from 'node:assert';
import { Script, SourceTextModule, createContext } from 'node:vm';

async function test() {
  const foo = new SourceTextModule('export const a = 1;');
  await foo.link(common.mustNotCall());
  await foo.evaluate();

  const ctx = createContext({}, {
    importModuleDynamically: common.mustCall((specifier, wrap) => {
      assert.strictEqual(specifier, 'foo');
      assert.strictEqual(wrap, ctx);
      return foo;
    }, 2),
  });
  {
    const s = new Script('Promise.resolve("import(\'foo\')").then(eval)', {
      importModuleDynamically: common.mustNotCall(),
    });

    const result = s.runInContext(ctx);
    assert.strictEqual(await result, foo.namespace);
  }

  {
    const m = new SourceTextModule('globalThis.fooResult = Promise.resolve("import(\'foo\')").then(eval)', {
      context: ctx,
      importModuleDynamically: common.mustNotCall(),
    });
    await m.link(common.mustNotCall());
    await m.evaluate();
    assert.strictEqual(await ctx.fooResult, foo.namespace);
    delete ctx.fooResult;
  }
}

async function testMissing() {
  const ctx = createContext({});
  {
    const s = new Script('Promise.resolve("import(\'foo\')").then(eval)', {
      importModuleDynamically: common.mustNotCall(),
    });

    const result = s.runInContext(ctx);
    await assert.rejects(result, {
      code: 'ERR_VM_DYNAMIC_IMPORT_CALLBACK_MISSING',
    });
  }

  {
    const m = new SourceTextModule('globalThis.fooResult = Promise.resolve("import(\'foo\')").then(eval)', {
      context: ctx,
      importModuleDynamically: common.mustNotCall(),
    });
    await m.link(common.mustNotCall());
    await m.evaluate();

    await assert.rejects(ctx.fooResult, {
      code: 'ERR_VM_DYNAMIC_IMPORT_CALLBACK_MISSING',
    });
    delete ctx.fooResult;
  }
}

await Promise.all([
  test(),
  testMissing(),
]).then(common.mustCall());
