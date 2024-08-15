/* MIT License
 *
 * Copyright (c) 2023 Brad House
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef __ARES__BUF_H
#define __ARES__BUF_H

/*! \addtogroup ares__buf Safe Data Builder and buffer
 *
 * This is a buffer building and parsing framework with a focus on security over
 * performance. All data to be read from the buffer will perform explicit length
 * validation and return a success/fail result.  There are also various helpers
 * for writing data to the buffer which dynamically grows.
 *
 * All operations that fetch or consume data from the buffer will move forward
 * the internal pointer, thus marking the data as processed which may no longer
 * be accessible after certain operations (such as append).
 *
 * The helpers for this object are meant to be added as needed.  If you can't
 * find it, write it!
 *
 * @{
 */
struct ares__buf;

/*! Opaque data type for generic hash table implementation */
typedef struct ares__buf ares__buf_t;

/*! Create a new buffer object that dynamically allocates buffers for data.
 *
 *  \return initialized buffer object or NULL if out of memory.
 */
ares__buf_t             *ares__buf_create(void);

/*! Create a new buffer object that uses a user-provided data pointer.  The
 *  data provided will not be manipulated, and cannot be appended to.  This
 *  is strictly used for parsing.
 *
 *  \param[in] data     Data to provide to buffer, must not be NULL.
 *  \param[in] data_len Size of buffer provided, must be > 0
 *
 *  \return initialized buffer object or NULL if out of memory or misuse.
 */
ares__buf_t *ares__buf_create_const(const unsigned char *data, size_t data_len);


/*! Destroy an initialized buffer object.
 *
 *  \param[in] buf  Initialized buf object
 */
void         ares__buf_destroy(ares__buf_t *buf);


/*! Append multiple bytes to a dynamic buffer object
 *
 *  \param[in] buf      Initialized buffer object
 *  \param[in] data     Data to copy to buffer object
 *  \param[in] data_len Length of data to copy to buffer object.
 *  \return ARES_SUCCESS or one of the c-ares error codes
 */
ares_status_t  ares__buf_append(ares__buf_t *buf, const unsigned char *data,
                                size_t data_len);

/*! Append a single byte to the dynamic buffer object
 *
 *  \param[in] buf      Initialized buffer object
 *  \param[in] b        Single byte to append to buffer object.
 *  \return ARES_SUCCESS or one of the c-ares error codes
 */
ares_status_t  ares__buf_append_byte(ares__buf_t *buf, unsigned char b);

/*! Append a null-terminated string to the dynamic buffer object
 *
 *  \param[in] buf      Initialized buffer object
 *  \param[in] str      String to append to buffer object.
 *  \return ARES_SUCCESS or one of the c-ares error codes
 */
ares_status_t  ares__buf_append_str(ares__buf_t *buf, const char *str);

/*! Append a 16bit Big Endian number to the buffer.
 *
 *  \param[in]  buf     Initialized buffer object
 *  \param[out] u16     16bit integer
 *  \return ARES_SUCCESS or one of the c-ares error codes
 */
ares_status_t  ares__buf_append_be16(ares__buf_t *buf, unsigned short u16);

/*! Append a 32bit Big Endian number to the buffer.
 *
 *  \param[in]  buf     Initialized buffer object
 *  \param[out] u32     32bit integer
 *  \return ARES_SUCCESS or one of the c-ares error codes
 */
ares_status_t  ares__buf_append_be32(ares__buf_t *buf, unsigned int u32);

/*! Append a number in ASCII decimal form.
 *
 *  \param[in] buf  Initialized buffer object
 *  \param[in] num  Number to print
 *  \param[in] len  Length to output, use 0 for no padding
 *  \return ARES_SUCCESS on success
 */
ares_status_t  ares__buf_append_num_dec(ares__buf_t *buf, size_t num,
                                        size_t len);

/*! Append a number in ASCII hexadecimal form.
 *
 *  \param[in] buf  Initialized buffer object
 *  \param[in] num  Number to print
 *  \param[in] len  Length to output, use 0 for no padding
 *  \return ARES_SUCCESS on success
 */
ares_status_t  ares__buf_append_num_hex(ares__buf_t *buf, size_t num,
                                        size_t len);

/*! Sets the current buffer length.  This *may* be used if there is a need to
 *  override a prior position in the buffer, such as if there is a length
 *  prefix that isn't easily predictable, and you must go back and overwrite
 *  that position.
 *
 *  Only valid on non-const buffers.  Length provided must not exceed current
 *  allocated buffer size, but otherwise there are very few protections on
 *  this function.  Use cautiously.
 *
 *  \param[in]  buf  Initialized buffer object
 *  \param[in]  len  Length to set
 *  \return ARES_SUCCESS or one of the c-ares error codes
 */
ares_status_t  ares__buf_set_length(ares__buf_t *buf, size_t len);


/*! Start a dynamic append operation that returns a buffer suitable for
 *  writing.  A desired minimum length is passed in, and the actual allocated
 *  buffer size is returned which may be greater than the requested size.
 *  No operation other than ares__buf_append_finish() is allowed on the
 *  buffer after this request.
 *
 *  \param[in]     buf     Initialized buffer object
 *  \param[in,out] len     Desired non-zero length passed in, actual buffer size
 *                         returned.
 *  \return Pointer to writable buffer or NULL on failure (usage, out of mem)
 */
unsigned char *ares__buf_append_start(ares__buf_t *buf, size_t *len);

/*! Finish a dynamic append operation.  Called after
 *  ares__buf_append_start() once desired data is written.
 *
 *  \param[in] buf    Initialized buffer object.
 *  \param[in] len    Length of data written.  May be zero to terminate
 *                    operation. Must not be greater than returned from
 *                    ares__buf_append_start().
 */
void           ares__buf_append_finish(ares__buf_t *buf, size_t len);

/*! Write the data provided to the buffer in a hexdump format.
 *
 *  \param[in] buf      Initialized buffer object.
 *  \param[in] data     Data to hex dump
 *  \param[in] len      Length of data to hexdump
 *  \return ARES_SUCCESS on success.
 */
ares_status_t  ares__buf_hexdump(ares__buf_t *buf, const unsigned char *data,
                                 size_t len);

/*! Clean up ares__buf_t and return allocated pointer to unprocessed data.  It
 *  is the responsibility of the  caller to ares_free() the returned buffer.
 *  The passed in buf parameter is invalidated by this call.
 *
 * \param[in]  buf    Initialized buffer object. Can not be a "const" buffer.
 * \param[out] len    Length of data returned
 * \return pointer to unprocessed data (may be zero length) or NULL on error.
 */
unsigned char *ares__buf_finish_bin(ares__buf_t *buf, size_t *len);

/*! Clean up ares__buf_t and return allocated pointer to unprocessed data and
 *  return it as a string (null terminated).  It is the responsibility of the
 *  caller to ares_free() the returned buffer. The passed in buf parameter is
 *  invalidated by this call.
 *
 *  This function in no way validates the data in this buffer is actually
 *  a string, that characters are printable, or that there aren't multiple
 *  NULL terminators.  It is assumed that the caller will either validate that
 *  themselves or has built this buffer with only a valid character set.
 *
 * \param[in]  buf    Initialized buffer object. Can not be a "const" buffer.
 * \param[out] len    Optional. Length of data returned, or NULL if not needed.
 * \return pointer to unprocessed data or NULL on error.
 */
char          *ares__buf_finish_str(ares__buf_t *buf, size_t *len);

/*! Tag a position to save in the buffer in case parsing needs to rollback,
 *  such as if insufficient data is available, but more data may be added in
 *  the future.  Only a single tag can be set per buffer object.  Setting a
 *  tag will override any pre-existing tag.
 *
 *  \param[in] buf Initialized buffer object
 */
void           ares__buf_tag(ares__buf_t *buf);

/*! Rollback to a tagged position.  Will automatically clear the tag.
 *
 *  \param[in] buf Initialized buffer object
 *  \return ARES_SUCCESS or one of the c-ares error codes
 */
ares_status_t  ares__buf_tag_rollback(ares__buf_t *buf);

/*! Clear the tagged position without rolling back.  You should do this any
 *  time a tag is no longer needed as future append operations can reclaim
 *  buffer space.
 *
 *  \param[in] buf Initialized buffer object
 *  \return ARES_SUCCESS or one of the c-ares error codes
 */
ares_status_t  ares__buf_tag_clear(ares__buf_t *buf);

/*! Fetch the buffer and length of data starting from the tagged position up
 *  to the _current_ position.  It will not unset the tagged position.  The
 *  data may be invalidated by any future ares__buf_*() calls.
 *
 *  \param[in]  buf    Initialized buffer object
 *  \param[out] len    Length between tag and current offset in buffer
 *  \return NULL on failure (such as no tag), otherwise pointer to start of
 *          buffer
 */
const unsigned char *ares__buf_tag_fetch(const ares__buf_t *buf, size_t *len);

/*! Get the length of the current tag offset to the current position.
 *
 *  \param[in]  buf    Initialized buffer object
 *  \return length
 */
size_t               ares__buf_tag_length(const ares__buf_t *buf);

/*! Fetch the bytes starting from the tagged position up to the _current_
 *  position using the provided buffer.  It will not unset the tagged position.
 *
 *  \param[in]     buf    Initialized buffer object
 *  \param[in,out] bytes  Buffer to hold data
 *  \param[in,out] len    On input, buffer size, on output, bytes place in
 *                        buffer.
 *  \return ARES_SUCCESS if fetched, ARES_EFORMERR if insufficient buffer size
 */
ares_status_t        ares__buf_tag_fetch_bytes(const ares__buf_t *buf,
                                               unsigned char *bytes, size_t *len);

/*! Fetch the bytes starting from the tagged position up to the _current_
 *  position as a NULL-terminated string using the provided buffer.  The data
 *  is validated to be ASCII-printable data.  It will not unset the tagged
 *  poition.
 *
 *  \param[in]     buf    Initialized buffer object
 *  \param[in,out] str    Buffer to hold data
 *  \param[in]     len    On input, buffer size, on output, bytes place in
 *                        buffer.
 *  \return ARES_SUCCESS if fetched, ARES_EFORMERR if insufficient buffer size,
 *          ARES_EBADSTR if not printable ASCII
 */
ares_status_t ares__buf_tag_fetch_string(const ares__buf_t *buf, char *str,
                                         size_t len);

/*! Consume the given number of bytes without reading them.
 *
 *  \param[in] buf    Initialized buffer object
 *  \param[in] len    Length to consume
 *  \return ARES_SUCCESS or one of the c-ares error codes
 */
ares_status_t ares__buf_consume(ares__buf_t *buf, size_t len);

/*! Fetch a 16bit Big Endian number from the buffer.
 *
 *  \param[in]  buf     Initialized buffer object
 *  \param[out] u16     Buffer to hold 16bit integer
 *  \return ARES_SUCCESS or one of the c-ares error codes
 */
ares_status_t ares__buf_fetch_be16(ares__buf_t *buf, unsigned short *u16);

/*! Fetch a 32bit Big Endian number from the buffer.
 *
 *  \param[in]  buf     Initialized buffer object
 *  \param[out] u32     Buffer to hold 32bit integer
 *  \return ARES_SUCCESS or one of the c-ares error codes
 */
ares_status_t ares__buf_fetch_be32(ares__buf_t *buf, unsigned int *u32);


/*! Fetch the requested number of bytes into the provided buffer
 *
 *  \param[in]  buf     Initialized buffer object
 *  \param[out] bytes   Buffer to hold data
 *  \param[in]  len     Requested number of bytes (must be > 0)
 *  \return ARES_SUCCESS or one of the c-ares error codes
 */
ares_status_t ares__buf_fetch_bytes(ares__buf_t *buf, unsigned char *bytes,
                                    size_t len);


/*! Fetch the requested number of bytes and return a new buffer that must be
 *  ares_free()'d by the caller.
 *
 *  \param[in]  buf       Initialized buffer object
 *  \param[in]  len       Requested number of bytes (must be > 0)
 *  \param[in]  null_term Even though this is considered binary data, the user
 *                        knows it may be a vald string, so add a null
 *                        terminator.
 *  \param[out] bytes     Pointer passed by reference. Will be allocated.
 *  \return ARES_SUCCESS or one of the c-ares error codes
 */
ares_status_t ares__buf_fetch_bytes_dup(ares__buf_t *buf, size_t len,
                                        ares_bool_t     null_term,
                                        unsigned char **bytes);

/*! Fetch the requested number of bytes and place them into the provided
 *  dest buffer object.
 *
 *  \param[in]  buf     Initialized buffer object
 *  \param[out] dest    Buffer object to append bytes.
 *  \param[in]  len     Requested number of bytes (must be > 0)
 *  \return ARES_SUCCESS or one of the c-ares error codes
 */
ares_status_t ares__buf_fetch_bytes_into_buf(ares__buf_t *buf,
                                             ares__buf_t *dest, size_t len);

/*! Fetch the requested number of bytes and return a new buffer that must be
 *  ares_free()'d by the caller.  The returned buffer is a null terminated
 *  string.
 *
 *  \param[in]  buf     Initialized buffer object
 *  \param[in]  len     Requested number of bytes (must be > 0)
 *  \param[out] str     Pointer passed by reference. Will be allocated.
 *  \return ARES_SUCCESS or one of the c-ares error codes
 */
ares_status_t ares__buf_fetch_str_dup(ares__buf_t *buf, size_t len, char **str);

/*! Consume whitespace characters (0x09, 0x0B, 0x0C, 0x0D, 0x20, and optionally
 *  0x0A).
 *
 *  \param[in]  buf               Initialized buffer object
 *  \param[in]  include_linefeed  ARES_TRUE to include consuming 0x0A,
 *                                ARES_FALSE otherwise.
 *  \return number of whitespace characters consumed
 */
size_t        ares__buf_consume_whitespace(ares__buf_t *buf,
                                           ares_bool_t  include_linefeed);


/*! Consume any non-whitespace character (anything other than 0x09, 0x0B, 0x0C,
 *  0x0D, 0x20, and 0x0A).
 *
 *  \param[in]  buf               Initialized buffer object
 *  \return number of characters consumed
 */
size_t        ares__buf_consume_nonwhitespace(ares__buf_t *buf);


/*! Consume until a character in the character set provided is reached.  Does
 *  not include the character from the charset at the end.
 *
 *  \param[in] buf                Initialized buffer object
 *  \param[in] charset            character set
 *  \param[in] len                length of character set
 *  \param[in] require_charset    require we find a character from the charset.
 *                                if ARES_FALSE it will simply consume the
 *                                rest of the buffer.  If ARES_TRUE will return
 *                                0 if not found.
 *  \return number of characters consumed
 */
size_t        ares__buf_consume_until_charset(ares__buf_t         *buf,
                                              const unsigned char *charset, size_t len,
                                              ares_bool_t require_charset);


/*! Consume while the characters match the characters in the provided set.
 *
 *  \param[in] buf                Initialized buffer object
 *  \param[in] charset            character set
 *  \param[in] len                length of character set
 *  \return number of characters consumed
 */
size_t ares__buf_consume_charset(ares__buf_t *buf, const unsigned char *charset,
                                 size_t len);


/*! Consume from the current position until the end of the line, and optionally
 *  the end of line character (0x0A) itself.
 *
 *  \param[in]  buf               Initialized buffer object
 *  \param[in]  include_linefeed  ARES_TRUE to include consuming 0x0A,
 *                                ARES_FALSE otherwise.
 *  \return number of characters consumed
 */
size_t ares__buf_consume_line(ares__buf_t *buf, ares_bool_t include_linefeed);

typedef enum {
  /*! No flags */
  ARES_BUF_SPLIT_NONE = 0,
  /*! The delimiter will be the first character in the buffer, except the
   *  first buffer since the start doesn't have a delimiter.  This option is
   *  incompatible with ARES_BUF_SPLIT_LTRIM since the delimiter is always
   *  the first character.
   */
  ARES_BUF_SPLIT_DONT_CONSUME_DELIMS = 1 << 0,
  /*! Allow blank sections, by default blank sections are not emitted.  If using
   *  ARES_BUF_SPLIT_DONT_CONSUME_DELIMS, the delimiter is not counted as part
   *  of the section */
  ARES_BUF_SPLIT_ALLOW_BLANK = 1 << 1,
  /*! Remove duplicate entries */
  ARES_BUF_SPLIT_NO_DUPLICATES = 1 << 2,
  /*! Perform case-insensitive matching when comparing values */
  ARES_BUF_SPLIT_CASE_INSENSITIVE = 1 << 3,
  /*! Trim leading whitespace from buffer */
  ARES_BUF_SPLIT_LTRIM = 1 << 4,
  /*! Trim trailing whitespace from buffer */
  ARES_BUF_SPLIT_RTRIM = 1 << 5,
  /*! Trim leading and trailing whitespace from buffer */
  ARES_BUF_SPLIT_TRIM = (ARES_BUF_SPLIT_LTRIM | ARES_BUF_SPLIT_RTRIM)
} ares__buf_split_t;

/*! Split the provided buffer into multiple sub-buffers stored in the variable
 *  pointed to by the linked list.  The sub buffers are const buffers pointing
 *  into the buf provided.
 *
 *  \param[in]  buf               Initialized buffer object
 *  \param[in]  delims            Possible delimiters
 *  \param[in]  delims_len        Length of possible delimiters
 *  \param[in]  flags             One more more flags
 *  \param[in]  max_sections      Maximum number of sections.  Use 0 for
 *                                unlimited. Useful for splitting key/value
 *                                pairs where the delimiter may be a valid
 *                                character in the value.  A value of 1 would
 *                                have little usefulness and would effectively
 *                                ignore the delimiter itself.
 *  \param[out] list              Result. Depending on flags, this may be a
 *                                valid list with no elements.  Use
 *                                ares__llist_destroy() to free the memory which
 *                                will also free the contained ares__buf_t
 *                                objects.
 *  \return ARES_SUCCESS on success, or error like ARES_ENOMEM.
 */
ares_status_t ares__buf_split(ares__buf_t *buf, const unsigned char *delims,
                              size_t delims_len, ares__buf_split_t flags,
                              size_t max_sections, ares__llist_t **list);


/*! Check the unprocessed buffer to see if it begins with the sequence of
 *  characters provided.
 *
 *  \param[in] buf          Initialized buffer object
 *  \param[in] data         Bytes of data to compare.
 *  \param[in] data_len     Length of data to compare.
 *  \return ARES_TRUE on match, ARES_FALSE otherwise.
 */
ares_bool_t   ares__buf_begins_with(const ares__buf_t   *buf,
                                    const unsigned char *data, size_t data_len);


/*! Size of unprocessed remaining data length
 *
 *  \param[in] buf Initialized buffer object
 *  \return length remaining
 */
size_t        ares__buf_len(const ares__buf_t *buf);

/*! Retrieve a pointer to the currently unprocessed data.  Generally this isn't
 *  recommended to be used in practice.  The returned pointer may be invalidated
 *  by any future ares__buf_*() calls.
 *
 *  \param[in]  buf    Initialized buffer object
 *  \param[out] len    Length of available data
 *  \return Pointer to buffer of unprocessed data
 */
const unsigned char *ares__buf_peek(const ares__buf_t *buf, size_t *len);


/*! Wipe any processed data from the beginning of the buffer.  This will
 *  move any remaining data to the front of the internally allocated buffer.
 *
 *  Can not be used on const buffer objects.
 *
 *  Typically not needed to call, as any new append operation will automatically
 *  call this function if there is insufficient space to append the data in
 *  order to try to avoid another memory allocation.
 *
 *  It may be useful to call in order to ensure the current message being
 *  processed is in the beginning of the buffer if there is an intent to use
 *  ares__buf_set_position() and ares__buf_get_position() as may be necessary
 *  when processing DNS compressed names.
 *
 *  If there is an active tag, it will NOT clear the tag, it will use the tag
 *  as the start of the unprocessed data rather than the current offset.  If
 *  a prior tag is no longer needed, may be wise to call ares__buf_tag_clear().
 *
 *  \param[in]  buf    Initialized buffer object
 */
void                 ares__buf_reclaim(ares__buf_t *buf);

/*! Set the current offset within the internal buffer.
 *
 *  Typically this should not be used, if possible, use the ares__buf_tag*()
 *  operations instead.
 *
 *  One exception is DNS name compression which may backwards reference to
 *  an index in the message.  It may be necessary in such a case to call
 *  ares__buf_reclaim() if using a dynamic (non-const) buffer before processing
 *  such a message.
 *
 *  \param[in] buf  Initialized buffer object
 *  \param[in] idx  Index to set position
 *  \return ARES_SUCCESS if valid index
 */
ares_status_t        ares__buf_set_position(ares__buf_t *buf, size_t idx);

/*! Get the current offset within the internal buffer.
 *
 *  Typically this should not be used, if possible, use the ares__buf_tag*()
 *  operations instead.
 *
 *  This can be used to get the current position, useful for saving if a
 *  jump via ares__buf_set_position() is performed and need to restore the
 *  current position for future operations.
 *
 *  \param[in] buf Initialized buffer object
 *  \return index of current position
 */
size_t               ares__buf_get_position(const ares__buf_t *buf);

/*! Parse a character-string as defined in RFC1035, as a null-terminated
 *  string.
 *
 *  \param[in]  buf            initialized buffer object
 *  \param[in]  remaining_len  maximum length that should be used for parsing
 *                             the string, this is often less than the remaining
 *                             buffer and is based on the RR record length.
 *  \param[out] name           Pointer passed by reference to be filled in with
 *                             allocated string of the parsed that must be
 *                             ares_free()'d by the caller.
 *  \return ARES_SUCCESS on success
 */
ares_status_t ares__buf_parse_dns_str(ares__buf_t *buf, size_t remaining_len,
                                      char **name);

/*! Parse an array of character strings as defined in RFC1035, as binary,
 *  however, for convenience this does guarantee a NULL terminator (that is
 *  not included in the length for each value).
 *
 *  \param[in]  buf                initialized buffer object
 *  \param[in]  remaining_len      maximum length that should be used for
 *                                 parsing the string, this is often less than
 *                                 the remaining buffer and is based on the RR
 *                                 record length.
 *  \param[out] strs               Pointer passed by reference to be filled in
 *                                 with
 *                                 the array of values.
 *  \param[out] validate_printable Validate the strings contain only printable
 *                                 data.
 *  \return ARES_SUCCESS on success
 */
ares_status_t ares__buf_parse_dns_abinstr(ares__buf_t *buf,
                                          size_t       remaining_len,
                                          ares__dns_multistring_t **strs,
                                          ares_bool_t validate_printable);

/*! Parse a character-string as defined in RFC1035, as binary, however for
 *  convenience this does guarantee a NULL terminator (that is not included
 *  in the returned length).
 *
 *  \param[in]  buf            initialized buffer object
 *  \param[in]  remaining_len  maximum length that should be used for parsing
 *                             the string, this is often less than the remaining
 *                             buffer and is based on the RR record length.
 *  \param[out] bin            Pointer passed by reference to be filled in with
 *                             allocated string of the parsed that must be
 *                             ares_free()'d by the caller.
 *  \param[out] bin_len        Length of returned string.
 *  \return ARES_SUCCESS on success
 */
ares_status_t ares__buf_parse_dns_binstr(ares__buf_t *buf, size_t remaining_len,
                                         unsigned char **bin, size_t *bin_len);

/*! Load data from specified file path into provided buffer.  The entire file
 *  is loaded into memory.
 *
 *  \param[in]     filename complete path to file
 *  \param[in,out] buf      Initialized (non-const) buffer object to load data
 *                          into
 *  \return ARES_ENOTFOUND if file not found, ARES_EFILE if issues reading
 *          file, ARES_ENOMEM if out of memory, ARES_SUCCESS on success.
 */
ares_status_t ares__buf_load_file(const char *filename, ares__buf_t *buf);

/*! @} */

#endif /* __ARES__BUF_H */
