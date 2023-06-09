project(
  'tiny_process_library',
  'cpp',
  version: '4bf0e59e64f18d3080a1ce4e853775de2181e993',
  license: 'MIT',
  default_options: ['cpp_std=c++17'],
  meson_version: '>=0.63.0',
)

src = files(
  'process.cpp',
  'process_unix.cpp',
)

inc = include_directories('.')

tiny_process_library_dependencies = []
tiny_process_library_compile_args = ['-O3']

tiny_process_library_lib = static_library(
  'tiny_process_library',
  src,
  dependencies: tiny_process_library_dependencies,
  cpp_args: tiny_process_library_compile_args,
  c_args: tiny_process_library_compile_args,
)

tiny_process_library_dep = declare_dependency(
    dependencies: tiny_process_library_dependencies,
    include_directories: inc,
    link_with: tiny_process_library_lib,
    compile_args: tiny_process_library_compile_args,
)
