{
  'targets': [
    {
      'target_name': 'binding',
      'sources': [ 'binding.cc' ],
      'defines': [
          'NAPI_EXPERIMENTAL'
      ]
    },
    {
      'target_name': 'binding_node_api_v8',
      'sources': [ 'binding.cc' ],
      'defines': [
          'NAPI_VERSION=8'
      ]
    }
  ]
}
