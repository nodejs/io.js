/**
 * @file ada_c.h
 * @brief Includes the C definitions for Ada. This is a C file, not C++.
 */
#ifndef ADA_C_H
#define ADA_C_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// This is a reference to ada::url_components::omitted
// It represents "uint32_t(-1)"
#define ada_url_omitted 0xffffffff

// string that is owned by the ada_url instance
typedef struct {
  const char* data;
  size_t length;
} ada_string;

// string that must be freed by the caller
typedef struct {
  const char* data;
  size_t length;
} ada_owned_string;

typedef struct {
  uint32_t protocol_end;
  uint32_t username_end;
  uint32_t host_start;
  uint32_t host_end;
  uint32_t port;
  uint32_t pathname_start;
  uint32_t search_start;
  uint32_t hash_start;
} ada_url_components;

typedef void* ada_url;

// input should be a null terminated C string (ASCII or UTF-8)
// you must call ada_free on the returned pointer
ada_url ada_parse(const char* input, size_t length);
ada_url ada_parse_with_base(const char* input, size_t input_length,
                            const char* base, size_t base_length);

// input and base should be a null terminated C strings
bool ada_can_parse(const char* input, size_t length);
bool ada_can_parse_with_base(const char* input, size_t input_length,
                             const char* base, size_t base_length);

void ada_free(ada_url result);
void ada_free_owned_string(ada_owned_string owned);
ada_url ada_copy(ada_url input);

bool ada_is_valid(ada_url result);

// url_aggregator getters
// if ada_is_valid(result)) is false, an empty string is returned
ada_owned_string ada_get_origin(ada_url result);
ada_string ada_get_href(ada_url result);
ada_string ada_get_username(ada_url result);
ada_string ada_get_password(ada_url result);
ada_string ada_get_port(ada_url result);
ada_string ada_get_hash(ada_url result);
ada_string ada_get_host(ada_url result);
ada_string ada_get_hostname(ada_url result);
ada_string ada_get_pathname(ada_url result);
ada_string ada_get_search(ada_url result);
ada_string ada_get_protocol(ada_url result);
uint8_t ada_get_host_type(ada_url result);
uint8_t ada_get_scheme_type(ada_url result);

// url_aggregator setters
// if ada_is_valid(result)) is false, the setters have no effect
// input should be a null terminated C string
bool ada_set_href(ada_url result, const char* input, size_t length);
bool ada_set_host(ada_url result, const char* input, size_t length);
bool ada_set_hostname(ada_url result, const char* input, size_t length);
bool ada_set_protocol(ada_url result, const char* input, size_t length);
bool ada_set_username(ada_url result, const char* input, size_t length);
bool ada_set_password(ada_url result, const char* input, size_t length);
bool ada_set_port(ada_url result, const char* input, size_t length);
bool ada_set_pathname(ada_url result, const char* input, size_t length);
void ada_set_search(ada_url result, const char* input, size_t length);
void ada_set_hash(ada_url result, const char* input, size_t length);

// url_aggregator clear methods
void ada_clear_port(ada_url result);
void ada_clear_hash(ada_url result);
void ada_clear_search(ada_url result);

// url_aggregator functions
// if ada_is_valid(result) is false, functions below will return false
bool ada_has_credentials(ada_url result);
bool ada_has_empty_hostname(ada_url result);
bool ada_has_hostname(ada_url result);
bool ada_has_non_empty_username(ada_url result);
bool ada_has_non_empty_password(ada_url result);
bool ada_has_port(ada_url result);
bool ada_has_password(ada_url result);
bool ada_has_hash(ada_url result);
bool ada_has_search(ada_url result);

// returns a pointer to the internal url_aggregator::url_components
const ada_url_components* ada_get_components(ada_url result);

// idna methods
ada_owned_string ada_idna_to_unicode(const char* input, size_t length);
ada_owned_string ada_idna_to_ascii(const char* input, size_t length);

// url search params
typedef void* ada_url_search_params;

// Represents an std::vector<std::string>
typedef void* ada_strings;
typedef void* ada_url_search_params_keys_iter;
typedef void* ada_url_search_params_values_iter;

typedef struct {
  ada_string key;
  ada_string value;
} ada_string_pair;

typedef void* ada_url_search_params_entries_iter;

ada_url_search_params ada_parse_search_params(const char* input, size_t length);
void ada_free_search_params(ada_url_search_params result);

size_t ada_search_params_size(ada_url_search_params result);
void ada_search_params_sort(ada_url_search_params result);
ada_owned_string ada_search_params_to_string(ada_url_search_params result);

void ada_search_params_append(ada_url_search_params result, const char* key,
                              size_t key_length, const char* value,
                              size_t value_length);
void ada_search_params_set(ada_url_search_params result, const char* key,
                           size_t key_length, const char* value,
                           size_t value_length);
void ada_search_params_remove(ada_url_search_params result, const char* key,
                              size_t key_length);
void ada_search_params_remove_value(ada_url_search_params result,
                                    const char* key, size_t key_length,
                                    const char* value, size_t value_length);
bool ada_search_params_has(ada_url_search_params result, const char* key,
                           size_t key_length);
bool ada_search_params_has_value(ada_url_search_params result, const char* key,
                                 size_t key_length, const char* value,
                                 size_t value_length);
ada_string ada_search_params_get(ada_url_search_params result, const char* key,
                                 size_t key_length);
ada_strings ada_search_params_get_all(ada_url_search_params result,
                                      const char* key, size_t key_length);
void ada_search_params_reset(ada_url_search_params result, const char* input,
                             size_t length);
ada_url_search_params_keys_iter ada_search_params_get_keys(
    ada_url_search_params result);
ada_url_search_params_values_iter ada_search_params_get_values(
    ada_url_search_params result);
ada_url_search_params_entries_iter ada_search_params_get_entries(
    ada_url_search_params result);

void ada_free_strings(ada_strings result);
size_t ada_strings_size(ada_strings result);
ada_string ada_strings_get(ada_strings result, size_t index);

void ada_free_search_params_keys_iter(ada_url_search_params_keys_iter result);
ada_string ada_search_params_keys_iter_next(
    ada_url_search_params_keys_iter result);
bool ada_search_params_keys_iter_has_next(
    ada_url_search_params_keys_iter result);

void ada_free_search_params_values_iter(
    ada_url_search_params_values_iter result);
ada_string ada_search_params_values_iter_next(
    ada_url_search_params_values_iter result);
bool ada_search_params_values_iter_has_next(
    ada_url_search_params_values_iter result);

void ada_free_search_params_entries_iter(
    ada_url_search_params_entries_iter result);
ada_string_pair ada_search_params_entries_iter_next(
    ada_url_search_params_entries_iter result);
bool ada_search_params_entries_iter_has_next(
    ada_url_search_params_entries_iter result);

#endif  // ADA_C_H
