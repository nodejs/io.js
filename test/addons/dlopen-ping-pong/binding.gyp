{
  'targets': [
    {
      'target_name': 'ping',
      'product_extension': 'so',
      'type': 'shared_library',
      'sources': [ 'ping.c' ],
      'conditions': [
        ['OS=="mac"', {
          'xcode_settings': {
            'OTHER_LDFLAGS': [ '-Wl,-undefined', '-Wl,dynamic_lookup' ]
        }}],
        # Enable the shared object to be linked by runtime linker
        ['OS in "aix os400"', {
          'ldflags': [ '-Wl,-G' ]
        }],
        # Avoid excessive LTO
        ['enable_lto=="true"', {
          'ldflags': [ '-fno-lto' ],
        }],
      ],
    },
    {
      'target_name': 'binding',
      'sources': [ 'binding.cc' ],
      'includes': ['../common.gypi'],
    }
  ]
}
