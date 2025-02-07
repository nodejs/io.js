{
  'variables': {
    'crdtp_sources': [
      'crdtp/cbor.cc',
      'crdtp/cbor.h',
      'crdtp/dispatch.cc',
      'crdtp/dispatch.h',
      'crdtp/error_support.cc',
      'crdtp/error_support.h',
      'crdtp/export.h',
      'crdtp/find_by_first.h',
      'crdtp/frontend_channel.h',
      'crdtp/json.cc',
      'crdtp/json.h',
      'crdtp/json_platform.cc',
      'crdtp/json_platform.h',
      'crdtp/maybe.h',
      'crdtp/parser_handler.h',
      'crdtp/protocol_core.cc',
      'crdtp/protocol_core.h',
      'crdtp/serializable.cc',
      'crdtp/serializable.h',
      'crdtp/span.cc',
      'crdtp/span.h',
      'crdtp/status.cc',
      'crdtp/status.h',
    ]
  },
  'targets': [
    {
      'target_name': 'crdtp',
      'type': 'static_library',
      'include_dirs': [ '.' ],
      'direct_dependent_settings': {
        # Use like `#include "crdtp/json.h"`
        'include_dirs': [ '.' ],
      },
      'sources': [
        '<@(crdtp_sources)',
      ],
    },
  ]
}
