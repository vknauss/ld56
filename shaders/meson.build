glslc = find_program('glslc')

shaders_input = [
  {
    'input': 'test.vs.glsl',
    'type': 'vertex'
  },
  {
    'input': 'test.fs.glsl',
    'type': 'fragment'
  },
]

shader_targets = []
foreach record : shaders_input
  input_file = files(record['input'])
  output_name = record.get('output', fs.stem(input_file) + '.spv')

  command = [glslc]
  if 'type' in record
    command += '-fshader-stage=' + record['type']
  endif
  command += ['-c', '@INPUT@', '-o', '@OUTPUT@' ]
  command += record.get('arguments', [])

  shader_targets += custom_target(output_name,
    input: input_file,
    output: output_name,
    command: command,
    build_by_default: true,
    install: true,
    install_dir: join_paths(get_option('datadir'), 'shaders')
  )
endforeach
