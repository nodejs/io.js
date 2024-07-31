/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#include "static_dict.h"

#include "../common/dictionary.h"
#include "../common/platform.h"
#include "../common/transform.h"
#include "encoder_dict.h"
#include "find_match_length.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

static BROTLI_INLINE uint32_t Hash(const uint8_t* data) {
  uint32_t h = BROTLI_UNALIGNED_LOAD32LE(data) * kDictHashMul32;
  /* The higher bits contain more mixture from the multiplication,
     so we take our results from there. */
  return h >> (32 - kDictNumBits);
}

static BROTLI_INLINE void AddMatch(size_t distance, size_t len, size_t len_code,
                                   uint32_t* matches) {
  uint32_t match = (uint32_t)((distance << 5) + len_code);
  matches[len] = BROTLI_MIN(uint32_t, matches[len], match);
}

static BROTLI_INLINE size_t DictMatchLength(const BrotliDictionary* dictionary,
                                            const uint8_t* data,
                                            size_t id,
                                            size_t len,
                                            size_t maxlen) {
  const size_t offset = dictionary->offsets_by_length[len] + len * id;
  return FindMatchLengthWithLimit(&dictionary->data[offset], data,
                                  BROTLI_MIN(size_t, len, maxlen));
}

static BROTLI_INLINE BROTLI_BOOL IsMatch(const BrotliDictionary* dictionary,
    DictWord w, const uint8_t* data, size_t max_length) {
  if (w.len > max_length) {
    return BROTLI_FALSE;
  } else {
    const size_t offset = dictionary->offsets_by_length[w.len] +
        (size_t)w.len * (size_t)w.idx;
    const uint8_t* dict = &dictionary->data[offset];
    if (w.transform == 0) {
      /* Match against base dictionary word. */
      return
          TO_BROTLI_BOOL(FindMatchLengthWithLimit(dict, data, w.len) == w.len);
    } else if (w.transform == 10) {
      /* Match against uppercase first transform.
         Note that there are only ASCII uppercase words in the lookup table. */
      return TO_BROTLI_BOOL(dict[0] >= 'a' && dict[0] <= 'z' &&
              (dict[0] ^ 32) == data[0] &&
              FindMatchLengthWithLimit(&dict[1], &data[1], w.len - 1u) ==
              w.len - 1u);
    } else {
      /* Match against uppercase all transform.
         Note that there are only ASCII uppercase words in the lookup table. */
      size_t i;
      for (i = 0; i < w.len; ++i) {
        if (dict[i] >= 'a' && dict[i] <= 'z') {
          if ((dict[i] ^ 32) != data[i]) return BROTLI_FALSE;
        } else {
          if (dict[i] != data[i]) return BROTLI_FALSE;
        }
      }
      return BROTLI_TRUE;
    }
  }
}

/* Finds matches for a single static dictionary */
static BROTLI_BOOL BrotliFindAllStaticDictionaryMatchesFor(
    const BrotliEncoderDictionary* dictionary, const uint8_t* data,
    size_t min_length, size_t max_length, uint32_t* matches) {
  BROTLI_BOOL has_found_match = BROTLI_FALSE;
#if defined(BROTLI_EXPERIMENTAL)
  if (dictionary->has_words_heavy) {
    const BrotliTrieNode* node = &dictionary->trie.root;
    size_t l = 0;
    while (node && l < max_length) {
      uint8_t c;
      if (l >= min_length && node->len_) {
        AddMatch(node->idx_, l, node->len_, matches);
        has_found_match = BROTLI_TRUE;
      }
      c = data[l++];
      node = BrotliTrieSub(&dictionary->trie, node, c);
    }
    return has_found_match;
  }
#endif  /* BROTLI_EXPERIMENTAL */
  {
    size_t offset = dictionary->buckets[Hash(data)];
    BROTLI_BOOL end = !offset;
    while (!end) {
      DictWord w = dictionary->dict_words[offset++];
      const size_t l = w.len & 0x1F;
      const size_t n = (size_t)1 << dictionary->words->size_bits_by_length[l];
      const size_t id = w.idx;
      end = !!(w.len & 0x80);
      w.len = (uint8_t)l;
      if (w.transform == 0) {
        const size_t matchlen =
            DictMatchLength(dictionary->words, data, id, l, max_length);
        const uint8_t* s;
        size_t minlen;
        size_t maxlen;
        size_t len;
        /* Transform "" + BROTLI_TRANSFORM_IDENTITY + "" */
        if (matchlen == l) {
          AddMatch(id, l, l, matches);
          has_found_match = BROTLI_TRUE;
        }
        /* Transforms "" + BROTLI_TRANSFORM_OMIT_LAST_1 + "" and
                      "" + BROTLI_TRANSFORM_OMIT_LAST_1 + "ing " */
        if (matchlen >= l - 1) {
          AddMatch(id + 12 * n, l - 1, l, matches);
          if (l + 2 < max_length &&
              data[l - 1] == 'i' && data[l] == 'n' && data[l + 1] == 'g' &&
              data[l + 2] == ' ') {
            AddMatch(id + 49 * n, l + 3, l, matches);
          }
          has_found_match = BROTLI_TRUE;
        }
        /* Transform "" + BROTLI_TRANSFORM_OMIT_LAST_# + "" (# = 2 .. 9) */
        minlen = min_length;
        if (l > 9) minlen = BROTLI_MAX(size_t, minlen, l - 9);
        maxlen = BROTLI_MIN(size_t, matchlen, l - 2);
        for (len = minlen; len <= maxlen; ++len) {
          size_t cut = l - len;
          size_t transform_id = (cut << 2) +
              (size_t)((dictionary->cutoffTransforms >> (cut * 6)) & 0x3F);
          AddMatch(id + transform_id * n, len, l, matches);
          has_found_match = BROTLI_TRUE;
        }
        if (matchlen < l || l + 6 >= max_length) {
          continue;
        }
        s = &data[l];
        /* Transforms "" + BROTLI_TRANSFORM_IDENTITY + <suffix> */
        if (s[0] == ' ') {
          AddMatch(id + n, l + 1, l, matches);
          if (s[1] == 'a') {
            if (s[2] == ' ') {
              AddMatch(id + 28 * n, l + 3, l, matches);
            } else if (s[2] == 's') {
              if (s[3] == ' ') AddMatch(id + 46 * n, l + 4, l, matches);
            } else if (s[2] == 't') {
              if (s[3] == ' ') AddMatch(id + 60 * n, l + 4, l, matches);
            } else if (s[2] == 'n') {
              if (s[3] == 'd' && s[4] == ' ') {
                AddMatch(id + 10 * n, l + 5, l, matches);
              }
            }
          } else if (s[1] == 'b') {
            if (s[2] == 'y' && s[3] == ' ') {
              AddMatch(id + 38 * n, l + 4, l, matches);
            }
          } else if (s[1] == 'i') {
            if (s[2] == 'n') {
              if (s[3] == ' ') AddMatch(id + 16 * n, l + 4, l, matches);
            } else if (s[2] == 's') {
              if (s[3] == ' ') AddMatch(id + 47 * n, l + 4, l, matches);
            }
          } else if (s[1] == 'f') {
            if (s[2] == 'o') {
              if (s[3] == 'r' && s[4] == ' ') {
                AddMatch(id + 25 * n, l + 5, l, matches);
              }
            } else if (s[2] == 'r') {
              if (s[3] == 'o' && s[4] == 'm' && s[5] == ' ') {
                AddMatch(id + 37 * n, l + 6, l, matches);
              }
            }
          } else if (s[1] == 'o') {
            if (s[2] == 'f') {
              if (s[3] == ' ') AddMatch(id + 8 * n, l + 4, l, matches);
            } else if (s[2] == 'n') {
              if (s[3] == ' ') AddMatch(id + 45 * n, l + 4, l, matches);
            }
          } else if (s[1] == 'n') {
            if (s[2] == 'o' && s[3] == 't' && s[4] == ' ') {
              AddMatch(id + 80 * n, l + 5, l, matches);
            }
          } else if (s[1] == 't') {
            if (s[2] == 'h') {
              if (s[3] == 'e') {
                if (s[4] == ' ') AddMatch(id + 5 * n, l + 5, l, matches);
              } else if (s[3] == 'a') {
                if (s[4] == 't' && s[5] == ' ') {
                  AddMatch(id + 29 * n, l + 6, l, matches);
                }
              }
            } else if (s[2] == 'o') {
              if (s[3] == ' ') AddMatch(id + 17 * n, l + 4, l, matches);
            }
          } else if (s[1] == 'w') {
            if (s[2] == 'i' && s[3] == 't' && s[4] == 'h' && s[5] == ' ') {
              AddMatch(id + 35 * n, l + 6, l, matches);
            }
          }
        } else if (s[0] == '"') {
          AddMatch(id + 19 * n, l + 1, l, matches);
          if (s[1] == '>') {
            AddMatch(id + 21 * n, l + 2, l, matches);
          }
        } else if (s[0] == '.') {
          AddMatch(id + 20 * n, l + 1, l, matches);
          if (s[1] == ' ') {
            AddMatch(id + 31 * n, l + 2, l, matches);
            if (s[2] == 'T' && s[3] == 'h') {
              if (s[4] == 'e') {
                if (s[5] == ' ') AddMatch(id + 43 * n, l + 6, l, matches);
              } else if (s[4] == 'i') {
                if (s[5] == 's' && s[6] == ' ') {
                  AddMatch(id + 75 * n, l + 7, l, matches);
                }
              }
            }
          }
        } else if (s[0] == ',') {
          AddMatch(id + 76 * n, l + 1, l, matches);
          if (s[1] == ' ') {
            AddMatch(id + 14 * n, l + 2, l, matches);
          }
        } else if (s[0] == '\n') {
          AddMatch(id + 22 * n, l + 1, l, matches);
          if (s[1] == '\t') {
            AddMatch(id + 50 * n, l + 2, l, matches);
          }
        } else if (s[0] == ']') {
          AddMatch(id + 24 * n, l + 1, l, matches);
        } else if (s[0] == '\'') {
          AddMatch(id + 36 * n, l + 1, l, matches);
        } else if (s[0] == ':') {
          AddMatch(id + 51 * n, l + 1, l, matches);
        } else if (s[0] == '(') {
          AddMatch(id + 57 * n, l + 1, l, matches);
        } else if (s[0] == '=') {
          if (s[1] == '"') {
            AddMatch(id + 70 * n, l + 2, l, matches);
          } else if (s[1] == '\'') {
            AddMatch(id + 86 * n, l + 2, l, matches);
          }
        } else if (s[0] == 'a') {
          if (s[1] == 'l' && s[2] == ' ') {
            AddMatch(id + 84 * n, l + 3, l, matches);
          }
        } else if (s[0] == 'e') {
          if (s[1] == 'd') {
            if (s[2] == ' ') AddMatch(id + 53 * n, l + 3, l, matches);
          } else if (s[1] == 'r') {
            if (s[2] == ' ') AddMatch(id + 82 * n, l + 3, l, matches);
          } else if (s[1] == 's') {
            if (s[2] == 't' && s[3] == ' ') {
              AddMatch(id + 95 * n, l + 4, l, matches);
            }
          }
        } else if (s[0] == 'f') {
          if (s[1] == 'u' && s[2] == 'l' && s[3] == ' ') {
            AddMatch(id + 90 * n, l + 4, l, matches);
          }
        } else if (s[0] == 'i') {
          if (s[1] == 'v') {
            if (s[2] == 'e' && s[3] == ' ') {
              AddMatch(id + 92 * n, l + 4, l, matches);
            }
          } else if (s[1] == 'z') {
            if (s[2] == 'e' && s[3] == ' ') {
              AddMatch(id + 100 * n, l + 4, l, matches);
            }
          }
        } else if (s[0] == 'l') {
          if (s[1] == 'e') {
            if (s[2] == 's' && s[3] == 's' && s[4] == ' ') {
              AddMatch(id + 93 * n, l + 5, l, matches);
            }
          } else if (s[1] == 'y') {
            if (s[2] == ' ') AddMatch(id + 61 * n, l + 3, l, matches);
          }
        } else if (s[0] == 'o') {
          if (s[1] == 'u' && s[2] == 's' && s[3] == ' ') {
            AddMatch(id + 106 * n, l + 4, l, matches);
          }
        }
      } else {
        /* Set is_all_caps=0 for BROTLI_TRANSFORM_UPPERCASE_FIRST and
               is_all_caps=1 otherwise (BROTLI_TRANSFORM_UPPERCASE_ALL)
           transform. */
        const BROTLI_BOOL is_all_caps =
            TO_BROTLI_BOOL(w.transform != BROTLI_TRANSFORM_UPPERCASE_FIRST);
        const uint8_t* s;
        if (!IsMatch(dictionary->words, w, data, max_length)) {
          continue;
        }
        /* Transform "" + kUppercase{First,All} + "" */
        AddMatch(id + (is_all_caps ? 44 : 9) * n, l, l, matches);
        has_found_match = BROTLI_TRUE;
        if (l + 1 >= max_length) {
          continue;
        }
        /* Transforms "" + kUppercase{First,All} + <suffix> */
        s = &data[l];
        if (s[0] == ' ') {
          AddMatch(id + (is_all_caps ? 68 : 4) * n, l + 1, l, matches);
        } else if (s[0] == '"') {
          AddMatch(id + (is_all_caps ? 87 : 66) * n, l + 1, l, matches);
          if (s[1] == '>') {
            AddMatch(id + (is_all_caps ? 97 : 69) * n, l + 2, l, matches);
          }
        } else if (s[0] == '.') {
          AddMatch(id + (is_all_caps ? 101 : 79) * n, l + 1, l, matches);
          if (s[1] == ' ') {
            AddMatch(id + (is_all_caps ? 114 : 88) * n, l + 2, l, matches);
          }
        } else if (s[0] == ',') {
          AddMatch(id + (is_all_caps ? 112 : 99) * n, l + 1, l, matches);
          if (s[1] == ' ') {
            AddMatch(id + (is_all_caps ? 107 : 58) * n, l + 2, l, matches);
          }
        } else if (s[0] == '\'') {
          AddMatch(id + (is_all_caps ? 94 : 74) * n, l + 1, l, matches);
        } else if (s[0] == '(') {
          AddMatch(id + (is_all_caps ? 113 : 78) * n, l + 1, l, matches);
        } else if (s[0] == '=') {
          if (s[1] == '"') {
            AddMatch(id + (is_all_caps ? 105 : 104) * n, l + 2, l, matches);
          } else if (s[1] == '\'') {
            AddMatch(id + (is_all_caps ? 116 : 108) * n, l + 2, l, matches);
          }
        }
      }
    }
  }
  /* Transforms with prefixes " " and "." */
  if (max_length >= 5 && (data[0] == ' ' || data[0] == '.')) {
    BROTLI_BOOL is_space = TO_BROTLI_BOOL(data[0] == ' ');
    size_t offset = dictionary->buckets[Hash(&data[1])];
    BROTLI_BOOL end = !offset;
    while (!end) {
      DictWord w = dictionary->dict_words[offset++];
      const size_t l = w.len & 0x1F;
      const size_t n = (size_t)1 << dictionary->words->size_bits_by_length[l];
      const size_t id = w.idx;
      end = !!(w.len & 0x80);
      w.len = (uint8_t)l;
      if (w.transform == 0) {
        const uint8_t* s;
        if (!IsMatch(dictionary->words, w, &data[1], max_length - 1)) {
          continue;
        }
        /* Transforms " " + BROTLI_TRANSFORM_IDENTITY + "" and
                      "." + BROTLI_TRANSFORM_IDENTITY + "" */
        AddMatch(id + (is_space ? 6 : 32) * n, l + 1, l, matches);
        has_found_match = BROTLI_TRUE;
        if (l + 2 >= max_length) {
          continue;
        }
        /* Transforms " " + BROTLI_TRANSFORM_IDENTITY + <suffix> and
                      "." + BROTLI_TRANSFORM_IDENTITY + <suffix>
        */
        s = &data[l + 1];
        if (s[0] == ' ') {
          AddMatch(id + (is_space ? 2 : 77) * n, l + 2, l, matches);
        } else if (s[0] == '(') {
          AddMatch(id + (is_space ? 89 : 67) * n, l + 2, l, matches);
        } else if (is_space) {
          if (s[0] == ',') {
            AddMatch(id + 103 * n, l + 2, l, matches);
            if (s[1] == ' ') {
              AddMatch(id + 33 * n, l + 3, l, matches);
            }
          } else if (s[0] == '.') {
            AddMatch(id + 71 * n, l + 2, l, matches);
            if (s[1] == ' ') {
              AddMatch(id + 52 * n, l + 3, l, matches);
            }
          } else if (s[0] == '=') {
            if (s[1] == '"') {
              AddMatch(id + 81 * n, l + 3, l, matches);
            } else if (s[1] == '\'') {
              AddMatch(id + 98 * n, l + 3, l, matches);
            }
          }
        }
      } else if (is_space) {
        /* Set is_all_caps=0 for BROTLI_TRANSFORM_UPPERCASE_FIRST and
               is_all_caps=1 otherwise (BROTLI_TRANSFORM_UPPERCASE_ALL)
           transform. */
        const BROTLI_BOOL is_all_caps =
            TO_BROTLI_BOOL(w.transform != BROTLI_TRANSFORM_UPPERCASE_FIRST);
        const uint8_t* s;
        if (!IsMatch(dictionary->words, w, &data[1], max_length - 1)) {
          continue;
        }
        /* Transforms " " + kUppercase{First,All} + "" */
        AddMatch(id + (is_all_caps ? 85 : 30) * n, l + 1, l, matches);
        has_found_match = BROTLI_TRUE;
        if (l + 2 >= max_length) {
          continue;
        }
        /* Transforms " " + kUppercase{First,All} + <suffix> */
        s = &data[l + 1];
        if (s[0] == ' ') {
          AddMatch(id + (is_all_caps ? 83 : 15) * n, l + 2, l, matches);
        } else if (s[0] == ',') {
          if (!is_all_caps) {
            AddMatch(id + 109 * n, l + 2, l, matches);
          }
          if (s[1] == ' ') {
            AddMatch(id + (is_all_caps ? 111 : 65) * n, l + 3, l, matches);
          }
        } else if (s[0] == '.') {
          AddMatch(id + (is_all_caps ? 115 : 96) * n, l + 2, l, matches);
          if (s[1] == ' ') {
            AddMatch(id + (is_all_caps ? 117 : 91) * n, l + 3, l, matches);
          }
        } else if (s[0] == '=') {
          if (s[1] == '"') {
            AddMatch(id + (is_all_caps ? 110 : 118) * n, l + 3, l, matches);
          } else if (s[1] == '\'') {
            AddMatch(id + (is_all_caps ? 119 : 120) * n, l + 3, l, matches);
          }
        }
      }
    }
  }
  if (max_length >= 6) {
    /* Transforms with prefixes "e ", "s ", ", " and "\xC2\xA0" */
    if ((data[1] == ' ' &&
         (data[0] == 'e' || data[0] == 's' || data[0] == ',')) ||
        (data[0] == 0xC2 && data[1] == 0xA0)) {
      size_t offset = dictionary->buckets[Hash(&data[2])];
      BROTLI_BOOL end = !offset;
      while (!end) {
        DictWord w = dictionary->dict_words[offset++];
        const size_t l = w.len & 0x1F;
        const size_t n = (size_t)1 << dictionary->words->size_bits_by_length[l];
        const size_t id = w.idx;
        end = !!(w.len & 0x80);
        w.len = (uint8_t)l;
        if (w.transform == 0 &&
            IsMatch(dictionary->words, w, &data[2], max_length - 2)) {
          if (data[0] == 0xC2) {
            AddMatch(id + 102 * n, l + 2, l, matches);
            has_found_match = BROTLI_TRUE;
          } else if (l + 2 < max_length && data[l + 2] == ' ') {
            size_t t = data[0] == 'e' ? 18 : (data[0] == 's' ? 7 : 13);
            AddMatch(id + t * n, l + 3, l, matches);
            has_found_match = BROTLI_TRUE;
          }
        }
      }
    }
  }
  if (max_length >= 9) {
    /* Transforms with prefixes " the " and ".com/" */
    if ((data[0] == ' ' && data[1] == 't' && data[2] == 'h' &&
         data[3] == 'e' && data[4] == ' ') ||
        (data[0] == '.' && data[1] == 'c' && data[2] == 'o' &&
         data[3] == 'm' && data[4] == '/')) {
      size_t offset = dictionary->buckets[Hash(&data[5])];
      BROTLI_BOOL end = !offset;
      while (!end) {
        DictWord w = dictionary->dict_words[offset++];
        const size_t l = w.len & 0x1F;
        const size_t n = (size_t)1 << dictionary->words->size_bits_by_length[l];
        const size_t id = w.idx;
        end = !!(w.len & 0x80);
        w.len = (uint8_t)l;
        if (w.transform == 0 &&
            IsMatch(dictionary->words, w, &data[5], max_length - 5)) {
          AddMatch(id + (data[0] == ' ' ? 41 : 72) * n, l + 5, l, matches);
          has_found_match = BROTLI_TRUE;
          if (l + 5 < max_length) {
            const uint8_t* s = &data[l + 5];
            if (data[0] == ' ') {
              if (l + 8 < max_length &&
                  s[0] == ' ' && s[1] == 'o' && s[2] == 'f' && s[3] == ' ') {
                AddMatch(id + 62 * n, l + 9, l, matches);
                if (l + 12 < max_length &&
                    s[4] == 't' && s[5] == 'h' && s[6] == 'e' && s[7] == ' ') {
                  AddMatch(id + 73 * n, l + 13, l, matches);
                }
              }
            }
          }
        }
      }
    }
  }
  return has_found_match;
}

/* Finds matches for one or more dictionaries, if multiple are present
   in the contextual dictionary */
BROTLI_BOOL BrotliFindAllStaticDictionaryMatches(
    const BrotliEncoderDictionary* dictionary, const uint8_t* data,
    size_t min_length, size_t max_length, uint32_t* matches) {
  BROTLI_BOOL has_found_match =
      BrotliFindAllStaticDictionaryMatchesFor(
          dictionary, data, min_length, max_length, matches);

  if (!!dictionary->parent && dictionary->parent->num_dictionaries > 1) {
    uint32_t matches2[BROTLI_MAX_STATIC_DICTIONARY_MATCH_LEN + 1];
    int l;
    const BrotliEncoderDictionary* dictionary2 = dictionary->parent->dict[0];
    if (dictionary2 == dictionary) {
      dictionary2 = dictionary->parent->dict[1];
    }

    for (l = 0; l < BROTLI_MAX_STATIC_DICTIONARY_MATCH_LEN + 1; l++) {
      matches2[l] = kInvalidMatch;
    }

    has_found_match |= BrotliFindAllStaticDictionaryMatchesFor(
        dictionary2, data, min_length, max_length, matches2);

    for (l = 0; l < BROTLI_MAX_STATIC_DICTIONARY_MATCH_LEN + 1; l++) {
      if (matches2[l] != kInvalidMatch) {
        uint32_t dist = (uint32_t)(matches2[l] >> 5);
        uint32_t len_code = matches2[l] & 31;
        uint32_t skipdist = (uint32_t)((uint32_t)(1 << dictionary->words->
            size_bits_by_length[len_code]) & ~1u) *
            (uint32_t)dictionary->num_transforms;
        /* TODO(lode): check for dist overflow */
        dist += skipdist;
        AddMatch(dist, (size_t)l, len_code, matches);
      }
    }
  }
  return has_found_match;
}
#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
