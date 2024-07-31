/**
 * @fileoverview Rule to disallow use of the new operator with the `Symbol` object
 * @author Alberto Rodríguez
 * @deprecated in ESLint v9.0.0
 */

"use strict";

//------------------------------------------------------------------------------
// Rule Definition
//------------------------------------------------------------------------------

/** @type {import('../shared/types').Rule} */
module.exports = {
    meta: {
        type: "problem",

        docs: {
            description: "Disallow `new` operators with the `Symbol` object",
            recommended: false,
            url: "https://eslint.org/docs/latest/rules/no-new-symbol"
        },

        deprecated: true,

        replacedBy: [
            "no-new-native-nonconstructor"
        ],

        schema: [],

        messages: {
            noNewSymbol: "`Symbol` cannot be called as a constructor."
        }
    },

    create(context) {

        const sourceCode = context.sourceCode;

        return {
            "Program:exit"(node) {
                const globalScope = sourceCode.getScope(node);
                const variable = globalScope.set.get("Symbol");

                if (variable && variable.defs.length === 0) {
                    variable.references.forEach(ref => {
                        const idNode = ref.identifier;
                        const parent = idNode.parent;

                        if (parent && parent.type === "NewExpression" && parent.callee === idNode) {
                            context.report({
                                node: idNode,
                                messageId: "noNewSymbol"
                            });
                        }
                    });
                }
            }
        };

    }
};
