'use strict';

const {
  Array,
  ArrayPrototypeJoin,
  ArrayPrototypeSome,
  FunctionPrototype,
  ObjectSetPrototypeOf,
  PromisePrototypeThen,
  PromiseResolve,
  ReflectApply,
  RegExpPrototypeExec,
  RegExpPrototypeSymbolReplace,
  SafePromiseAllReturnArrayLike,
  SafePromiseAllReturnVoid,
  SafeSet,
  StringPrototypeIncludes,
  StringPrototypeSplit,
  StringPrototypeStartsWith,
  globalThis,
} = primordials;
let debug = require('internal/util/debuglog').debuglog('esm', (fn) => {
  debug = fn;
});

const { ModuleWrap, kEvaluated } = internalBinding('module_wrap');
const {
  privateSymbols: {
    entry_point_module_private_symbol,
  },
} = internalBinding('util');
const { decorateErrorStack, kEmptyObject } = require('internal/util');
const {
  getSourceMapsEnabled,
} = require('internal/source_map/source_map_cache');
const assert = require('internal/assert');
const resolvedPromise = PromiseResolve();
const {
  setHasStartedUserESMExecution,
} = require('internal/modules/helpers');
const noop = FunctionPrototype;

let hasPausedEntry = false;

const CJSGlobalLike = [
  'require',
  'module',
  'exports',
  '__filename',
  '__dirname',
];
const isCommonJSGlobalLikeNotDefinedError = (errorMessage) =>
  ArrayPrototypeSome(
    CJSGlobalLike,
    (globalLike) => errorMessage === `${globalLike} is not defined`,
  );

class ModuleJobBase {
  constructor(url, importAttributes, moduleWrapMaybePromise, isMain, inspectBrk) {
    this.importAttributes = importAttributes;
    this.isMain = isMain;
    this.inspectBrk = inspectBrk;

    this.url = url;
    this.module = moduleWrapMaybePromise;
  }
}

/* A ModuleJob tracks the loading of a single Module, and the ModuleJobs of
 * its dependencies, over time. */
class ModuleJob extends ModuleJobBase {
  #loader = null;
  // `loader` is the Loader instance used for loading dependencies.
  constructor(loader, url, importAttributes = { __proto__: null },
              moduleProvider, isMain, inspectBrk, sync = false) {
    const modulePromise = ReflectApply(moduleProvider, loader, [url, isMain]);
    super(url, importAttributes, modulePromise, isMain, inspectBrk);
    this.#loader = loader;
    // Expose the promise to the ModuleWrap directly for linking below.
    // `this.module` is also filled in below.
    this.modulePromise = modulePromise;

    if (sync) {
      this.module = this.modulePromise;
      this.modulePromise = PromiseResolve(this.module);
    } else {
      this.modulePromise = PromiseResolve(this.modulePromise);
    }

    // Promise for the list of all dependencyJobs.
    this.linked = this._link();
    // This promise is awaited later anyway, so silence
    // 'unhandled rejection' warnings.
    PromisePrototypeThen(this.linked, undefined, noop);

    // instantiated == deep dependency jobs wrappers are instantiated,
    // and module wrapper is instantiated.
    this.instantiated = undefined;
  }

  /**
   * Iterates the module requests and links with the loader.
   * @returns {Promise<ModuleJob[]>} Dependency module jobs.
   */
  async _link() {
    this.module = await this.modulePromise;
    assert(this.module instanceof ModuleWrap);

    const moduleRequests = this.module.getModuleRequests();
    // Explicitly keeping track of dependency jobs is needed in order
    // to flatten out the dependency graph below in `_instantiate()`,
    // so that circular dependencies can't cause a deadlock by two of
    // these `link` callbacks depending on each other.
    // Create an ArrayLike to avoid calling into userspace with `.then`
    // when returned from the async function.
    const dependencyJobs = Array(moduleRequests.length);
    ObjectSetPrototypeOf(dependencyJobs, null);

    // Specifiers should be aligned with the moduleRequests array in order.
    const specifiers = Array(moduleRequests.length);
    const modulePromises = Array(moduleRequests.length);
    // Iterate with index to avoid calling into userspace with `Symbol.iterator`.
    for (let idx = 0; idx < moduleRequests.length; idx++) {
      const { specifier, attributes } = moduleRequests[idx];

      const dependencyJobPromise = this.#loader.getModuleJob(
        specifier, this.url, attributes,
      );
      const modulePromise = PromisePrototypeThen(dependencyJobPromise, (job) => {
        debug(`async link() ${this.url} -> ${specifier}`, job);
        dependencyJobs[idx] = job;
        return job.modulePromise;
      });
      modulePromises[idx] = modulePromise;
      specifiers[idx] = specifier;
    }

    const modules = await SafePromiseAllReturnArrayLike(modulePromises);
    this.module.link(specifiers, modules);

    return dependencyJobs;
  }

  instantiate() {
    if (this.instantiated === undefined) {
      this.instantiated = this._instantiate();
    }
    return this.instantiated;
  }

  async _instantiate() {
    const jobsInGraph = new SafeSet();
    const addJobsToDependencyGraph = async (moduleJob) => {
      debug(`async addJobsToDependencyGraph() ${this.url}`, moduleJob);

      if (jobsInGraph.has(moduleJob)) {
        return;
      }
      jobsInGraph.add(moduleJob);
      const dependencyJobs = await moduleJob.linked;
      return SafePromiseAllReturnVoid(dependencyJobs, addJobsToDependencyGraph);
    };
    await addJobsToDependencyGraph(this);

    try {
      if (!hasPausedEntry && this.inspectBrk) {
        hasPausedEntry = true;
        const initWrapper = internalBinding('inspector').callAndPauseOnStart;
        initWrapper(this.module.instantiate, this.module);
      } else {
        this.module.instantiate();
      }
    } catch (e) {
      decorateErrorStack(e);
      // TODO(@bcoe): Add source map support to exception that occurs as result
      // of missing named export. This is currently not possible because
      // stack trace originates in module_job, not the file itself. A hidden
      // symbol with filename could be set in node_errors.cc to facilitate this.
      if (!getSourceMapsEnabled() &&
          StringPrototypeIncludes(e.message,
                                  ' does not provide an export named')) {
        const splitStack = StringPrototypeSplit(e.stack, '\n');
        const parentFileUrl = RegExpPrototypeSymbolReplace(
          /:\d+$/,
          splitStack[0],
          '',
        );
        const { 1: childSpecifier, 2: name } = RegExpPrototypeExec(
          /module '(.*)' does not provide an export named '(.+)'/,
          e.message);
        const { url: childFileURL } = await this.#loader.resolve(
          childSpecifier,
          parentFileUrl,
          kEmptyObject,
        );
        let format;
        try {
          // This might throw for non-CommonJS modules because we aren't passing
          // in the import attributes and some formats require them; but we only
          // care about CommonJS for the purposes of this error message.
          ({ format } =
            await this.#loader.load(childFileURL));
        } catch {
          // Continue regardless of error.
        }

        if (format === 'commonjs') {
          const importStatement = splitStack[1];
          // TODO(@ctavan): The original error stack only provides the single
          // line which causes the error. For multi-line import statements we
          // cannot generate an equivalent object destructuring assignment by
          // just parsing the error stack.
          const oneLineNamedImports = RegExpPrototypeExec(/{.*}/, importStatement);
          const destructuringAssignment = oneLineNamedImports &&
            RegExpPrototypeSymbolReplace(/\s+as\s+/g, oneLineNamedImports, ': ');
          e.message = `Named export '${name}' not found. The requested module` +
            ` '${childSpecifier}' is a CommonJS module, which may not support` +
            ' all module.exports as named exports.\nCommonJS modules can ' +
            'always be imported via the default export, for example using:' +
            `\n\nimport pkg from '${childSpecifier}';\n${
              destructuringAssignment ?
                `const ${destructuringAssignment} = pkg;\n` : ''}`;
          const newStack = StringPrototypeSplit(e.stack, '\n');
          newStack[3] = `SyntaxError: ${e.message}`;
          e.stack = ArrayPrototypeJoin(newStack, '\n');
        }
      }
      throw e;
    }

    for (const dependencyJob of jobsInGraph) {
      // Calling `this.module.instantiate()` instantiates not only the
      // ModuleWrap in this module, but all modules in the graph.
      dependencyJob.instantiated = resolvedPromise;
    }
  }

  runSync() {
    assert(this.module instanceof ModuleWrap);
    if (this.instantiated !== undefined) {
      return { __proto__: null, module: this.module };
    }

    this.module.instantiate();
    this.instantiated = PromiseResolve();
    const timeout = -1;
    const breakOnSigint = false;
    setHasStartedUserESMExecution();
    this.module.evaluate(timeout, breakOnSigint);
    return { __proto__: null, module: this.module };
  }

  async run(isEntryPoint = false) {
    await this.instantiate();
    if (isEntryPoint) {
      globalThis[entry_point_module_private_symbol] = this.module;
    }
    const timeout = -1;
    const breakOnSigint = false;
    setHasStartedUserESMExecution();
    try {
      await this.module.evaluate(timeout, breakOnSigint);
    } catch (e) {
      if (e?.name === 'ReferenceError' &&
          isCommonJSGlobalLikeNotDefinedError(e.message)) {
        e.message += ' in ES module scope';

        if (StringPrototypeStartsWith(e.message, 'require ')) {
          e.message += ', you can use import instead';
        }

        const packageConfig =
          StringPrototypeStartsWith(this.module.url, 'file://') &&
            RegExpPrototypeExec(/\.js(\?[^#]*)?(#.*)?$/, this.module.url) !== null &&
            require('internal/modules/package_json_reader')
              .getPackageScopeConfig(this.module.url);
        if (packageConfig.type === 'module') {
          e.message +=
            '\nThis file is being treated as an ES module because it has a ' +
            `'.js' file extension and '${packageConfig.pjsonPath}' contains ` +
            '"type": "module". To treat it as a CommonJS script, rename it ' +
            'to use the \'.cjs\' file extension.';
        }
      }
      throw e;
    }
    return { __proto__: null, module: this.module };
  }
}

// This is a fully synchronous job and does not spawn additional threads in any way.
// All the steps are ensured to be synchronous and it throws on instantiating
// an asynchronous graph.
class ModuleJobSync extends ModuleJobBase {
  #loader = null;
  constructor(loader, url, importAttributes, moduleWrap, isMain, inspectBrk) {
    super(url, importAttributes, moduleWrap, isMain, inspectBrk, true);
    this.#loader = loader;

    assert(this.module instanceof ModuleWrap);
    // Store itself into the cache first before linking in case there are circular
    // references in the linking.
    loader.loadCache.set(url, importAttributes.type, this);

    try {
      const moduleRequests = this.module.getModuleRequests();
      // Specifiers should be aligned with the moduleRequests array in order.
      const specifiers = Array(moduleRequests.length);
      const modules = Array(moduleRequests.length);
      const jobs = Array(moduleRequests.length);
      for (let i = 0; i < moduleRequests.length; ++i) {
        const { specifier, attributes } = moduleRequests[i];
        const job = this.#loader.getModuleJobForRequire(specifier, url, attributes);
        specifiers[i] = specifier;
        modules[i] = job.module;
        jobs[i] = job;
      }
      this.module.link(specifiers, modules);
      this.linked = jobs;
    } finally {
      // Restore it - if it succeeds, we'll reset in the caller; Otherwise it's
      // not cached and if the error is caught, subsequent attempt would still fail.
      loader.loadCache.delete(url, importAttributes.type);
    }
  }

  get modulePromise() {
    return PromiseResolve(this.module);
  }

  async run() {
    const status = this.module.getStatus();
    assert(status === kEvaluated,
           `A require()-d module that is imported again must be evaluated. Status = ${status}`);
    return { __proto__: null, module: this.module };
  }

  runSync() {
    this.module.instantiateSync();
    setHasStartedUserESMExecution();
    const namespace = this.module.evaluateSync();
    return { __proto__: null, module: this.module, namespace };
  }
}

ObjectSetPrototypeOf(ModuleJobBase.prototype, null);
module.exports = {
  ModuleJob, ModuleJobSync, ModuleJobBase,
};
