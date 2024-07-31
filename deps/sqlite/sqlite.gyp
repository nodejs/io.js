{
  'variables': {
    'sqlite_sources': [
      'sqlite3.c',
    ],
  },
  'targets': [
    {
      'target_name': 'sqlite',
      'type': 'static_library',
      'cflags': ['-fvisibility=hidden'],
      'xcode_settings': {
        'GCC_SYMBOLS_PRIVATE_EXTERN': 'YES',  # -fvisibility=hidden
      },
      'include_dirs': ['.'],
      'sources': [
        '<@(sqlite_sources)',
      ],
      'direct_dependent_settings': {
        'include_dirs': ['.'],
      },
    },
  ],
}
