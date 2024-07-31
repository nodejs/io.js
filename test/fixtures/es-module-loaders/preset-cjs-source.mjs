export function resolve(specifier, context, next) {
  if (specifier.endsWith('/no-such-file.cjs')) {
    // Shortcut to avoid ERR_MODULE_NOT_FOUND for non-existing file, but keep the url for the load hook.
    return {
      shortCircuit: true,
      url: specifier,
    };
  }
  return next(specifier);
}

export function load(href, context, next) {
  if (href.endsWith('.cjs')) {
    return {
      format: 'commonjs',
      shortCircuit: true,
      source: 'module.exports = "no .cjs file was read to get this source";',
    };
  }
  return next(href);
}
