/**
 * @fileoverview Rule to enforce a particular function style
 * @author Nicholas C. Zakas
 */
"use strict";

//------------------------------------------------------------------------------
// Rule Definition
//------------------------------------------------------------------------------

/** @type {import('../shared/types').Rule} */
module.exports = {
    meta: {
        type: "suggestion",

        docs: {
            description: "Enforce the consistent use of either `function` declarations or expressions assigned to variables",
            recommended: false,
            url: "https://eslint.org/docs/latest/rules/func-style"
        },

        schema: [
            {
                enum: ["declaration", "expression"]
            },
            {
                type: "object",
                properties: {
                    allowArrowFunctions: {
                        type: "boolean",
                        default: false
                    },
                    overrides: {
                        type: "object",
                        properties: {
                            namedExports: {
                                enum: ["declaration", "expression", "ignore"]
                            }
                        },
                        additionalProperties: false
                    }
                },
                additionalProperties: false
            }
        ],

        messages: {
            expression: "Expected a function expression.",
            declaration: "Expected a function declaration."
        }
    },

    create(context) {

        const style = context.options[0],
            allowArrowFunctions = context.options[1] && context.options[1].allowArrowFunctions,
            enforceDeclarations = (style === "declaration"),
            exportFunctionStyle = context.options[1] && context.options[1].overrides && context.options[1].overrides.namedExports,
            stack = [];

        const nodesToCheck = {
            FunctionDeclaration(node) {
                stack.push(false);

                if (
                    !enforceDeclarations &&
                    node.parent.type !== "ExportDefaultDeclaration" &&
                    (typeof exportFunctionStyle === "undefined" || node.parent.type !== "ExportNamedDeclaration")
                ) {
                    context.report({ node, messageId: "expression" });
                }

                if (node.parent.type === "ExportNamedDeclaration" && exportFunctionStyle === "expression") {
                    context.report({ node, messageId: "expression" });
                }
            },
            "FunctionDeclaration:exit"() {
                stack.pop();
            },

            FunctionExpression(node) {
                stack.push(false);

                if (
                    enforceDeclarations &&
                    node.parent.type === "VariableDeclarator" &&
                    (typeof exportFunctionStyle === "undefined" || node.parent.parent.parent.type !== "ExportNamedDeclaration")
                ) {
                    context.report({ node: node.parent, messageId: "declaration" });
                }

                if (
                    node.parent.type === "VariableDeclarator" && node.parent.parent.parent.type === "ExportNamedDeclaration" &&
                    exportFunctionStyle === "declaration"
                ) {
                    context.report({ node: node.parent, messageId: "declaration" });
                }
            },
            "FunctionExpression:exit"() {
                stack.pop();
            },

            "ThisExpression, Super"() {
                if (stack.length > 0) {
                    stack[stack.length - 1] = true;
                }
            }
        };

        if (!allowArrowFunctions) {
            nodesToCheck.ArrowFunctionExpression = function() {
                stack.push(false);
            };

            nodesToCheck["ArrowFunctionExpression:exit"] = function(node) {
                const hasThisOrSuperExpr = stack.pop();

                if (!hasThisOrSuperExpr && node.parent.type === "VariableDeclarator") {
                    if (
                        enforceDeclarations &&
                        (typeof exportFunctionStyle === "undefined" || node.parent.parent.parent.type !== "ExportNamedDeclaration")
                    ) {
                        context.report({ node: node.parent, messageId: "declaration" });
                    }

                    if (node.parent.parent.parent.type === "ExportNamedDeclaration" && exportFunctionStyle === "declaration") {
                        context.report({ node: node.parent, messageId: "declaration" });
                    }
                }
            };
        }

        return nodesToCheck;

    }
};
