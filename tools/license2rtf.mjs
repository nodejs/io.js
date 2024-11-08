import assert from 'node:assert';
import Stream from 'node:stream';
import { pipeline } from 'node:stream/promises';
import { stdin, stdout } from 'node:process';

/*
 * This filter consumes a stream of characters and emits one string per line.
 */
class LineSplitter extends Stream {
  constructor() {
    super();
    this.buffer = '';
    this.writable = true;
  }

  write(data) {
    const lines = (this.buffer + data).split(/\r\n|\n\r|\n|\r/);
    for (let i = 0; i < lines.length - 1; i++) {
      this.emit('data', lines[i]);
    }
    this.buffer = lines[lines.length - 1];
    return true;
  }

  end(data) {
    this.write(data || '');
    if (this.buffer) {
      this.emit('data', this.buffer);
    }
    this.writable = false;
    this.emit('end');
  }
}


/*
 * This filter consumes lines and emits paragraph objects.
 */
class ParagraphParser extends Stream {
  constructor() {
    super();
    this.blockIsLicenseBlock = false;
    this.writable = true;
    this.resetBlock(false);
  }

  write(data) {
    this.parseLine(data + '');
    return true;
  }

  end(data) {
    if (data)
      this.parseLine(data + '');
    this.flushParagraph();
    this.writable = false;
    this.emit('end');
  }

  resetParagraph() {
    this.paragraphLineIndent = -1;

    this.paragraph = {
      li: '',
      inLicenseBlock: this.blockIsLicenseBlock,
      lines: [],
    };
  }

  resetBlock(isLicenseBlock) {
    this.blockIsLicenseBlock = isLicenseBlock;
    this.blockHasCStyleComment = false;
    this.resetParagraph();
  }

  flushParagraph() {
    if (this.paragraph.lines.length || this.paragraph.li) {
      this.emit('data', this.paragraph);
    }
    this.resetParagraph();
  }

  parseLine(line) {
    // Strip trailing whitespace
    line = line.trimRight();

    // Detect block separator
    if (/^\s*(=|"){3,}\s*$/.test(line)) {
      this.flushParagraph();
      this.resetBlock(!this.blockIsLicenseBlock);
      return;
    }

    // Strip comments around block
    if (this.blockIsLicenseBlock) {
      this.blockHasCStyleComment ||= /^\s*(\/\*)/.test(line);
      if (this.blockHasCStyleComment) {
        const prev = line;
        line = line.replace(/^(\s*?)(?:\s?\*\/|\/\*\s|\s\*\s?)/, '$1');
        if (prev === line)
          line = line.replace(/^\s{2}/, '');
        if (/\*\//.test(prev))
          this.blockHasCStyleComment = false;
      } else {
        // Strip C++ and perl style comments.
        line = line.replace(/^(\s*)(?:\/\/\s?|#\s?)/, '$1');
      }
    }

    // Detect blank line (paragraph separator)
    if (!/\S/.test(line)) {
      this.flushParagraph();
      return;
    }

    // Detect separator "lines" within a block. These mark a paragraph break
    // and are stripped from the output.
    if (/^\s*[=*-]{5,}\s*$/.test(line)) {
      this.flushParagraph();
      return;
    }

    // Find out indentation level and the start of a lied or numbered list;
    const result = /^(\s*)(\d+\.|\*|-)?\s*/.exec(line);
    assert.ok(result);
    // The number of characters that will be stripped from the beginning of
    // the line.
    const lineStripLength = result[0].length;
    // The indentation size that will be used to detect indentation jumps.
    // Fudge by 1 space.
    const lineIndent = Math.floor(lineStripLength / 2) * 2;
    // The indentation level that will be exported
    const level = Math.floor(result[1].length / 2);
    // The list indicator that precedes the actual content, if any.
    const lineLi = result[2];

    // Flush the paragraph when there is a li or an indentation jump
    if (lineLi || (lineIndent !== this.paragraphLineIndent &&
                   this.paragraphLineIndent !== -1)) {
      this.flushParagraph();
      this.paragraph.li = lineLi;
    }

    // Set the paragraph indent that we use to detect indentation jumps. When
    // we just detected a list indicator, wait
    // for the next line to arrive before setting this.
    if (!lineLi && this.paragraphLineIndent !== -1) {
      this.paragraphLineIndent = lineIndent;
    }

    // Set the output indent level if it has not been set yet.
    if (this.paragraph.level === undefined)
      this.paragraph.level = level;

    // Strip leading whitespace and li.
    line = line.slice(lineStripLength);

    if (line)
      this.paragraph.lines.push(line);
  }
}


/*
 * This filter consumes paragraph objects and emits modified paragraph objects.
 * The lines within the paragraph are unwrapped where appropriate. It also
 * replaces multiple consecutive whitespace characters by a single one.
 */
class Unwrapper extends Stream {
  constructor() {
    super();
    this.writable = true;
  }

  write(paragraph) {
    const lines = paragraph.lines;
    const breakAfter = [];
    let i;

    for (i = 0; i < lines.length - 1; i++) {
      const line = lines[i];

      // When a line is really short, the line was probably kept separate for a
      // reason.
      if (line.length < 50) {
        // If the first word on the next line really didn't fit after the line,
        // it probably was just ordinary wrapping after all.
        const nextFirstWordLength = lines[i + 1].replace(/\s.*$/, '').length;
        if (line.length + nextFirstWordLength < 60) {
          breakAfter[i] = true;
        }
      }
    }

    for (i = 0; i < lines.length - 1;) {
      if (!breakAfter[i]) {
        lines[i] += ` ${lines.splice(i + 1, 1)[0]}`;
      } else {
        i++;
      }
    }

    for (i = 0; i < lines.length; i++) {
      // Replace multiple whitespace characters by a single one, and strip
      // trailing whitespace.
      lines[i] = lines[i].replace(/\s+/g, ' ').replace(/\s+$/, '');
    }

    this.emit('data', paragraph);
  }

  end(data) {
    if (data)
      this.write(data);
    this.writable = false;
    this.emit('end');
  }
}

function rtfEscape(string) {
  function toHex(number, length) {
    return (~~number).toString(16).padStart(length, '0');
  }

  return string
    .replace(/[\\{}]/g, (m) => `\\${m}`)
    .replace(/\t/g, () => '\\tab ')
    // eslint-disable-next-line no-control-regex
    .replace(/[\x00-\x1f\x7f-\xff]/g, (m) => `\\'${toHex(m.charCodeAt(0), 2)}`)
    .replace(/\ufeff/g, '')
    .replace(/[\u0100-\uffff]/g, (m) => `\\u${toHex(m.charCodeAt(0), 4)}?`);
}

/*
 * This filter generates an rtf document from a stream of paragraph objects.
 */
class RtfGenerator extends Stream {
  constructor() {
    super();
    this.didWriteAnything = false;
    this.writable = true;
  }

  write({ li, level, lines, inLicenseBlock: lic }) {
    if (!this.didWriteAnything) {
      this.emitHeader();
      this.didWriteAnything = true;
    }

    if (li)
      level++;

    let rtf = '\\pard\\sa150\\sl300\\slmult1';
    if (level > 0)
      rtf += `\\li${level * 240}`;
    if (li)
      rtf += `\\tx${level * 240}\\fi-240`;
    if (lic)
      rtf += '\\ri240';
    if (!lic)
      rtf += '\\b';
    if (li)
      rtf += ` ${li}\\tab`;
    rtf += ` ${lines.map(rtfEscape).join('\\line ')}`;
    if (!lic)
      rtf += '\\b0';
    rtf += '\\par\n';

    this.emit('data', rtf);
  }

  end(data) {
    if (data)
      this.write(data);
    if (this.didWriteAnything)
      this.emitFooter();
    this.writable = false;
    this.emit('end');
  }

  emitHeader() {
    this.emit('data', '{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0\\deflang1033' +
                      '{\\fonttbl{\\f0\\fswiss\\fcharset0 Tahoma;}}\\fs20\n' +
                      '{\\*\\generator txt2rtf 0.0.1;}\n');
  }

  emitFooter() {
    this.emit('data', '}');
  }
}

stdin.setEncoding('utf-8');
stdin.resume();

await pipeline(
  stdin,
  new LineSplitter(),
  new ParagraphParser(),
  new Unwrapper(),
  new RtfGenerator(),
  stdout,
);
