'use strict';

const {
  ArrayIsArray,
  ArrayPrototypeIncludes,
  ArrayPrototypePush,
  MathAbs,
  MathMax,
  MathMin,
  MathRound,
  NumberParseInt,
  RegExpPrototypeExec,
  SafeMap,
  StringPrototypeSplit,
  StringPrototypeTrim,
} = primordials;

// Copyright 2018-2023 the Deno authors. All rights reserved. MIT license.
// https://github.com/denoland/deno/blob/ece2a3de5b19588160634452638aa656218853c5/ext/console/01_console.js#L2575
const colorKeywords = new SafeMap([
  ['black', '#000000'],
  ['silver', '#c0c0c0'],
  ['gray', '#808080'],
  ['white', '#ffffff'],
  ['maroon', '#800000'],
  ['red', '#ff0000'],
  ['purple', '#800080'],
  ['fuchsia', '#ff00ff'],
  ['green', '#008000'],
  ['lime', '#00ff00'],
  ['olive', '#808000'],
  ['yellow', '#ffff00'],
  ['navy', '#000080'],
  ['blue', '#0000ff'],
  ['teal', '#008080'],
  ['aqua', '#00ffff'],
  ['orange', '#ffa500'],
  ['aliceblue', '#f0f8ff'],
  ['antiquewhite', '#faebd7'],
  ['aquamarine', '#7fffd4'],
  ['azure', '#f0ffff'],
  ['beige', '#f5f5dc'],
  ['bisque', '#ffe4c4'],
  ['blanchedalmond', '#ffebcd'],
  ['blueviolet', '#8a2be2'],
  ['brown', '#a52a2a'],
  ['burlywood', '#deb887'],
  ['cadetblue', '#5f9ea0'],
  ['chartreuse', '#7fff00'],
  ['chocolate', '#d2691e'],
  ['coral', '#ff7f50'],
  ['cornflowerblue', '#6495ed'],
  ['cornsilk', '#fff8dc'],
  ['crimson', '#dc143c'],
  ['cyan', '#00ffff'],
  ['darkblue', '#00008b'],
  ['darkcyan', '#008b8b'],
  ['darkgoldenrod', '#b8860b'],
  ['darkgray', '#a9a9a9'],
  ['darkgreen', '#006400'],
  ['darkgrey', '#a9a9a9'],
  ['darkkhaki', '#bdb76b'],
  ['darkmagenta', '#8b008b'],
  ['darkolivegreen', '#556b2f'],
  ['darkorange', '#ff8c00'],
  ['darkorchid', '#9932cc'],
  ['darkred', '#8b0000'],
  ['darksalmon', '#e9967a'],
  ['darkseagreen', '#8fbc8f'],
  ['darkslateblue', '#483d8b'],
  ['darkslategray', '#2f4f4f'],
  ['darkslategrey', '#2f4f4f'],
  ['darkturquoise', '#00ced1'],
  ['darkviolet', '#9400d3'],
  ['deeppink', '#ff1493'],
  ['deepskyblue', '#00bfff'],
  ['dimgray', '#696969'],
  ['dimgrey', '#696969'],
  ['dodgerblue', '#1e90ff'],
  ['firebrick', '#b22222'],
  ['floralwhite', '#fffaf0'],
  ['forestgreen', '#228b22'],
  ['gainsboro', '#dcdcdc'],
  ['ghostwhite', '#f8f8ff'],
  ['gold', '#ffd700'],
  ['goldenrod', '#daa520'],
  ['greenyellow', '#adff2f'],
  ['grey', '#808080'],
  ['honeydew', '#f0fff0'],
  ['hotpink', '#ff69b4'],
  ['indianred', '#cd5c5c'],
  ['indigo', '#4b0082'],
  ['ivory', '#fffff0'],
  ['khaki', '#f0e68c'],
  ['lavender', '#e6e6fa'],
  ['lavenderblush', '#fff0f5'],
  ['lawngreen', '#7cfc00'],
  ['lemonchiffon', '#fffacd'],
  ['lightblue', '#add8e6'],
  ['lightcoral', '#f08080'],
  ['lightcyan', '#e0ffff'],
  ['lightgoldenrodyellow', '#fafad2'],
  ['lightgray', '#d3d3d3'],
  ['lightgreen', '#90ee90'],
  ['lightgrey', '#d3d3d3'],
  ['lightpink', '#ffb6c1'],
  ['lightsalmon', '#ffa07a'],
  ['lightseagreen', '#20b2aa'],
  ['lightskyblue', '#87cefa'],
  ['lightslategray', '#778899'],
  ['lightslategrey', '#778899'],
  ['lightsteelblue', '#b0c4de'],
  ['lightyellow', '#ffffe0'],
  ['limegreen', '#32cd32'],
  ['linen', '#faf0e6'],
  ['magenta', '#ff00ff'],
  ['mediumaquamarine', '#66cdaa'],
  ['mediumblue', '#0000cd'],
  ['mediumorchid', '#ba55d3'],
  ['mediumpurple', '#9370db'],
  ['mediumseagreen', '#3cb371'],
  ['mediumslateblue', '#7b68ee'],
  ['mediumspringgreen', '#00fa9a'],
  ['mediumturquoise', '#48d1cc'],
  ['mediumvioletred', '#c71585'],
  ['midnightblue', '#191970'],
  ['mintcream', '#f5fffa'],
  ['mistyrose', '#ffe4e1'],
  ['moccasin', '#ffe4b5'],
  ['navajowhite', '#ffdead'],
  ['oldlace', '#fdf5e6'],
  ['olivedrab', '#6b8e23'],
  ['orangered', '#ff4500'],
  ['orchid', '#da70d6'],
  ['palegoldenrod', '#eee8aa'],
  ['palegreen', '#98fb98'],
  ['paleturquoise', '#afeeee'],
  ['palevioletred', '#db7093'],
  ['papayawhip', '#ffefd5'],
  ['peachpuff', '#ffdab9'],
  ['peru', '#cd853f'],
  ['pink', '#ffc0cb'],
  ['plum', '#dda0dd'],
  ['powderblue', '#b0e0e6'],
  ['rosybrown', '#bc8f8f'],
  ['royalblue', '#4169e1'],
  ['saddlebrown', '#8b4513'],
  ['salmon', '#fa8072'],
  ['sandybrown', '#f4a460'],
  ['seagreen', '#2e8b57'],
  ['seashell', '#fff5ee'],
  ['sienna', '#a0522d'],
  ['skyblue', '#87ceeb'],
  ['slateblue', '#6a5acd'],
  ['slategray', '#708090'],
  ['slategrey', '#708090'],
  ['snow', '#fffafa'],
  ['springgreen', '#00ff7f'],
  ['steelblue', '#4682b4'],
  ['tan', '#d2b48c'],
  ['thistle', '#d8bfd8'],
  ['tomato', '#ff6347'],
  ['turquoise', '#40e0d0'],
  ['violet', '#ee82ee'],
  ['wheat', '#f5deb3'],
  ['whitesmoke', '#f5f5f5'],
  ['yellowgreen', '#9acd32'],
  ['rebeccapurple', '#663399'],
]);

const HASH_PATTERN = /^#([\dA-Fa-f]{2})([\dA-Fa-f]{2})([\dA-Fa-f]{2})([\dA-Fa-f]{2})?$/;
const SMALL_HASH_PATTERN = /^#([\dA-Fa-f])([\dA-Fa-f])([\dA-Fa-f])([\dA-Fa-f])?$/;
const RGB_PATTERN = /^rgba?\(\s*([+-]?\d*\.?\d+)\s*,\s*([+-]?\d*\.?\d+)\s*,\s*([+-]?\d*\.?\d+)\s*(,\s*([+-]?\d*\.?\d+)\s*)?\)$/;
const HSL_PATTERN = /^hsla?\(\s*([+-]?\d*\.?\d+)\s*,\s*([+-]?\d*\.?\d+)%\s*,\s*([+-]?\d*\.?\d+)%\s*(,\s*([+-]?\d*\.?\d+)\s*)?\)$/;

// Copyright 2018-2023 the Deno authors. All rights reserved. MIT license.
// https://github.com/denoland/deno/blob/ece2a3de5b19588160634452638aa656218853c5/ext/console/01_console.js#L2739
function parseCssColor(colorString) {
  if (colorKeywords.has(colorString)) {
    colorString = colorKeywords.get(colorString);
  }
  const hashMatch = RegExpPrototypeExec(HASH_PATTERN, colorString);
  if (hashMatch != null) {
    return [
      NumberParseInt(hashMatch[1], 16),
      NumberParseInt(hashMatch[2], 16),
      NumberParseInt(hashMatch[3], 16),
    ];
  }
  const smallHashMatch = RegExpPrototypeExec(SMALL_HASH_PATTERN, colorString);
  if (smallHashMatch != null) {
    return [
      NumberParseInt(`${smallHashMatch[1]}${smallHashMatch[1]}`, 16),
      NumberParseInt(`${smallHashMatch[2]}${smallHashMatch[2]}`, 16),
      NumberParseInt(`${smallHashMatch[3]}${smallHashMatch[3]}`, 16),
    ];
  }
  const rgbMatch = RegExpPrototypeExec(RGB_PATTERN, colorString);
  if (rgbMatch != null) {
    return [
      MathRound(MathMax(0, MathMin(255, rgbMatch[1]))),
      MathRound(MathMax(0, MathMin(255, rgbMatch[2]))),
      MathRound(MathMax(0, MathMin(255, rgbMatch[3]))),
    ];
  }
  // deno-fmt-ignore
  const hslMatch = RegExpPrototypeExec(HSL_PATTERN, colorString);
  if (hslMatch != null) {
    // https://www.rapidtables.com/convert/color/hsl-to-rgb.html
    let h = hslMatch[1] % 360;
    if (h < 0) {
      h += 360;
    }
    const s = MathMax(0, MathMin(100, hslMatch[2])) / 100;
    const l = MathMax(0, MathMin(100, hslMatch[3])) / 100;
    const c = (1 - MathAbs(2 * l - 1)) * s;
    const x = c * (1 - MathAbs((h / 60) % 2 - 1));
    const m = l - c / 2;
    let r_;
    let g_;
    let b_;
    if (h < 60) {
      ({ 0: r_, 1: g_, 2: b_ } = [c, x, 0]);
    } else if (h < 120) {
      ({ 0: r_, 1: g_, 2: b_ } = [x, c, 0]);
    } else if (h < 180) {
      ({ 0: r_, 1: g_, 2: b_ } = [0, c, x]);
    } else if (h < 240) {
      ({ 0: r_, 1: g_, 2: b_ } = [0, x, c]);
    } else if (h < 300) {
      ({ 0: r_, 1: g_, 2: b_ } = [x, 0, c]);
    } else {
      ({ 0: r_, 1: g_, 2: b_ } = [c, 0, x]);
    }
    return [
      MathRound((r_ + m) * 255),
      MathRound((g_ + m) * 255),
      MathRound((b_ + m) * 255),
    ];
  }
  return null;
}

function getDefaultCss() {
  return {
    __proto__: null,
    backgroundColor: null,
    color: null,
    fontWeight: null,
    fontStyle: null,
    textDecorationColor: null,
    textDecorationLine: [],
  };
}

const SPACE_PATTERN = /\s+/g;

// Copyright 2018-2023 the Deno authors. All rights reserved. MIT license.
// https://github.com/denoland/deno/blob/ece2a3de5b19588160634452638aa656218853c5/ext/console/01_console.js#L2821
function parseCss(cssString) {
  const css = getDefaultCss();

  const rawEntries = [];
  let inValue = false;
  let currentKey = null;
  let parenthesesDepth = 0;
  let currentPart = '';
  for (let i = 0; i < cssString.length; i++) {
    const c = cssString[i];
    if (c === '(') {
      parenthesesDepth++;
    } else if (parenthesesDepth > 0) {
      if (c === ')') {
        parenthesesDepth--;
      }
    } else if (inValue) {
      if (c === ';') {
        const value = StringPrototypeTrim(currentPart);
        if (value !== '') {
          ArrayPrototypePush(rawEntries, [currentKey, value]);
        }
        currentKey = null;
        currentPart = '';
        inValue = false;
        continue;
      }
    } else if (c === ':') {
      currentKey = StringPrototypeTrim(currentPart);
      currentPart = '';
      inValue = true;
      continue;
    }
    currentPart += c;
  }
  if (inValue && parenthesesDepth === 0) {
    const value = StringPrototypeTrim(currentPart);
    if (value !== '') {
      ArrayPrototypePush(rawEntries, [currentKey, value]);
    }
    currentKey = null;
    currentPart = '';
  }

  for (let i = 0; i < rawEntries.length; ++i) {
    const { 0: key, 1: value } = rawEntries[i];
    if (key === 'background-color') {
      if (value != null) {
        css.backgroundColor = value;
      }
    } else if (key === 'color') {
      if (value != null) {
        css.color = value;
      }
    } else if (key === 'font-weight') {
      if (value === 'bold') {
        css.fontWeight = value;
      }
    } else if (key === 'font-style') {
      if (
        ArrayPrototypeIncludes(['italic', 'oblique', 'oblique 14deg'], value)
      ) {
        css.fontStyle = 'italic';
      }
    } else if (key === 'text-decoration-line') {
      css.textDecorationLine = [];
      const lineTypes = StringPrototypeSplit(value, SPACE_PATTERN);
      for (let i = 0; i < lineTypes.length; ++i) {
        const lineType = lineTypes[i];
        if (
          ArrayPrototypeIncludes(
            ['line-through', 'overline', 'underline'],
            lineType,
          )
        ) {
          ArrayPrototypePush(css.textDecorationLine, lineType);
        }
      }
    } else if (key === 'text-decoration-color') {
      const color = parseCssColor(value);
      if (color != null) {
        css.textDecorationColor = color;
      }
    } else if (key === 'text-decoration') {
      css.textDecorationColor = null;
      css.textDecorationLine = [];
      const args = StringPrototypeSplit(value, SPACE_PATTERN);
      for (let i = 0; i < args.length; ++i) {
        const arg = args[i];
        const maybeColor = parseCssColor(arg);
        if (maybeColor != null) {
          css.textDecorationColor = maybeColor;
        } else if (
          ArrayPrototypeIncludes(['line-through', 'overline', 'underline'], arg)
        ) {
          ArrayPrototypePush(css.textDecorationLine, arg);
        }
      }
    }
  }

  return css;
}

// Copyright 2018-2023 the Deno authors. All rights reserved. MIT license.
// https://github.com/denoland/deno/blob/ece2a3de5b19588160634452638aa656218853c5/ext/console/01_console.js#L2928
function colorEquals(color1, color2) {
  return color1?.[0] === color2?.[0] && color1?.[1] === color2?.[1] &&
    color1?.[2] === color2?.[2];
}

// Copyright 2018-2023 the Deno authors. All rights reserved. MIT license.
// https://github.com/denoland/deno/blob/ece2a3de5b19588160634452638aa656218853c5/ext/console/01_console.js#L2933
function cssToAnsi(css, prevCss = null) {
  prevCss = prevCss ?? getDefaultCss();
  let ansi = '';
  if (!colorEquals(css.backgroundColor, prevCss.backgroundColor)) {
    // Why not use util.inspect.colors? Because it's user mutable. The colors
    // here would be changeable like
    // `util.inspect.colors.black = util.inspect.colors.red` but what about
    // the colorKeywords map like "color:salmon"? Using util.inspect.colors
    // would introduce a weird edge-case and require more cognitive overhead
    // to read & understand since it's in a different module.
    if (css.backgroundColor == null) {
      ansi += '\x1b[49m';
    } else if (css.backgroundColor === 'black') {
      ansi += '\x1b[40m';
    } else if (css.backgroundColor === 'red') {
      ansi += '\x1b[41m';
    } else if (css.backgroundColor === 'green') {
      ansi += '\x1b[42m';
    } else if (css.backgroundColor === 'yellow') {
      ansi += '\x1b[43m';
    } else if (css.backgroundColor === 'blue') {
      ansi += '\x1b[44m';
    } else if (css.backgroundColor === 'magenta') {
      ansi += '\x1b[45m';
    } else if (css.backgroundColor === 'cyan') {
      ansi += '\x1b[46m';
    } else if (css.backgroundColor === 'white') {
      ansi += '\x1b[47m';
    } else if (ArrayIsArray(css.backgroundColor)) {
      const { 0: r, 1: g, 2: b } = css.backgroundColor;
      ansi += `\x1b[48;2;${r};${g};${b}m`;
    } else {
      const parsed = parseCssColor(css.backgroundColor);
      if (parsed !== null) {
        const { 0: r, 1: g, 2: b } = parsed;
        ansi += `\x1b[48;2;${r};${g};${b}m`;
      } else {
        ansi += '\x1b[49m';
      }
    }
  }
  if (!colorEquals(css.color, prevCss.color)) {
    // Not using util.inspect.colors map. See above backgroundColor comment.
    if (css.color == null) {
      ansi += '\x1b[39m';
    } else if (css.color === 'black') {
      ansi += '\x1b[30m';
    } else if (css.color === 'red') {
      ansi += '\x1b[31m';
    } else if (css.color === 'green') {
      ansi += '\x1b[32m';
    } else if (css.color === 'yellow') {
      ansi += '\x1b[33m';
    } else if (css.color === 'blue') {
      ansi += '\x1b[34m';
    } else if (css.color === 'magenta') {
      ansi += '\x1b[35m';
    } else if (css.color === 'cyan') {
      ansi += '\x1b[36m';
    } else if (css.color === 'white') {
      ansi += '\x1b[37m';
    } else if (ArrayIsArray(css.color)) {
      const { 0: r, 1: g, 2: b } = css.color;
      ansi += `\x1b[38;2;${r};${g};${b}m`;
    } else {
      const parsed = parseCssColor(css.color);
      if (parsed !== null) {
        const { 0: r, 1: g, 2: b } = parsed;
        ansi += `\x1b[38;2;${r};${g};${b}m`;
      } else {
        ansi += '\x1b[39m';
      }
    }
  }
  if (css.fontWeight !== prevCss.fontWeight) {
    if (css.fontWeight === 'bold') {
      ansi += '\x1b[1m';
    } else {
      ansi += '\x1b[22m';
    }
  }
  if (css.fontStyle !== prevCss.fontStyle) {
    if (css.fontStyle === 'italic') {
      ansi += '\x1b[3m';
    } else {
      ansi += '\x1b[23m';
    }
  }
  if (!colorEquals(css.textDecorationColor, prevCss.textDecorationColor)) {
    if (css.textDecorationColor != null) {
      const { 0: r, 1: g, 2: b } = css.textDecorationColor;
      ansi += `\x1b[58;2;${r};${g};${b}m`;
    } else {
      ansi += '\x1b[59m';
    }
  }
  if (
    ArrayPrototypeIncludes(css.textDecorationLine, 'line-through') !==
    ArrayPrototypeIncludes(prevCss.textDecorationLine, 'line-through')
  ) {
    if (ArrayPrototypeIncludes(css.textDecorationLine, 'line-through')) {
      ansi += '\x1b[9m';
    } else {
      ansi += '\x1b[29m';
    }
  }
  if (
    ArrayPrototypeIncludes(css.textDecorationLine, 'overline') !==
    ArrayPrototypeIncludes(prevCss.textDecorationLine, 'overline')
  ) {
    if (ArrayPrototypeIncludes(css.textDecorationLine, 'overline')) {
      ansi += '\x1b[53m';
    } else {
      ansi += '\x1b[55m';
    }
  }
  if (
    ArrayPrototypeIncludes(css.textDecorationLine, 'underline') !==
    ArrayPrototypeIncludes(prevCss.textDecorationLine, 'underline')
  ) {
    if (ArrayPrototypeIncludes(css.textDecorationLine, 'underline')) {
      ansi += '\x1b[4m';
    } else {
      ansi += '\x1b[24m';
    }
  }
  return ansi;
}

module.exports = {
  parseCss,
  cssToAnsi,
};
