{
  "targets": [
    {
      "target_name": "test_reference_all_types",
      "sources": [ "test_reference_by_node_api_version.c" ],
      "defines": [ "NAPI_VERSION=10" ],
    },
    {
      "target_name": "test_reference_obj_only",
      "sources": [ "test_reference_by_node_api_version.c" ],
      "defines": [ "NAPI_VERSION=9" ],
    }
  ]
}
