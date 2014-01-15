{

  'targets': [

    {
      'target_name': 'hiredis',
      'type': 'static_library',
      'direct_dependent_settings': {
        'include_dirs': [ '.' ],
      },
      'sources': [
        'hiredis.c',
        'dict.c',
        'async.c',
        'net.c',
        'sds.c',
      ],
      'cflags': [
        '-D_POSIX_SOURCE',
        '-std=c99',
        '-Wall',
        '-O3',
      ],
      'conditions': [
        ['OS=="mac"', {
          'xcode_settings': {
            'GCC_C_LANGUAGE_STANDARD': 'c99'
          }
        }],
      ]
    },

  ]
}