#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "llhttp.h"

#define CALLBACK_MAYBE(PARSER, NAME)                                          \
  do {                                                                        \
    const llhttp_settings_t* settings;                                        \
    settings = (const llhttp_settings_t*) (PARSER)->settings;                 \
    if (settings == NULL || settings->NAME == NULL) {                         \
      err = 0;                                                                \
      break;                                                                  \
    }                                                                         \
    err = settings->NAME((PARSER));                                           \
  } while (0)

#define SPAN_CALLBACK_MAYBE(PARSER, NAME, START, LEN)                         \
  do {                                                                        \
    const llhttp_settings_t* settings;                                        \
    settings = (const llhttp_settings_t*) (PARSER)->settings;                 \
    if (settings == NULL || settings->NAME == NULL) {                         \
      err = 0;                                                                \
      break;                                                                  \
    }                                                                         \
    err = settings->NAME((PARSER), (START), (LEN));                           \
    if (err == -1) {                                                          \
      err = HPE_USER;                                                         \
      llhttp_set_error_reason((PARSER), "Span callback error in " #NAME);     \
    }                                                                         \
  } while (0)

void llhttp_init(llhttp_t* parser, llhttp_type_t type,
                 const llhttp_settings_t* settings) {
  llhttp__internal_init(parser);

  parser->type = type;
  parser->settings = (void*) settings;
}


#if defined(__wasm__)

extern int wasm_on_message_begin(llhttp_t * p);
extern int wasm_on_url(llhttp_t* p, const char* at, size_t length);
extern int wasm_on_status(llhttp_t* p, const char* at, size_t length);
extern int wasm_on_header_field(llhttp_t* p, const char* at, size_t length);
extern int wasm_on_header_value(llhttp_t* p, const char* at, size_t length);
extern int wasm_on_headers_complete(llhttp_t * p, int status_code,
                                    uint8_t upgrade, int should_keep_alive);
extern int wasm_on_body(llhttp_t* p, const char* at, size_t length);
extern int wasm_on_message_complete(llhttp_t * p);

static int wasm_on_headers_complete_wrap(llhttp_t* p) {
  return wasm_on_headers_complete(p, p->status_code, p->upgrade,
                                  llhttp_should_keep_alive(p));
}

const llhttp_settings_t wasm_settings = {
  wasm_on_message_begin,
  wasm_on_url,
  wasm_on_status,
  NULL,
  NULL,
  wasm_on_header_field,
  wasm_on_header_value,
  NULL,
  NULL,
  wasm_on_headers_complete_wrap,
  wasm_on_body,
  wasm_on_message_complete,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
};


llhttp_t* llhttp_alloc(llhttp_type_t type) {
  llhttp_t* parser = malloc(sizeof(llhttp_t));
  llhttp_init(parser, type, &wasm_settings);
  return parser;
}

void llhttp_free(llhttp_t* parser) {
  free(parser);
}

#endif  // defined(__wasm__)

/* Some getters required to get stuff from the parser */

uint8_t llhttp_get_type(llhttp_t* parser) {
  return parser->type;
}

uint8_t llhttp_get_http_major(llhttp_t* parser) {
  return parser->http_major;
}

uint8_t llhttp_get_http_minor(llhttp_t* parser) {
  return parser->http_minor;
}

uint8_t llhttp_get_method(llhttp_t* parser) {
  return parser->method;
}

int llhttp_get_status_code(llhttp_t* parser) {
  return parser->status_code;
}

uint8_t llhttp_get_upgrade(llhttp_t* parser) {
  return parser->upgrade;
}


void llhttp_reset(llhttp_t* parser) {
  llhttp_type_t type = parser->type;
  const llhttp_settings_t* settings = parser->settings;
  void* data = parser->data;
  uint16_t lenient_flags = parser->lenient_flags;

  llhttp__internal_init(parser);

  parser->type = type;
  parser->settings = (void*) settings;
  parser->data = data;
  parser->lenient_flags = lenient_flags;
}


llhttp_errno_t llhttp_execute(llhttp_t* parser, const char* data, size_t len) {
  return llhttp__internal_execute(parser, data, data + len);
}


void llhttp_settings_init(llhttp_settings_t* settings) {
  memset(settings, 0, sizeof(*settings));
}


llhttp_errno_t llhttp_finish(llhttp_t* parser) {
  int err;

  /* We're in an error state. Don't bother doing anything. */
  if (parser->error != 0) {
    return 0;
  }

  switch (parser->finish) {
    case HTTP_FINISH_SAFE_WITH_CB:
      CALLBACK_MAYBE(parser, on_message_complete);
      if (err != HPE_OK) return err;

    /* FALLTHROUGH */
    case HTTP_FINISH_SAFE:
      return HPE_OK;
    case HTTP_FINISH_UNSAFE:
      parser->reason = "Invalid EOF state";
      return HPE_INVALID_EOF_STATE;
    default:
      abort();
  }
}


void llhttp_pause(llhttp_t* parser) {
  if (parser->error != HPE_OK) {
    return;
  }

  parser->error = HPE_PAUSED;
  parser->reason = "Paused";
}


void llhttp_resume(llhttp_t* parser) {
  if (parser->error != HPE_PAUSED) {
    return;
  }

  parser->error = 0;
}


void llhttp_resume_after_upgrade(llhttp_t* parser) {
  if (parser->error != HPE_PAUSED_UPGRADE) {
    return;
  }

  parser->error = 0;
}


llhttp_errno_t llhttp_get_errno(const llhttp_t* parser) {
  return parser->error;
}


const char* llhttp_get_error_reason(const llhttp_t* parser) {
  return parser->reason;
}


void llhttp_set_error_reason(llhttp_t* parser, const char* reason) {
  parser->reason = reason;
}


const char* llhttp_get_error_pos(const llhttp_t* parser) {
  return parser->error_pos;
}


const char* llhttp_errno_name(llhttp_errno_t err) {
#define HTTP_ERRNO_GEN(CODE, NAME, _) case HPE_##NAME: return "HPE_" #NAME;
  switch (err) {
    HTTP_ERRNO_MAP(HTTP_ERRNO_GEN)
    default: abort();
  }
#undef HTTP_ERRNO_GEN
}


const char* llhttp_method_name(llhttp_method_t method) {
#define HTTP_METHOD_GEN(NUM, NAME, STRING) case HTTP_##NAME: return #STRING;
  switch (method) {
    HTTP_ALL_METHOD_MAP(HTTP_METHOD_GEN)
    default: abort();
  }
#undef HTTP_METHOD_GEN
}

const char* llhttp_status_name(llhttp_status_t status) {
#define HTTP_STATUS_GEN(NUM, NAME, STRING) case HTTP_STATUS_##NAME: return #STRING;
  switch (status) {
    HTTP_STATUS_MAP(HTTP_STATUS_GEN)
    default: abort();
  }
#undef HTTP_STATUS_GEN
}


void llhttp_set_lenient_headers(llhttp_t* parser, int enabled) {
  if (enabled) {
    parser->lenient_flags |= LENIENT_HEADERS;
  } else {
    parser->lenient_flags &= ~LENIENT_HEADERS;
  }
}


void llhttp_set_lenient_chunked_length(llhttp_t* parser, int enabled) {
  if (enabled) {
    parser->lenient_flags |= LENIENT_CHUNKED_LENGTH;
  } else {
    parser->lenient_flags &= ~LENIENT_CHUNKED_LENGTH;
  }
}


void llhttp_set_lenient_keep_alive(llhttp_t* parser, int enabled) {
  if (enabled) {
    parser->lenient_flags |= LENIENT_KEEP_ALIVE;
  } else {
    parser->lenient_flags &= ~LENIENT_KEEP_ALIVE;
  }
}

void llhttp_set_lenient_transfer_encoding(llhttp_t* parser, int enabled) {
  if (enabled) {
    parser->lenient_flags |= LENIENT_TRANSFER_ENCODING;
  } else {
    parser->lenient_flags &= ~LENIENT_TRANSFER_ENCODING;
  }
}

void llhttp_set_lenient_version(llhttp_t* parser, int enabled) {
  if (enabled) {
    parser->lenient_flags |= LENIENT_VERSION;
  } else {
    parser->lenient_flags &= ~LENIENT_VERSION;
  }
}

void llhttp_set_lenient_data_after_close(llhttp_t* parser, int enabled) {
  if (enabled) {
    parser->lenient_flags |= LENIENT_DATA_AFTER_CLOSE;
  } else {
    parser->lenient_flags &= ~LENIENT_DATA_AFTER_CLOSE;
  }
}

void llhttp_set_lenient_optional_lf_after_cr(llhttp_t* parser, int enabled) {
  if (enabled) {
    parser->lenient_flags |= LENIENT_OPTIONAL_LF_AFTER_CR;
  } else {
    parser->lenient_flags &= ~LENIENT_OPTIONAL_LF_AFTER_CR;
  }
}

void llhttp_set_lenient_optional_crlf_after_chunk(llhttp_t* parser, int enabled) {
  if (enabled) {
    parser->lenient_flags |= LENIENT_OPTIONAL_CRLF_AFTER_CHUNK;
  } else {
    parser->lenient_flags &= ~LENIENT_OPTIONAL_CRLF_AFTER_CHUNK;
  }
}

void llhttp_set_lenient_optional_cr_before_lf(llhttp_t* parser, int enabled) {
  if (enabled) {
    parser->lenient_flags |= LENIENT_OPTIONAL_CR_BEFORE_LF;
  } else {
    parser->lenient_flags &= ~LENIENT_OPTIONAL_CR_BEFORE_LF;
  }
}

void llhttp_set_lenient_spaces_after_chunk_size(llhttp_t* parser, int enabled) {
  if (enabled) {
    parser->lenient_flags |= LENIENT_SPACES_AFTER_CHUNK_SIZE;
  } else {
    parser->lenient_flags &= ~LENIENT_SPACES_AFTER_CHUNK_SIZE;
  }
}

/* Callbacks */


int llhttp__on_message_begin(llhttp_t* s, const char* p, const char* endp) {
  int err;
  CALLBACK_MAYBE(s, on_message_begin);
  return err;
}


int llhttp__on_url(llhttp_t* s, const char* p, const char* endp) {
  int err;
  SPAN_CALLBACK_MAYBE(s, on_url, p, endp - p);
  return err;
}


int llhttp__on_url_complete(llhttp_t* s, const char* p, const char* endp) {
  int err;
  CALLBACK_MAYBE(s, on_url_complete);
  return err;
}


int llhttp__on_status(llhttp_t* s, const char* p, const char* endp) {
  int err;
  SPAN_CALLBACK_MAYBE(s, on_status, p, endp - p);
  return err;
}


int llhttp__on_status_complete(llhttp_t* s, const char* p, const char* endp) {
  int err;
  CALLBACK_MAYBE(s, on_status_complete);
  return err;
}


int llhttp__on_method(llhttp_t* s, const char* p, const char* endp) {
  int err;
  SPAN_CALLBACK_MAYBE(s, on_method, p, endp - p);
  return err;
}


int llhttp__on_method_complete(llhttp_t* s, const char* p, const char* endp) {
  int err;
  CALLBACK_MAYBE(s, on_method_complete);
  return err;
}


int llhttp__on_version(llhttp_t* s, const char* p, const char* endp) {
  int err;
  SPAN_CALLBACK_MAYBE(s, on_version, p, endp - p);
  return err;
}


int llhttp__on_version_complete(llhttp_t* s, const char* p, const char* endp) {
  int err;
  CALLBACK_MAYBE(s, on_version_complete);
  return err;
}


int llhttp__on_header_field(llhttp_t* s, const char* p, const char* endp) {
  int err;
  SPAN_CALLBACK_MAYBE(s, on_header_field, p, endp - p);
  return err;
}


int llhttp__on_header_field_complete(llhttp_t* s, const char* p, const char* endp) {
  int err;
  CALLBACK_MAYBE(s, on_header_field_complete);
  return err;
}


int llhttp__on_header_value(llhttp_t* s, const char* p, const char* endp) {
  int err;
  SPAN_CALLBACK_MAYBE(s, on_header_value, p, endp - p);
  return err;
}


int llhttp__on_header_value_complete(llhttp_t* s, const char* p, const char* endp) {
  int err;
  CALLBACK_MAYBE(s, on_header_value_complete);
  return err;
}


int llhttp__on_headers_complete(llhttp_t* s, const char* p, const char* endp) {
  int err;
  CALLBACK_MAYBE(s, on_headers_complete);
  return err;
}


int llhttp__on_message_complete(llhttp_t* s, const char* p, const char* endp) {
  int err;
  CALLBACK_MAYBE(s, on_message_complete);
  return err;
}


int llhttp__on_body(llhttp_t* s, const char* p, const char* endp) {
  int err;
  SPAN_CALLBACK_MAYBE(s, on_body, p, endp - p);
  return err;
}


int llhttp__on_chunk_header(llhttp_t* s, const char* p, const char* endp) {
  int err;
  CALLBACK_MAYBE(s, on_chunk_header);
  return err;
}


int llhttp__on_chunk_extension_name(llhttp_t* s, const char* p, const char* endp) {
  int err;
  SPAN_CALLBACK_MAYBE(s, on_chunk_extension_name, p, endp - p);
  return err;
}


int llhttp__on_chunk_extension_name_complete(llhttp_t* s, const char* p, const char* endp) {
  int err;
  CALLBACK_MAYBE(s, on_chunk_extension_name_complete);
  return err;
}


int llhttp__on_chunk_extension_value(llhttp_t* s, const char* p, const char* endp) {
  int err;
  SPAN_CALLBACK_MAYBE(s, on_chunk_extension_value, p, endp - p);
  return err;
}


int llhttp__on_chunk_extension_value_complete(llhttp_t* s, const char* p, const char* endp) {
  int err;
  CALLBACK_MAYBE(s, on_chunk_extension_value_complete);
  return err;
}


int llhttp__on_chunk_complete(llhttp_t* s, const char* p, const char* endp) {
  int err;
  CALLBACK_MAYBE(s, on_chunk_complete);
  return err;
}


int llhttp__on_reset(llhttp_t* s, const char* p, const char* endp) {
  int err;
  CALLBACK_MAYBE(s, on_reset);
  return err;
}


/* Private */


void llhttp__debug(llhttp_t* s, const char* p, const char* endp,
                   const char* msg) {
  if (p == endp) {
    fprintf(stderr, "p=%p type=%d flags=%02x next=null debug=%s\n", s, s->type,
            s->flags, msg);
  } else {
    fprintf(stderr, "p=%p type=%d flags=%02x next=%02x   debug=%s\n", s,
            s->type, s->flags, *p, msg);
  }
}
