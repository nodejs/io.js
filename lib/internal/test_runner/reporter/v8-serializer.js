'use strict';

const {
  TypedArrayPrototypeGetLength,
} = primordials;
const { DefaultSerializer } = require('v8');
const { Buffer } = require('buffer');
const { serializeError } = require('internal/error_serdes');


module.exports = async function* v8Reporter(source) {
  const serializer = new DefaultSerializer();
  serializer.writeHeader();
  const headerLength = TypedArrayPrototypeGetLength(serializer.releaseBuffer());

  for await (const item of source) {
    const originalError = item.data.details?.error;
    if (originalError) {
      // Error is overridden with a serialized version, so that it can be
      // deserialized in the parent process.
      // Error is restored after serialization.
      item.data.details.error = serializeError(originalError);
    }
    serializer.writeHeader();
    // Add 4 bytes, to later populate with message length
    serializer.writeRawBytes(Buffer.allocUnsafe(4));
    serializer.writeHeader();
    serializer.writeValue(item);

    if (originalError) {
      item.data.details.error = originalError;
    }

    const serializedMessage = serializer.releaseBuffer();
    const serializedMessageLength = serializedMessage.length - (4 + headerLength);

    serializedMessage.set([
      serializedMessageLength >> 24 & 0xFF,
      serializedMessageLength >> 16 & 0xFF,
      serializedMessageLength >> 8 & 0xFF,
      serializedMessageLength & 0xFF,
    ], headerLength);
    yield serializedMessage;
  }
};
