/*istanbul ignore start*/
"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports["default"] = _default;
/*istanbul ignore end*/
// Iterator that traverses in the range of [min, max], stepping
// by distance from a given start position. I.e. for [0, 4], with
// start of 2, this will iterate 2, 3, 1, 4, 0.
function
/*istanbul ignore start*/
_default
/*istanbul ignore end*/
(start, minLine, maxLine) {
  var wantForward = true,
    backwardExhausted = false,
    forwardExhausted = false,
    localOffset = 1;
  return function iterator() {
    if (wantForward && !forwardExhausted) {
      if (backwardExhausted) {
        localOffset++;
      } else {
        wantForward = false;
      }

      // Check if trying to fit beyond text length, and if not, check it fits
      // after offset location (or desired location on first iteration)
      if (start + localOffset <= maxLine) {
        return start + localOffset;
      }
      forwardExhausted = true;
    }
    if (!backwardExhausted) {
      if (!forwardExhausted) {
        wantForward = true;
      }

      // Check if trying to fit before text beginning, and if not, check it fits
      // before offset location
      if (minLine <= start - localOffset) {
        return start - localOffset++;
      }
      backwardExhausted = true;
      return iterator();
    }

    // We tried to fit hunk before text beginning and beyond text length, then
    // hunk can't fit on the text. Return undefined
  };
}
//# sourceMappingURL=data:application/json;charset=utf-8;base64,eyJ2ZXJzaW9uIjozLCJuYW1lcyI6WyJfZGVmYXVsdCIsInN0YXJ0IiwibWluTGluZSIsIm1heExpbmUiLCJ3YW50Rm9yd2FyZCIsImJhY2t3YXJkRXhoYXVzdGVkIiwiZm9yd2FyZEV4aGF1c3RlZCIsImxvY2FsT2Zmc2V0IiwiaXRlcmF0b3IiXSwic291cmNlcyI6WyIuLi8uLi9zcmMvdXRpbC9kaXN0YW5jZS1pdGVyYXRvci5qcyJdLCJzb3VyY2VzQ29udGVudCI6WyIvLyBJdGVyYXRvciB0aGF0IHRyYXZlcnNlcyBpbiB0aGUgcmFuZ2Ugb2YgW21pbiwgbWF4XSwgc3RlcHBpbmdcbi8vIGJ5IGRpc3RhbmNlIGZyb20gYSBnaXZlbiBzdGFydCBwb3NpdGlvbi4gSS5lLiBmb3IgWzAsIDRdLCB3aXRoXG4vLyBzdGFydCBvZiAyLCB0aGlzIHdpbGwgaXRlcmF0ZSAyLCAzLCAxLCA0LCAwLlxuZXhwb3J0IGRlZmF1bHQgZnVuY3Rpb24oc3RhcnQsIG1pbkxpbmUsIG1heExpbmUpIHtcbiAgbGV0IHdhbnRGb3J3YXJkID0gdHJ1ZSxcbiAgICAgIGJhY2t3YXJkRXhoYXVzdGVkID0gZmFsc2UsXG4gICAgICBmb3J3YXJkRXhoYXVzdGVkID0gZmFsc2UsXG4gICAgICBsb2NhbE9mZnNldCA9IDE7XG5cbiAgcmV0dXJuIGZ1bmN0aW9uIGl0ZXJhdG9yKCkge1xuICAgIGlmICh3YW50Rm9yd2FyZCAmJiAhZm9yd2FyZEV4aGF1c3RlZCkge1xuICAgICAgaWYgKGJhY2t3YXJkRXhoYXVzdGVkKSB7XG4gICAgICAgIGxvY2FsT2Zmc2V0Kys7XG4gICAgICB9IGVsc2Uge1xuICAgICAgICB3YW50Rm9yd2FyZCA9IGZhbHNlO1xuICAgICAgfVxuXG4gICAgICAvLyBDaGVjayBpZiB0cnlpbmcgdG8gZml0IGJleW9uZCB0ZXh0IGxlbmd0aCwgYW5kIGlmIG5vdCwgY2hlY2sgaXQgZml0c1xuICAgICAgLy8gYWZ0ZXIgb2Zmc2V0IGxvY2F0aW9uIChvciBkZXNpcmVkIGxvY2F0aW9uIG9uIGZpcnN0IGl0ZXJhdGlvbilcbiAgICAgIGlmIChzdGFydCArIGxvY2FsT2Zmc2V0IDw9IG1heExpbmUpIHtcbiAgICAgICAgcmV0dXJuIHN0YXJ0ICsgbG9jYWxPZmZzZXQ7XG4gICAgICB9XG5cbiAgICAgIGZvcndhcmRFeGhhdXN0ZWQgPSB0cnVlO1xuICAgIH1cblxuICAgIGlmICghYmFja3dhcmRFeGhhdXN0ZWQpIHtcbiAgICAgIGlmICghZm9yd2FyZEV4aGF1c3RlZCkge1xuICAgICAgICB3YW50Rm9yd2FyZCA9IHRydWU7XG4gICAgICB9XG5cbiAgICAgIC8vIENoZWNrIGlmIHRyeWluZyB0byBmaXQgYmVmb3JlIHRleHQgYmVnaW5uaW5nLCBhbmQgaWYgbm90LCBjaGVjayBpdCBmaXRzXG4gICAgICAvLyBiZWZvcmUgb2Zmc2V0IGxvY2F0aW9uXG4gICAgICBpZiAobWluTGluZSA8PSBzdGFydCAtIGxvY2FsT2Zmc2V0KSB7XG4gICAgICAgIHJldHVybiBzdGFydCAtIGxvY2FsT2Zmc2V0Kys7XG4gICAgICB9XG5cbiAgICAgIGJhY2t3YXJkRXhoYXVzdGVkID0gdHJ1ZTtcbiAgICAgIHJldHVybiBpdGVyYXRvcigpO1xuICAgIH1cblxuICAgIC8vIFdlIHRyaWVkIHRvIGZpdCBodW5rIGJlZm9yZSB0ZXh0IGJlZ2lubmluZyBhbmQgYmV5b25kIHRleHQgbGVuZ3RoLCB0aGVuXG4gICAgLy8gaHVuayBjYW4ndCBmaXQgb24gdGhlIHRleHQuIFJldHVybiB1bmRlZmluZWRcbiAgfTtcbn1cbiJdLCJtYXBwaW5ncyI6Ijs7Ozs7Ozs7QUFBQTtBQUNBO0FBQ0E7QUFDZTtBQUFBO0FBQUFBO0FBQUFBO0FBQUEsQ0FBU0MsS0FBSyxFQUFFQyxPQUFPLEVBQUVDLE9BQU8sRUFBRTtFQUMvQyxJQUFJQyxXQUFXLEdBQUcsSUFBSTtJQUNsQkMsaUJBQWlCLEdBQUcsS0FBSztJQUN6QkMsZ0JBQWdCLEdBQUcsS0FBSztJQUN4QkMsV0FBVyxHQUFHLENBQUM7RUFFbkIsT0FBTyxTQUFTQyxRQUFRQSxDQUFBLEVBQUc7SUFDekIsSUFBSUosV0FBVyxJQUFJLENBQUNFLGdCQUFnQixFQUFFO01BQ3BDLElBQUlELGlCQUFpQixFQUFFO1FBQ3JCRSxXQUFXLEVBQUU7TUFDZixDQUFDLE1BQU07UUFDTEgsV0FBVyxHQUFHLEtBQUs7TUFDckI7O01BRUE7TUFDQTtNQUNBLElBQUlILEtBQUssR0FBR00sV0FBVyxJQUFJSixPQUFPLEVBQUU7UUFDbEMsT0FBT0YsS0FBSyxHQUFHTSxXQUFXO01BQzVCO01BRUFELGdCQUFnQixHQUFHLElBQUk7SUFDekI7SUFFQSxJQUFJLENBQUNELGlCQUFpQixFQUFFO01BQ3RCLElBQUksQ0FBQ0MsZ0JBQWdCLEVBQUU7UUFDckJGLFdBQVcsR0FBRyxJQUFJO01BQ3BCOztNQUVBO01BQ0E7TUFDQSxJQUFJRixPQUFPLElBQUlELEtBQUssR0FBR00sV0FBVyxFQUFFO1FBQ2xDLE9BQU9OLEtBQUssR0FBR00sV0FBVyxFQUFFO01BQzlCO01BRUFGLGlCQUFpQixHQUFHLElBQUk7TUFDeEIsT0FBT0csUUFBUSxDQUFDLENBQUM7SUFDbkI7O0lBRUE7SUFDQTtFQUNGLENBQUM7QUFDSCIsImlnbm9yZUxpc3QiOltdfQ==
