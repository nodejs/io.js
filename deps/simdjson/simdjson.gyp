{
  'variables': {
    'simdjson_sources': [
      'simdjson.cpp',
    ]
  },
  'targets': [
     {
       'target_name': 'simdjson',
       'type': 'static_library',
       'include_dirs': ['.'],
       'direct_dependent_settings': {
         'include_dirs': ['.'],
       },
       'sources': [
         '<@(simdjson_sources)',
       ],
     },
  ]
}
