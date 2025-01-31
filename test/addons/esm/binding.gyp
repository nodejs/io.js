{
  'variables': {
    'source_dir': '<!(<(python) -c "import os; print(os.getcwd())")',
  },
  'targets': [
    {
      'target_name': 'binding',
      'sources': [ 'binding.cc' ],
      'includes': ['../common.gypi'],
    },
    {
      'target_name': 'binding-export-default',
      'sources': [ 'binding-export-default.cc' ],
      'includes': ['../common.gypi'],
    },
    {
      'target_name': 'binding-export-primitive',
      'sources': [ 'binding-export-primitive.cc' ],
      'includes': ['../common.gypi'],
    },
    {
      'target_name': 'node_modules',
      'type': 'none',
      'dependencies': [
        'binding',
      ],
      # The `exports` in package.json can not reference files outside the package
      # directory. Copy the `binding.node` into the package directory so that
      # it can be exported with the conditional exports.
      'copies': [
        {
          'files': [
            '<(PRODUCT_DIR)/binding.node'
          ],
          'destination': '<(source_dir)/node_modules/esm-package/build',
        },
      ],
    }
  ]
}
