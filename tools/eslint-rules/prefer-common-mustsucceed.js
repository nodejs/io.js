'use strict';

const mustCall = 'CallExpression[callee.object.name="common"]' +
                 '[callee.property.name="mustCall"]';

module.exports = {
  create(context) {
    function isAssertIfError(node) {
      return node.type === 'MemberExpression' &&
            node.object.type === 'Identifier' &&
            node.object.name === 'assert' &&
            node.property.type === 'Identifier' &&
            node.property.name === 'ifError';
    }

    function isCallToIfError(node, errName) {
      return node.type === 'CallExpression' &&
            isAssertIfError(node.callee) &&
            node.arguments.length > 0 &&
            node.arguments[0].type === 'Identifier' &&
            node.arguments[0].name === errName;
    }

    function bodyStartsWithCallToIfError(body, errName) {
      while (body.type === 'BlockStatement' && body.body.length > 0)
        body = body.body[0];

      let expr;
      switch (body.type) {
        case 'ReturnStatement':
          expr = body.argument;
          break;
        case 'ExpressionStatement':
          expr = body.expression;
          break;
        default:
          expr = body;
      }

      return isCallToIfError(expr, errName);
    }

    return {
      [`${mustCall}:exit`]: (mustCall) => {
        if (mustCall.arguments.length > 0) {
          const callback = mustCall.arguments[0];
          if (isAssertIfError(callback)) {
            context.report(mustCall, 'Please use common.mustSucceed instead of ' +
                                    'common.mustCall(assert.ifError).');
          }

          if (callback.type === 'ArrowFunctionExpression' ||
              callback.type === 'FunctionExpression') {
            if (callback.params.length > 0 &&
                callback.params[0].type === 'Identifier') {
              const errName = callback.params[0].name;
              if (bodyStartsWithCallToIfError(callback.body, errName)) {
                context.report(mustCall, 'Please use common.mustSucceed instead' +
                                        ' of common.mustCall with' +
                                        ' assert.ifError.');
              }
            }
          }
        }
      },
    };
  },
};
