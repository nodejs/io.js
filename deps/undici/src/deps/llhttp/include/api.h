#ifndef INCLUDE_LLHTTP_API_H_
#define INCLUDE_LLHTTP_API_H_
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>

#if defined(__wasm__)
#define LLHTTP_EXPORT __attribute__((visibility("default")))
#elif defined(_WIN32)
#define LLHTTP_EXPORT __declspec(dllexport)
#else
#define LLHTTP_EXPORT
#endif

typedef llhttp__internal_t llhttp_t;
typedef struct llhttp_settings_s llhttp_settings_t;

typedef int (*llhttp_data_cb)(llhttp_t*, const char *at, size_t length);
typedef int (*llhttp_cb)(llhttp_t*);

struct llhttp_settings_s {
  /* Possible return values 0, -1, `HPE_PAUSED` */
  llhttp_cb      on_message_begin;

  /* Possible return values 0, -1, HPE_USER */
  llhttp_data_cb on_url;
  llhttp_data_cb on_status;
  llhttp_data_cb on_method;
  llhttp_data_cb on_version;
  llhttp_data_cb on_header_field;
  llhttp_data_cb on_header_value;
  llhttp_data_cb      on_chunk_extension_name;
  llhttp_data_cb      on_chunk_extension_value;

  /* Possible return values:
   * 0  - Proceed normally
   * 1  - Assume that request/response has no body, and proceed to parsing the
   *      next message
   * 2  - Assume absence of body (as above) and make `llhttp_execute()` return
   *      `HPE_PAUSED_UPGRADE`
   * -1 - Error
   * `HPE_PAUSED`
   */
  llhttp_cb      on_headers_complete;

  /* Possible return values 0, -1, HPE_USER */
  llhttp_data_cb on_body;

  /* Possible return values 0, -1, `HPE_PAUSED` */
  llhttp_cb      on_message_complete;
  llhttp_cb      on_url_complete;
  llhttp_cb      on_status_complete;
  llhttp_cb      on_method_complete;
  llhttp_cb      on_version_complete;
  llhttp_cb      on_header_field_complete;
  llhttp_cb      on_header_value_complete;
  llhttp_cb      on_chunk_extension_name_complete;
  llhttp_cb      on_chunk_extension_value_complete;

  /* When on_chunk_header is called, the current chunk length is stored
   * in parser->content_length.
   * Possible return values 0, -1, `HPE_PAUSED`
   */
  llhttp_cb      on_chunk_header;
  llhttp_cb      on_chunk_complete;
  llhttp_cb      on_reset;
};

/* Initialize the parser with specific type and user settings.
 *
 * NOTE: lifetime of `settings` has to be at least the same as the lifetime of
 * the `parser` here. In practice, `settings` has to be either a static
 * variable or be allocated with `malloc`, `new`, etc.
 */
LLHTTP_EXPORT
void llhttp_init(llhttp_t* parser, llhttp_type_t type,
                 const llhttp_settings_t* settings);

LLHTTP_EXPORT
llhttp_t* llhttp_alloc(llhttp_type_t type);

LLHTTP_EXPORT
void llhttp_free(llhttp_t* parser);

LLHTTP_EXPORT
uint8_t llhttp_get_type(llhttp_t* parser);

LLHTTP_EXPORT
uint8_t llhttp_get_http_major(llhttp_t* parser);

LLHTTP_EXPORT
uint8_t llhttp_get_http_minor(llhttp_t* parser);

LLHTTP_EXPORT
uint8_t llhttp_get_method(llhttp_t* parser);

LLHTTP_EXPORT
int llhttp_get_status_code(llhttp_t* parser);

LLHTTP_EXPORT
uint8_t llhttp_get_upgrade(llhttp_t* parser);

/* Reset an already initialized parser back to the start state, preserving the
 * existing parser type, callback settings, user data, and lenient flags.
 */
LLHTTP_EXPORT
void llhttp_reset(llhttp_t* parser);

/* Initialize the settings object */
LLHTTP_EXPORT
void llhttp_settings_init(llhttp_settings_t* settings);

/* Parse full or partial request/response, invoking user callbacks along the
 * way.
 *
 * If any of `llhttp_data_cb` returns errno not equal to `HPE_OK` - the parsing
 * interrupts, and such errno is returned from `llhttp_execute()`. If
 * `HPE_PAUSED` was used as a errno, the execution can be resumed with
 * `llhttp_resume()` call.
 *
 * In a special case of CONNECT/Upgrade request/response `HPE_PAUSED_UPGRADE`
 * is returned after fully parsing the request/response. If the user wishes to
 * continue parsing, they need to invoke `llhttp_resume_after_upgrade()`.
 *
 * NOTE: if this function ever returns a non-pause type error, it will continue
 * to return the same error upon each successive call up until `llhttp_init()`
 * is called.
 */
LLHTTP_EXPORT
llhttp_errno_t llhttp_execute(llhttp_t* parser, const char* data, size_t len);

/* This method should be called when the other side has no further bytes to
 * send (e.g. shutdown of readable side of the TCP connection.)
 *
 * Requests without `Content-Length` and other messages might require treating
 * all incoming bytes as the part of the body, up to the last byte of the
 * connection. This method will invoke `on_message_complete()` callback if the
 * request was terminated safely. Otherwise a error code would be returned.
 */
LLHTTP_EXPORT
llhttp_errno_t llhttp_finish(llhttp_t* parser);

/* Returns `1` if the incoming message is parsed until the last byte, and has
 * to be completed by calling `llhttp_finish()` on EOF
 */
LLHTTP_EXPORT
int llhttp_message_needs_eof(const llhttp_t* parser);

/* Returns `1` if there might be any other messages following the last that was
 * successfully parsed.
 */
LLHTTP_EXPORT
int llhttp_should_keep_alive(const llhttp_t* parser);

/* Make further calls of `llhttp_execute()` return `HPE_PAUSED` and set
 * appropriate error reason.
 *
 * Important: do not call this from user callbacks! User callbacks must return
 * `HPE_PAUSED` if pausing is required.
 */
LLHTTP_EXPORT
void llhttp_pause(llhttp_t* parser);

/* Might be called to resume the execution after the pause in user's callback.
 * See `llhttp_execute()` above for details.
 *
 * Call this only if `llhttp_execute()` returns `HPE_PAUSED`.
 */
LLHTTP_EXPORT
void llhttp_resume(llhttp_t* parser);

/* Might be called to resume the execution after the pause in user's callback.
 * See `llhttp_execute()` above for details.
 *
 * Call this only if `llhttp_execute()` returns `HPE_PAUSED_UPGRADE`
 */
LLHTTP_EXPORT
void llhttp_resume_after_upgrade(llhttp_t* parser);

/* Returns the latest return error */
LLHTTP_EXPORT
llhttp_errno_t llhttp_get_errno(const llhttp_t* parser);

/* Returns the verbal explanation of the latest returned error.
 *
 * Note: User callback should set error reason when returning the error. See
 * `llhttp_set_error_reason()` for details.
 */
LLHTTP_EXPORT
const char* llhttp_get_error_reason(const llhttp_t* parser);

/* Assign verbal description to the returned error. Must be called in user
 * callbacks right before returning the errno.
 *
 * Note: `HPE_USER` error code might be useful in user callbacks.
 */
LLHTTP_EXPORT
void llhttp_set_error_reason(llhttp_t* parser, const char* reason);

/* Returns the pointer to the last parsed byte before the returned error. The
 * pointer is relative to the `data` argument of `llhttp_execute()`.
 *
 * Note: this method might be useful for counting the number of parsed bytes.
 */
LLHTTP_EXPORT
const char* llhttp_get_error_pos(const llhttp_t* parser);

/* Returns textual name of error code */
LLHTTP_EXPORT
const char* llhttp_errno_name(llhttp_errno_t err);

/* Returns textual name of HTTP method */
LLHTTP_EXPORT
const char* llhttp_method_name(llhttp_method_t method);

/* Returns textual name of HTTP status */
LLHTTP_EXPORT
const char* llhttp_status_name(llhttp_status_t status);

/* Enables/disables lenient header value parsing (disabled by default).
 *
 * Lenient parsing disables header value token checks, extending llhttp's
 * protocol support to highly non-compliant clients/server. No
 * `HPE_INVALID_HEADER_TOKEN` will be raised for incorrect header values when
 * lenient parsing is "on".
 *
 * **Enabling this flag can pose a security issue since you will be exposed to
 * request smuggling attacks. USE WITH CAUTION!**
 */
LLHTTP_EXPORT
void llhttp_set_lenient_headers(llhttp_t* parser, int enabled);


/* Enables/disables lenient handling of conflicting `Transfer-Encoding` and
 * `Content-Length` headers (disabled by default).
 *
 * Normally `llhttp` would error when `Transfer-Encoding` is present in
 * conjunction with `Content-Length`. This error is important to prevent HTTP
 * request smuggling, but may be less desirable for small number of cases
 * involving legacy servers.
 *
 * **Enabling this flag can pose a security issue since you will be exposed to
 * request smuggling attacks. USE WITH CAUTION!**
 */
LLHTTP_EXPORT
void llhttp_set_lenient_chunked_length(llhttp_t* parser, int enabled);


/* Enables/disables lenient handling of `Connection: close` and HTTP/1.0
 * requests responses.
 *
 * Normally `llhttp` would error on (in strict mode) or discard (in loose mode)
 * the HTTP request/response after the request/response with `Connection: close`
 * and `Content-Length`. This is important to prevent cache poisoning attacks,
 * but might interact badly with outdated and insecure clients. With this flag
 * the extra request/response will be parsed normally.
 *
 * **Enabling this flag can pose a security issue since you will be exposed to
 * poisoning attacks. USE WITH CAUTION!**
 */
LLHTTP_EXPORT
void llhttp_set_lenient_keep_alive(llhttp_t* parser, int enabled);

/* Enables/disables lenient handling of `Transfer-Encoding` header.
 *
 * Normally `llhttp` would error when a `Transfer-Encoding` has `chunked` value
 * and another value after it (either in a single header or in multiple
 * headers whose value are internally joined using `, `).
 * This is mandated by the spec to reliably determine request body size and thus
 * avoid request smuggling.
 * With this flag the extra value will be parsed normally.
 *
 * **Enabling this flag can pose a security issue since you will be exposed to
 * request smuggling attacks. USE WITH CAUTION!**
 */
LLHTTP_EXPORT
void llhttp_set_lenient_transfer_encoding(llhttp_t* parser, int enabled);

/* Enables/disables lenient handling of HTTP version.
 *
 * Normally `llhttp` would error when the HTTP version in the request or status line
 * is not `0.9`, `1.0`, `1.1` or `2.0`.
 * With this flag the invalid value will be parsed normally.
 *
 * **Enabling this flag can pose a security issue since you will allow unsupported
 * HTTP versions. USE WITH CAUTION!**
 */
LLHTTP_EXPORT
void llhttp_set_lenient_version(llhttp_t* parser, int enabled);

/* Enables/disables lenient handling of additional data received after a message ends
 * and keep-alive is disabled.
 *
 * Normally `llhttp` would error when additional unexpected data is received if the message
 * contains the `Connection` header with `close` value.
 * With this flag the extra data will discarded without throwing an error.
 *
 * **Enabling this flag can pose a security issue since you will be exposed to
 * poisoning attacks. USE WITH CAUTION!**
 */
LLHTTP_EXPORT
void llhttp_set_lenient_data_after_close(llhttp_t* parser, int enabled);

/* Enables/disables lenient handling of incomplete CRLF sequences.
 *
 * Normally `llhttp` would error when a CR is not followed by LF when terminating the
 * request line, the status line, the headers or a chunk header.
 * With this flag only a CR is required to terminate such sections.
 *
 * **Enabling this flag can pose a security issue since you will be exposed to
 * request smuggling attacks. USE WITH CAUTION!**
 */
LLHTTP_EXPORT
void llhttp_set_lenient_optional_lf_after_cr(llhttp_t* parser, int enabled);

/*
 * Enables/disables lenient handling of line separators.
 *
 * Normally `llhttp` would error when a LF is not preceded by CR when terminating the
 * request line, the status line, the headers, a chunk header or a chunk data.
 * With this flag only a LF is required to terminate such sections.
 *
 * **Enabling this flag can pose a security issue since you will be exposed to
 * request smuggling attacks. USE WITH CAUTION!**
 */
LLHTTP_EXPORT
void llhttp_set_lenient_optional_cr_before_lf(llhttp_t* parser, int enabled);

/* Enables/disables lenient handling of chunks not separated via CRLF.
 *
 * Normally `llhttp` would error when after a chunk data a CRLF is missing before
 * starting a new chunk.
 * With this flag the new chunk can start immediately after the previous one.
 *
 * **Enabling this flag can pose a security issue since you will be exposed to
 * request smuggling attacks. USE WITH CAUTION!**
 */
LLHTTP_EXPORT
void llhttp_set_lenient_optional_crlf_after_chunk(llhttp_t* parser, int enabled);

/* Enables/disables lenient handling of spaces after chunk size.
 *
 * Normally `llhttp` would error when after a chunk size is followed by one or more
 * spaces are present instead of a CRLF or `;`.
 * With this flag this check is disabled.
 *
 * **Enabling this flag can pose a security issue since you will be exposed to
 * request smuggling attacks. USE WITH CAUTION!**
 */
LLHTTP_EXPORT
void llhttp_set_lenient_spaces_after_chunk_size(llhttp_t* parser, int enabled);

#ifdef __cplusplus
}  /* extern "C" */
#endif
#endif  /* INCLUDE_LLHTTP_API_H_ */
