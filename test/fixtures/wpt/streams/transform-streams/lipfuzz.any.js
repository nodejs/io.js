// META: global=window,worker,shadowrealm
'use strict';

class LipFuzzTransformer {
  constructor(substitutions) {
    this.substitutions = substitutions;
    this.partialChunk = '';
    this.lastIndex = undefined;
  }

  transform(chunk, controller) {
    chunk = this.partialChunk + chunk;
    this.partialChunk = '';
    // lastIndex is the index of the first character after the last substitution.
    this.lastIndex = 0;
    chunk = chunk.replace(/\{\{([a-zA-Z0-9_-]+)\}\}/g, this.replaceTag.bind(this));
    // Regular expression for an incomplete template at the end of a string.
    const partialAtEndRegexp = /\{(\{([a-zA-Z0-9_-]+(\})?)?)?$/g;
    // Avoid looking at any characters that have already been substituted.
    partialAtEndRegexp.lastIndex = this.lastIndex;
    this.lastIndex = undefined;
    const match = partialAtEndRegexp.exec(chunk);
    if (match) {
      this.partialChunk = chunk.substring(match.index);
      chunk = chunk.substring(0, match.index);
    }
    controller.enqueue(chunk);
  }

  flush(controller) {
    if (this.partialChunk.length > 0) {
      controller.enqueue(this.partialChunk);
    }
  }

  replaceTag(match, p1, offset) {
    let replacement = this.substitutions[p1];
    if (replacement === undefined) {
      replacement = '';
    }
    this.lastIndex = offset + replacement.length;
    return replacement;
  }
}

const substitutions = {
  in1: 'out1',
  in2: 'out2',
  quine: '{{quine}}',
  bogusPartial: '{{incompleteResult}'
};

const cases = [
  {
    input: [''],
    output: ['']
  },
  {
    input: [],
    output: []
  },
  {
    input: ['{{in1}}'],
    output: ['out1']
  },
  {
    input: ['z{{in1}}'],
    output: ['zout1']
  },
  {
    input: ['{{in1}}q'],
    output: ['out1q']
  },
  {
    input: ['{{in1}}{{in1}'],
    output: ['out1', '{{in1}']
  },
  {
    input: ['{{in1}}{{in1}', '}'],
    output: ['out1', 'out1']
  },
  {
    input: ['{{in1', '}}'],
    output: ['', 'out1']
  },
  {
    input: ['{{', 'in1}}'],
    output: ['', 'out1']
  },
  {
    input: ['{', '{in1}}'],
    output: ['', 'out1']
  },
  {
    input: ['{{', 'in1}'],
    output: ['', '', '{{in1}']
  },
  {
    input: ['{'],
    output: ['', '{']
  },
  {
    input: ['{', ''],
    output: ['', '', '{']
  },
  {
    input: ['{', '{', 'i', 'n', '1', '}', '}'],
    output: ['', '', '', '', '', '', 'out1']
  },
  {
    input: ['{{in1}}{{in2}}{{in1}}'],
    output: ['out1out2out1']
  },
  {
    input: ['{{wrong}}'],
    output: ['']
  },
  {
    input: ['{{wron', 'g}}'],
    output: ['', '']
  },
  {
    input: ['{{quine}}'],
    output: ['{{quine}}']
  },
  {
    input: ['{{bogusPartial}}'],
    output: ['{{incompleteResult}']
  },
  {
    input: ['{{bogusPartial}}}'],
    output: ['{{incompleteResult}}']
  }
];

for (const testCase of cases) {
  const inputChunks = testCase.input;
  const outputChunks = testCase.output;
  promise_test(() => {
    const lft = new TransformStream(new LipFuzzTransformer(substitutions));
    const writer = lft.writable.getWriter();
    const promises = [];
    for (const inputChunk of inputChunks) {
      promises.push(writer.write(inputChunk));
    }
    promises.push(writer.close());
    const reader = lft.readable.getReader();
    let readerChain = Promise.resolve();
    for (const outputChunk of outputChunks) {
      readerChain = readerChain.then(() => {
        return reader.read().then(({ value, done }) => {
          assert_false(done, `done should be false when reading ${outputChunk}`);
          assert_equals(value, outputChunk, `value should match outputChunk`);
        });
      });
    }
    readerChain = readerChain.then(() => {
      return reader.read().then(({ done }) => assert_true(done, `done should be true`));
    });
    promises.push(readerChain);
    return Promise.all(promises);
  }, `testing "${inputChunks}" (length ${inputChunks.length})`);
}
