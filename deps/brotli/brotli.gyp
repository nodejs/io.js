{
  'variables': {
    'brotli_sources': [
      # Common
      'c/common/constants.c',
      'c/common/context.c',
      'c/common/dictionary.c',
      'c/common/platform.c',
      'c/common/shared_dictionary.c',
      'c/common/transform.c',

      # Decoder
      'c/dec/bit_reader.c',
      'c/dec/decode.c',
      'c/dec/huffman.c',
      'c/dec/state.c',

      # Encoder
      'c/enc/backward_references.c',
      'c/enc/backward_references_hq.c',
      'c/enc/bit_cost.c',
      'c/enc/block_splitter.c',
      'c/enc/brotli_bit_stream.c',
      'c/enc/cluster.c',
      'c/enc/command.c',
      'c/enc/compound_dictionary.c',
      'c/enc/compress_fragment.c',
      'c/enc/compress_fragment_two_pass.c',
      'c/enc/dictionary_hash.c',
      'c/enc/encode.c',
      'c/enc/encoder_dict.c',
      'c/enc/entropy_encode.c',
      'c/enc/fast_log.c',
      'c/enc/histogram.c',
      'c/enc/literal_cost.c',
      'c/enc/memory.c',
      'c/enc/metablock.c',
      'c/enc/static_dict.c',
      'c/enc/utf8_util.c',
    ]
  },
  'targets': [
    {
      'target_name': 'brotli',
      'type': 'static_library',
      'include_dirs': ['c/include'],
      'conditions': [
        ['OS=="linux"', {
          'defines': [
            'OS_LINUX'
          ]
        }],
        ['OS=="freebsd"', {
          'defines': [
            'OS_FREEBSD'
          ]
        }],
        ['OS=="mac"', {
          'defines': [
            'OS_MACOSX'
          ]
        }],
      ],
      'direct_dependent_settings': {
        'include_dirs': [ 'c/include' ]
      },
      'libraries': [
        '-lm',
      ],
      'sources': [
        '<@(brotli_sources)',
      ]
    }
  ]
}
