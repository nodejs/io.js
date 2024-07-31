// NOTE: This file must be compatible with old Node.js versions, since it runs
// during testing.

/**
 * @typedef {Object} HelperMetadata
 * @property {string[]} globals
 * @property {{ [name: string]: string[] }} locals
 * @property {{ [name: string]: string[] }} dependencies
 * @property {string[]} exportBindingAssignments
 * @property {string} exportName
 */

/**
 * Given a file AST for a given helper, get a bunch of metadata about it so that Babel can quickly render
 * the helper is whatever context it is needed in.
 *
 * @param {typeof import("@babel/core")} babel
 *
 * @returns {HelperMetadata}
 */
export function getHelperMetadata(babel, code, helperName) {
  const globals = new Set();
  // Maps imported identifier name -> helper name
  const dependenciesBindings = new Map();

  let exportName;
  const exportBindingAssignments = [];
  // helper name -> reference paths
  const dependencies = new Map();
  // local variable name -> reference paths
  const locals = new Map();

  const spansToRemove = [];

  const validateDefaultExport = decl => {
    if (exportName) {
      throw new Error(
        `Helpers can have only one default export (in ${helperName})`
      );
    }

    if (!decl.isFunctionDeclaration() || !decl.node.id) {
      throw new Error(
        `Helpers can only export named function declarations (in ${helperName})`
      );
    }
  };

  /** @type {import("@babel/traverse").Visitor} */
  const dependencyVisitor = {
    Program(path) {
      for (const child of path.get("body")) {
        if (child.isImportDeclaration()) {
          if (
            child.get("specifiers").length !== 1 ||
            !child.get("specifiers.0").isImportDefaultSpecifier()
          ) {
            throw new Error(
              `Helpers can only import a default value (in ${helperName})`
            );
          }
          dependenciesBindings.set(
            child.node.specifiers[0].local.name,
            child.node.source.value
          );
          dependencies.set(child.node.source.value, []);
          spansToRemove.push([child.node.start, child.node.end]);
          child.remove();
        }
      }
      for (const child of path.get("body")) {
        if (child.isExportDefaultDeclaration()) {
          const decl = child.get("declaration");
          validateDefaultExport(decl);

          exportName = decl.node.id.name;
          spansToRemove.push([child.node.start, decl.node.start]);
          child.replaceWith(decl.node);
        } else if (
          child.isExportNamedDeclaration() &&
          child.node.specifiers.length === 1 &&
          child.get("specifiers.0.exported").isIdentifier({ name: "default" })
        ) {
          const { name } = child.node.specifiers[0].local;

          validateDefaultExport(child.scope.getBinding(name).path);

          exportName = name;
          spansToRemove.push([child.node.start, child.node.end]);
          child.remove();
        } else if (
          process.env.IS_BABEL_OLD_E2E &&
          child.isExportNamedDeclaration() &&
          child.node.specifiers.length === 0
        ) {
          spansToRemove.push([child.node.start, child.node.end]);
          child.remove();
        } else if (
          child.isExportAllDeclaration() ||
          child.isExportNamedDeclaration()
        ) {
          throw new Error(`Helpers can only export default (in ${helperName})`);
        }
      }

      path.scope.crawl();

      const bindings = path.scope.getAllBindings();
      Object.keys(bindings).forEach(name => {
        if (dependencies.has(name)) return;

        const binding = bindings[name];

        const references = [
          ...binding.path.getBindingIdentifierPaths(true)[name].map(makePath),
          ...binding.referencePaths.map(makePath),
        ];
        for (const violation of binding.constantViolations) {
          violation.getBindingIdentifierPaths(true)[name].forEach(path => {
            references.push(makePath(path));
          });
        }

        locals.set(name, references);
      });
    },
    ReferencedIdentifier(child) {
      const name = child.node.name;
      const binding = child.scope.getBinding(name);
      if (!binding) {
        if (dependenciesBindings.has(name)) {
          dependencies
            .get(dependenciesBindings.get(name))
            .push(makePath(child));
        } else if (name !== "arguments" || child.scope.path.isProgram()) {
          globals.add(name);
        }
      }
    },
    AssignmentExpression(child) {
      const left = child.get("left");

      if (!(exportName in left.getBindingIdentifiers())) return;

      if (!left.isIdentifier()) {
        throw new Error(
          `Only simple assignments to exports are allowed in helpers (in ${helperName})`
        );
      }

      const binding = child.scope.getBinding(exportName);

      if (binding && binding.scope.path.isProgram()) {
        exportBindingAssignments.push(makePath(child));
      }
    },
  };

  babel.transformSync(code, {
    configFile: false,
    babelrc: false,
    plugins: [() => ({ visitor: dependencyVisitor })],
  });

  if (!exportName) throw new Error("Helpers must have a named default export.");

  // Process these in reverse so that mutating the references does not invalidate any later paths in
  // the list.
  exportBindingAssignments.reverse();

  spansToRemove.sort(([start1], [start2]) => start2 - start1);
  for (const [start, end] of spansToRemove) {
    code = code.slice(0, start) + code.slice(end);
  }

  return [
    code,
    {
      globals: Array.from(globals),
      locals: Object.fromEntries(locals),
      dependencies: Object.fromEntries(dependencies),
      exportBindingAssignments,
      exportName,
    },
  ];
}

function makePath(path) {
  const parts = [];

  for (; path.parentPath; path = path.parentPath) {
    parts.push(path.key);
    if (path.inList) parts.push(path.listKey);
  }

  return parts.reverse().join(".");
}

export function stringifyMetadata(metadata) {
  return `\
    {
      globals: ${JSON.stringify(metadata.globals)},
      locals: ${JSON.stringify(metadata.locals)},
      exportBindingAssignments: ${JSON.stringify(metadata.exportBindingAssignments)},
      exportName: ${JSON.stringify(metadata.exportName)},
      dependencies: ${JSON.stringify(metadata.dependencies)},
    }
  `;
}
