{
  'variables': {
    'nghttp2_sources': [
      'lib/nghttp2_buf.c',
      'lib/nghttp2_callbacks.c',
      'lib/nghttp2_debug.c',
      'lib/nghttp2_extpri.c',
      'lib/nghttp2_frame.c',
      'lib/nghttp2_hd.c',
      'lib/nghttp2_hd_huffman.c',
      'lib/nghttp2_hd_huffman_data.c',
      'lib/nghttp2_helper.c',
      'lib/nghttp2_http.c',
      'lib/nghttp2_map.c',
      'lib/nghttp2_mem.c',
      'lib/nghttp2_alpn.c',
      'lib/nghttp2_option.c',
      'lib/nghttp2_outbound_item.c',
      'lib/nghttp2_pq.c',
      'lib/nghttp2_priority_spec.c',
      'lib/nghttp2_queue.c',
      'lib/nghttp2_ratelim.c',
      'lib/nghttp2_rcbuf.c',
      'lib/nghttp2_session.c',
      'lib/nghttp2_stream.c',
      'lib/nghttp2_submit.c',
      'lib/nghttp2_time.c',
      'lib/nghttp2_version.c',
      'lib/sfparse.c',
    ]
  },
  'target_defaults': {
    'defines': [
      '_U_='
    ]
  },
  'targets': [
    {
      'target_name': 'nghttp2',
      'type': 'static_library',
      'include_dirs': ['lib/includes'],
      'defines': [
        'BUILDING_NGHTTP2',
        'NGHTTP2_STATICLIB',
        'HAVE_CONFIG_H',
      ],
      'conditions': [
        ['OS=="win"', {
          'defines': [
            'WIN32',
            '_WINDOWS',
          ],
          'msvs_settings': {
            'VCCLCompilerTool': {
              'CompileAs': '1'
            },
          },
        }],
        ['debug_nghttp2 == 1', {
          'defines': [ 'DEBUGBUILD=1' ]
        }]
      ],
      'direct_dependent_settings': {
        'defines': [ 'NGHTTP2_STATICLIB' ],
        'include_dirs': [ 'lib/includes' ]
      },
      'sources': [
        '<@(nghttp2_sources)',
      ]
    }
  ]
}
