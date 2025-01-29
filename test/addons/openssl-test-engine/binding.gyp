{
  'targets': [
    {
      'target_name': 'testsetengine',
      'type': 'none',
      'includes': ['../common.gypi'],
      'conditions': [
        ['(OS=="mac" or OS=="linux") and '
         'node_use_openssl=="true" and '
         'node_shared=="false" and '
         'node_shared_openssl=="false"', {
          'type': 'shared_library',
          'sources': [ 'testsetengine.cc' ],
          'product_extension': 'engine',
          'include_dirs': ['../../../deps/openssl/openssl/include'],
          'conditions': [
            ['OS=="mac"', {
              'xcode_settings': {
                'OTHER_CFLAGS': ['-Wno-deprecated-declarations'],
              },
              'link_settings': {
                'libraries': [
                  '../../../../out/<(PRODUCT_DIR)/<(openssl_product)'
                ]
              },
            }],
            ['OS=="linux"', {
              'cflags': [
                '-Wno-deprecated-declarations',
              ],
            }],
            # Avoid excessive LTO
            ['enable_lto=="true"', {
              'ldflags': [ '-fno-lto' ],
            }],
          ],
        }],
      ],
    }
  ]
}
