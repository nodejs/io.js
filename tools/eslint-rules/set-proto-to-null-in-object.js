'use strict';

module.exports = {
  meta: {
    messages: {
      error: 'Add `__proto__: null` to object',
    },
    fixable: 'code',
  },
  create: function(context) {
    return {
      ObjectExpression(node) {
        // Not adding __proto__ to module.exports as it will break a lot of libraries
        if (isModuleExportsObject(node) || isModifiedExports(node)) {
          return;
        }

        const properties = node.properties;
        let hasProto = false;

        for (const property of properties) {

          if (!property.key) {
            continue;
          }

          if (property.key.type === 'Identifier' && property.key.name === '__proto__') {
            hasProto = true;
            break;
          }

          if (property.key.type === 'Literal' && property.key.value === '__proto__') {
            hasProto = true;
            break;
          }
        }

        if (hasProto) {
          return;
        }

        if (properties.length > 0) {
          // If the object has properties but no __proto__ property
          context.report({
            node,
            message: 'Every object must have __proto__: null',
            fix: function(fixer) {
              // Generate the fix suggestion to add __proto__: null
              const sourceCode = context.sourceCode;
              const firstProperty = properties[0];
              const firstPropertyToken = sourceCode.getFirstToken(firstProperty);


              const isMultiLine = properties.length === 1 ?
                // If the object has only one property,
                // it's multiline if the property is not on the same line as the object parenthesis
                properties[0].loc.start.line !== node.loc.start.line :
                // If the object has more than one property,
                // it's multiline if the first and second properties are not on the same line
                properties[0].loc.start.line !== properties[1].loc.start.line;

              const fixText = `__proto__: null,${isMultiLine ? '\n' : ' '}`;

              // Insert the fix suggestion before the first property
              return fixer.insertTextBefore(firstPropertyToken, fixText);
            },
          });
        }

        if (properties.length === 0) {
          // If the object is empty and missing __proto__
          context.report({
            node,
            message: 'Every empty object must have __proto__: null',
            fix: function(fixer) {
              // Generate the fix suggestion to create the object with __proto__: null
              const fixText = '{ __proto__: null }';

              // Replace the empty object with the fix suggestion
              return fixer.replaceText(node, fixText);
            },
          });
        }
      },
    };
  },
};

// Helper function to check if the object is `module.exports`
function isModuleExportsObject(node) {
  return (
    node.parent &&
    node.parent.type === 'AssignmentExpression' &&
    node.parent.left &&
    node.parent.left.type === 'MemberExpression' &&
    node.parent.left.object &&
    node.parent.left.object.name === 'module' &&
    node.parent.left.property &&
    node.parent.left.property.name === 'exports'
  );
}

function isModifiedExports(node) {
  return (
    node.parent &&
    (isObjectAssignCall(node.parent) || isObjectDefinePropertiesCall(node.parent))
  );
}

// Helper function to check if the node represents an ObjectAssign call
function isObjectAssignCall(node) {
  return (
    node.type === 'CallExpression' &&
    node.callee &&
    node.callee.type === 'Identifier' &&
    node.callee.name === 'ObjectAssign' &&
    node.arguments.length > 1 &&
    node.arguments.some((arg) =>
      arg.type === 'MemberExpression' &&
      arg.object.name === 'module' &&
      arg.property.name === 'exports',
    )
  );
}

// Helper function to check if the node represents an ObjectDefineProperties call
function isObjectDefinePropertiesCall(node) {
  return (
    node.type === 'CallExpression' &&
    node.callee &&
    node.callee.type === 'Identifier' &&
    node.callee.name === 'ObjectDefineProperties' &&
    node.arguments.length > 1 &&
    node.arguments.some((arg) =>
      arg.type === 'MemberExpression' &&
      arg.object.name === 'module' &&
      arg.property.name === 'exports',
    )
  );
}
