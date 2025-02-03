"use strict";
export function isSwcError(error) {
  return error.code !== void 0;
}
export function wrapAndReThrowSwcError(error) {
  switch (error.code) {
    case "UnsupportedSyntax": {
      const unsupportedSyntaxError = new Error(error.message);
      unsupportedSyntaxError.name = "UnsupportedSyntaxError";
      throw unsupportedSyntaxError;
    }
    case "InvalidSyntax":
      throw new SyntaxError(error.message);
    default:
      throw new Error(error.message);
  }
}
