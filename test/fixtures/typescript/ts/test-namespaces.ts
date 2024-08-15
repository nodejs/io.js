/// <reference path="a.ts" />
namespace Validation {
  const lettersRegexp = /^[A-Za-z]+$/;
    export class LettersOnlyValidator {
      isAcceptable(s: string) {
        return lettersRegexp.test(s);
      }
    }
}
