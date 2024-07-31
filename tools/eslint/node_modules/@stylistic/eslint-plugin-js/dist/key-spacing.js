'use strict';

var utils = require('./utils.js');

function containsLineTerminator(str) {
  return utils.LINEBREAK_MATCHER.test(str);
}
function last(arr) {
  return arr[arr.length - 1];
}
function isSingleLine(node) {
  return node.loc.end.line === node.loc.start.line;
}
function isSingleLineProperties(properties) {
  const [firstProp] = properties;
  const lastProp = last(properties);
  return firstProp.loc.start.line === lastProp.loc.end.line;
}
function initOptionProperty(toOptions, fromOptions) {
  toOptions.mode = fromOptions.mode || "strict";
  if (typeof fromOptions.beforeColon !== "undefined")
    toOptions.beforeColon = +fromOptions.beforeColon;
  else
    toOptions.beforeColon = 0;
  if (typeof fromOptions.afterColon !== "undefined")
    toOptions.afterColon = +fromOptions.afterColon;
  else
    toOptions.afterColon = 1;
  if (typeof fromOptions.align !== "undefined") {
    if (typeof fromOptions.align === "object") {
      toOptions.align = fromOptions.align;
    } else {
      toOptions.align = {
        on: fromOptions.align,
        mode: toOptions.mode,
        beforeColon: toOptions.beforeColon,
        afterColon: toOptions.afterColon
      };
    }
  }
  return toOptions;
}
function initOptions(toOptions, fromOptions) {
  if (typeof fromOptions.align === "object") {
    toOptions.align = initOptionProperty({}, fromOptions.align);
    toOptions.align.on = fromOptions.align.on || "colon";
    toOptions.align.mode = fromOptions.align.mode || "strict";
    toOptions.multiLine = initOptionProperty({}, fromOptions.multiLine || fromOptions);
    toOptions.singleLine = initOptionProperty({}, fromOptions.singleLine || fromOptions);
  } else {
    toOptions.multiLine = initOptionProperty({}, fromOptions.multiLine || fromOptions);
    toOptions.singleLine = initOptionProperty({}, fromOptions.singleLine || fromOptions);
    if (toOptions.multiLine.align) {
      toOptions.align = {
        on: toOptions.multiLine.align.on,
        mode: toOptions.multiLine.align.mode || toOptions.multiLine.mode,
        beforeColon: toOptions.multiLine.align.beforeColon,
        afterColon: toOptions.multiLine.align.afterColon
      };
    }
  }
  return toOptions;
}
var keySpacing = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Enforce consistent spacing between keys and values in object literal properties",
      url: "https://eslint.style/rules/js/key-spacing"
    },
    fixable: "whitespace",
    schema: [{
      anyOf: [
        {
          type: "object",
          properties: {
            align: {
              anyOf: [
                {
                  type: "string",
                  enum: ["colon", "value"]
                },
                {
                  type: "object",
                  properties: {
                    mode: {
                      type: "string",
                      enum: ["strict", "minimum"]
                    },
                    on: {
                      type: "string",
                      enum: ["colon", "value"]
                    },
                    beforeColon: {
                      type: "boolean"
                    },
                    afterColon: {
                      type: "boolean"
                    }
                  },
                  additionalProperties: false
                }
              ]
            },
            mode: {
              type: "string",
              enum: ["strict", "minimum"]
            },
            beforeColon: {
              type: "boolean"
            },
            afterColon: {
              type: "boolean"
            }
          },
          additionalProperties: false
        },
        {
          type: "object",
          properties: {
            singleLine: {
              type: "object",
              properties: {
                mode: {
                  type: "string",
                  enum: ["strict", "minimum"]
                },
                beforeColon: {
                  type: "boolean"
                },
                afterColon: {
                  type: "boolean"
                }
              },
              additionalProperties: false
            },
            multiLine: {
              type: "object",
              properties: {
                align: {
                  anyOf: [
                    {
                      type: "string",
                      enum: ["colon", "value"]
                    },
                    {
                      type: "object",
                      properties: {
                        mode: {
                          type: "string",
                          enum: ["strict", "minimum"]
                        },
                        on: {
                          type: "string",
                          enum: ["colon", "value"]
                        },
                        beforeColon: {
                          type: "boolean"
                        },
                        afterColon: {
                          type: "boolean"
                        }
                      },
                      additionalProperties: false
                    }
                  ]
                },
                mode: {
                  type: "string",
                  enum: ["strict", "minimum"]
                },
                beforeColon: {
                  type: "boolean"
                },
                afterColon: {
                  type: "boolean"
                }
              },
              additionalProperties: false
            }
          },
          additionalProperties: false
        },
        {
          type: "object",
          properties: {
            singleLine: {
              type: "object",
              properties: {
                mode: {
                  type: "string",
                  enum: ["strict", "minimum"]
                },
                beforeColon: {
                  type: "boolean"
                },
                afterColon: {
                  type: "boolean"
                }
              },
              additionalProperties: false
            },
            multiLine: {
              type: "object",
              properties: {
                mode: {
                  type: "string",
                  enum: ["strict", "minimum"]
                },
                beforeColon: {
                  type: "boolean"
                },
                afterColon: {
                  type: "boolean"
                }
              },
              additionalProperties: false
            },
            align: {
              type: "object",
              properties: {
                mode: {
                  type: "string",
                  enum: ["strict", "minimum"]
                },
                on: {
                  type: "string",
                  enum: ["colon", "value"]
                },
                beforeColon: {
                  type: "boolean"
                },
                afterColon: {
                  type: "boolean"
                }
              },
              additionalProperties: false
            }
          },
          additionalProperties: false
        }
      ]
    }],
    messages: {
      extraKey: "Extra space after {{computed}}key '{{key}}'.",
      extraValue: "Extra space before value for {{computed}}key '{{key}}'.",
      missingKey: "Missing space after {{computed}}key '{{key}}'.",
      missingValue: "Missing space before value for {{computed}}key '{{key}}'."
    }
  },
  create(context) {
    const options = context.options[0] || {};
    const ruleOptions = initOptions({}, options);
    const multiLineOptions = ruleOptions.multiLine;
    const singleLineOptions = ruleOptions.singleLine;
    const alignmentOptions = ruleOptions.align || null;
    const sourceCode = context.sourceCode;
    function isKeyValueProperty(property) {
      return !("method" in property && property.method || "shorthand" in property && property.shorthand || "kind" in property && property.kind !== "init" || property.type !== "Property");
    }
    function getNextColon(node) {
      return sourceCode.getTokenAfter(node, utils.isColonToken);
    }
    function getLastTokenBeforeColon(node) {
      const colonToken = getNextColon(node);
      return sourceCode.getTokenBefore(colonToken);
    }
    function getFirstTokenAfterColon(node) {
      const colonToken = getNextColon(node);
      return sourceCode.getTokenAfter(colonToken);
    }
    function continuesPropertyGroup(lastMember, candidate) {
      const groupEndLine = lastMember.loc.start.line;
      const candidateValueStartLine = (isKeyValueProperty(candidate) ? getFirstTokenAfterColon(candidate.key) : candidate).loc.start.line;
      if (candidateValueStartLine - groupEndLine <= 1)
        return true;
      const leadingComments = sourceCode.getCommentsBefore(candidate);
      if (leadingComments.length && leadingComments[0].loc.start.line - groupEndLine <= 1 && candidateValueStartLine - last(leadingComments).loc.end.line <= 1) {
        for (let i = 1; i < leadingComments.length; i++) {
          if (leadingComments[i].loc.start.line - leadingComments[i - 1].loc.end.line > 1)
            return false;
        }
        return true;
      }
      return false;
    }
    function getKey(property) {
      const key = property.key;
      if (property.computed)
        return sourceCode.getText().slice(key.range[0], key.range[1]);
      return utils.getStaticPropertyName(property);
    }
    function report(property, side, whitespace, expected, mode) {
      const diff = whitespace.length - expected;
      if ((diff && mode === "strict" || diff < 0 && mode === "minimum" || diff > 0 && !expected && mode === "minimum") && !(expected && containsLineTerminator(whitespace))) {
        const nextColon = getNextColon(property.key);
        const tokenBeforeColon = sourceCode.getTokenBefore(nextColon, { includeComments: true });
        const tokenAfterColon = sourceCode.getTokenAfter(nextColon, { includeComments: true });
        const isKeySide = side === "key";
        const isExtra = diff > 0;
        const diffAbs = Math.abs(diff);
        const spaces = Array(diffAbs + 1).join(" ");
        const locStart = isKeySide ? tokenBeforeColon.loc.end : nextColon.loc.start;
        const locEnd = isKeySide ? nextColon.loc.start : tokenAfterColon.loc.start;
        const missingLoc = isKeySide ? tokenBeforeColon.loc : tokenAfterColon.loc;
        const loc = isExtra ? { start: locStart, end: locEnd } : missingLoc;
        let fix;
        if (isExtra) {
          let range;
          if (isKeySide)
            range = [tokenBeforeColon.range[1], tokenBeforeColon.range[1] + diffAbs];
          else
            range = [tokenAfterColon.range[0] - diffAbs, tokenAfterColon.range[0]];
          fix = function(fixer) {
            return fixer.removeRange(range);
          };
        } else {
          if (isKeySide) {
            fix = function(fixer) {
              return fixer.insertTextAfter(tokenBeforeColon, spaces);
            };
          } else {
            fix = function(fixer) {
              return fixer.insertTextBefore(tokenAfterColon, spaces);
            };
          }
        }
        let messageId;
        if (isExtra)
          messageId = side === "key" ? "extraKey" : "extraValue";
        else
          messageId = side === "key" ? "missingKey" : "missingValue";
        context.report({
          node: property[side],
          loc,
          messageId,
          data: {
            computed: property.computed ? "computed " : "",
            key: getKey(property)
          },
          fix
        });
      }
    }
    function getKeyWidth(property) {
      const startToken = sourceCode.getFirstToken(property);
      const endToken = getLastTokenBeforeColon(property.key);
      return utils.getGraphemeCount(sourceCode.getText().slice(startToken.range[0], endToken.range[1]));
    }
    function getPropertyWhitespace(property) {
      const whitespace = /(\s*):(\s*)/u.exec(sourceCode.getText().slice(
        property.key.range[1],
        property.value.range[0]
      ));
      if (whitespace) {
        return {
          beforeColon: whitespace[1],
          afterColon: whitespace[2]
        };
      }
      return null;
    }
    function createGroups(node) {
      if (node.properties.length === 1)
        return [node.properties];
      return node.properties.reduce((groups, property) => {
        const currentGroup = last(groups);
        const prev = last(currentGroup);
        if (!prev || continuesPropertyGroup(prev, property))
          currentGroup.push(property);
        else
          groups.push([property]);
        return groups;
      }, [
        []
      ]);
    }
    function verifyGroupAlignment(properties) {
      const length = properties.length;
      const widths = properties.map(getKeyWidth);
      const align = alignmentOptions.on;
      let targetWidth = Math.max(...widths);
      let beforeColon;
      let afterColon;
      let mode;
      if (alignmentOptions && length > 1) {
        beforeColon = alignmentOptions.beforeColon;
        afterColon = alignmentOptions.afterColon;
        mode = alignmentOptions.mode;
      } else {
        beforeColon = multiLineOptions.beforeColon;
        afterColon = multiLineOptions.afterColon;
        mode = alignmentOptions.mode;
      }
      targetWidth += align === "colon" ? beforeColon : afterColon;
      for (let i = 0; i < length; i++) {
        const property = properties[i];
        const whitespace = getPropertyWhitespace(property);
        if (whitespace) {
          const width = widths[i];
          if (align === "value") {
            report(property, "key", whitespace.beforeColon, beforeColon, mode);
            report(property, "value", whitespace.afterColon, targetWidth - width, mode);
          } else {
            report(property, "key", whitespace.beforeColon, targetWidth - width, mode);
            report(property, "value", whitespace.afterColon, afterColon, mode);
          }
        }
      }
    }
    function verifySpacing(node, lineOptions) {
      const actual = getPropertyWhitespace(node);
      if (actual) {
        report(node, "key", actual.beforeColon, lineOptions.beforeColon, lineOptions.mode);
        report(node, "value", actual.afterColon, lineOptions.afterColon, lineOptions.mode);
      }
    }
    function verifyListSpacing(properties, lineOptions) {
      const length = properties.length;
      for (let i = 0; i < length; i++)
        verifySpacing(properties[i], lineOptions);
    }
    function verifyAlignment(node) {
      createGroups(node).forEach((group) => {
        const properties = group.filter(isKeyValueProperty);
        if (properties.length > 0 && isSingleLineProperties(properties))
          verifyListSpacing(properties, multiLineOptions);
        else
          verifyGroupAlignment(properties);
      });
    }
    if (alignmentOptions) {
      return {
        ObjectExpression(node) {
          if (isSingleLine(node))
            verifyListSpacing(node.properties.filter(isKeyValueProperty), singleLineOptions);
          else
            verifyAlignment(node);
        }
      };
    }
    return {
      Property(node) {
        verifySpacing(node, isSingleLine(node.parent) ? singleLineOptions : multiLineOptions);
      }
    };
  }
});

exports.keySpacing = keySpacing;
