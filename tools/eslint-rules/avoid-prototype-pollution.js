'use strict';

const CallExpression = (fnName) => `CallExpression[callee.name=${fnName}]`;
const AnyFunction = 'FunctionDeclaration, FunctionExpression, ArrowFunctionExpression';

function checkProperties(context, node) {
  if (
    node.type === 'CallExpression' &&
    node.callee.name === 'ObjectGetOwnPropertyDescriptors'
  ) {
    context.report({
      node,
      message:
        'Property descriptors inherits from the Object prototype, therefore are subject to prototype pollution',
    });
  }
  if (node.type !== 'ObjectExpression') return;
  for (const { key, value } of node.properties) {
    if (
      key != null && value != null &&
      !(key.type === 'Identifier' && key.name === '__proto__') &&
      !(key.type === 'Literal' && key.value === '__proto__')
    ) {
      checkPropertyDescriptor(context, value);
    }
  }
}

function isNullPrototypeObjectExpression(node) {
  if (node.type !== 'ObjectExpression') return;

  for (const { key, value } of node.properties) {
    if (
      key != null && value != null &&
      ((key.type === 'Identifier' && key.name === '__proto__') ||
        (key.type === 'Literal' && key.value === '__proto__')) &&
      value.type === 'Literal' && value.value === null
    ) {
      return true;
    }
  }
  return false;
}

function checkPropertyDescriptor(context, node) {
  if (
    node.type === 'CallExpression' &&
    (node.callee.name === 'ObjectGetOwnPropertyDescriptor' ||
     node.callee.name === 'ReflectGetOwnPropertyDescriptor')
  ) {
    context.report({
      node,
      message:
        'Property descriptors inherits from the Object prototype, therefore are subject to prototype pollution',
      suggest: [{
        desc: 'Wrap the property descriptor in a null-prototype object',
        fix(fixer) {
          return [
            fixer.insertTextBefore(node, '{ __proto__: null,...'),
            fixer.insertTextAfter(node, ' }'),
          ];
        },
      }],
    });
  }
  if (isNullPrototypeObjectExpression(node) === false) {
    context.report({
      node,
      message: 'Must use null-prototype object for property descriptors',
    });
  }
}

function createUnsafeStringMethodReport(context, name, lookedUpProperty) {
  const lastDotPosition = '$String.prototype.'.length;
  const unsafePrimordialName = `StringPrototype${name.charAt(lastDotPosition).toUpperCase()}${name.slice(lastDotPosition + 1, -1)}`;
  return {
    [CallExpression(unsafePrimordialName)](node) {
      context.report({
        node,
        message: `${name} looks up the ${lookedUpProperty} property on the first argument`,
      });
    },
  };
}

function createUnsafeStringMethodOnRegexReport(context, name, lookedUpProperty) {
  const dotPosition = 'Symbol.'.length;
  const safePrimordialName = `RegExpPrototypeSymbol${lookedUpProperty.charAt(dotPosition).toUpperCase()}${lookedUpProperty.slice(dotPosition + 1)}`;
  const lastDotPosition = '$String.prototype.'.length;
  const unsafePrimordialName = `StringPrototype${name.charAt(lastDotPosition).toUpperCase()}${name.slice(lastDotPosition + 1, -1)}`;
  return {
    [[
      `${CallExpression(unsafePrimordialName)}[arguments.1.type=Literal][arguments.1.regex]`,
      `${CallExpression(unsafePrimordialName)}[arguments.1.type=NewExpression][arguments.1.callee.name=RegExp]`,
    ].join(',')](node) {
      context.report({
        node,
        message: `${name} looks up the ${lookedUpProperty} property of the passed regex, use ${safePrimordialName} directly`,
      });
    },
  };
}

module.exports = {
  meta: { hasSuggestions: true },
  create(context) {
    return {
      [CallExpression(/^(Object|Reflect)DefinePropert(ies|y)$/)](node) {
        switch (node.callee.name) {
          case 'ObjectDefineProperties':
            checkProperties(context, node.arguments[1]);
            break;
          case 'ReflectDefineProperty':
          case 'ObjectDefineProperty':
            checkPropertyDescriptor(context, node.arguments[2]);
            break;
          default:
            throw new Error('Unreachable');
        }
      },

      [`${CallExpression('ObjectCreate')}[arguments.length=2]`](node) {
        checkProperties(context, node.arguments[1]);
      },

      [`:matches(${AnyFunction})[async=true]>BlockStatement ReturnStatement>ObjectExpression, :matches(${AnyFunction})[async=true]>ObjectExpression`](node) {
        if (node.parent.type === 'ReturnStatement') {
          let { parent } = node;
          do {
            ({ parent } = parent);
          } while (!parent.type.includes('Function'));

          if (!parent.async) return;
        }
        if (isNullPrototypeObjectExpression(node) === false) {
          context.report({
            node,
            message: 'Use null-prototype when returning from async function',
          });
        }
      },

      [CallExpression('RegExpPrototypeTest')](node) {
        context.report({
          node,
          message: '%RegExp.prototype.test% looks up the "exec" property of `this` value',
          suggest: [{
            desc: 'Use RegexpPrototypeExec instead',
            fix(fixer) {
              const testRange = [ ...node.range ];
              testRange[0] = testRange[0] + 'RegexpPrototype'.length;
              testRange[1] = testRange[0] + 'Test'.length;
              return [
                fixer.replaceTextRange(testRange, 'Exec'),
                fixer.insertTextAfter(node, ' !== null'),
              ];
            },
          }],
        });
      },
      [CallExpression(/^RegExpPrototypeSymbol(Match|MatchAll)$/)](node) {
        context.report({
          node,
          message: node.callee.name + ' looks up the "exec" property of `this` value',
        });
      },
      [CallExpression(/^(RegExpPrototypeSymbol|StringPrototype)Search$/)](node) {
        context.report({
          node,
          message: node.callee.name + ' is unsafe, use SafeStringPrototypeSearch instead',
        });
      },
      ...createUnsafeStringMethodReport(context, '%String.prototype.match%', 'Symbol.match'),
      ...createUnsafeStringMethodReport(context, '%String.prototype.matchAll%', 'Symbol.matchAll'),
      ...createUnsafeStringMethodOnRegexReport(context, '%String.prototype.replace%', 'Symbol.replace'),
      ...createUnsafeStringMethodOnRegexReport(context, '%String.prototype.replaceAll%', 'Symbol.replace'),
      ...createUnsafeStringMethodOnRegexReport(context, '%String.prototype.split%', 'Symbol.split'),

      'NewExpression[callee.name="Proxy"][arguments.1.type="ObjectExpression"]'(node) {
        for (const { key, value } of node.arguments[1].properties) {
          if (
            key != null && value != null &&
            ((key.type === 'Identifier' && key.name === '__proto__') ||
              (key.type === 'Literal' && key.value === '__proto__')) &&
            value.type === 'Literal' && value.value === null
          ) {
            return;
          }
        }
        context.report({
          node,
          message: 'Proxy handler must be a null-prototype object',
        });
      },

      [`ExpressionStatement>AwaitExpression>${CallExpression(/^(Safe)?PromiseAll(Settled)?$/)}`](node) {
        context.report({
          node,
          message: `Use ${node.callee.name}ReturnVoid`,
        });
      },

      [CallExpression('PromisePrototypeCatch')](node) {
        context.report({
          node,
          message: '%Promise.prototype.catch% looks up the `then` property of ' +
                   'the `this` argument, use PromisePrototypeThen instead',
        });
      },

      [CallExpression('PromisePrototypeFinally')](node) {
        context.report({
          node,
          message: '%Promise.prototype.finally% looks up the `then` property of ' +
                   'the `this` argument, use SafePromisePrototypeFinally or ' +
                   'try/finally instead',
        });
      },

      [CallExpression(/^Promise(All(Settled)?|Any|Race)/)](node) {
        context.report({
          node,
          message: `Use Safe${node.callee.name} instead of ${node.callee.name}`,
        });
      },

      [CallExpression('ArrayPrototypeConcat')](node) {
        context.report({
          node,
          message: '%Array.prototype.concat% looks up `@@isConcatSpreadable` ' +
                   'which can be subject to prototype pollution',
        });
      },
    };
  },
};
