project(
  'mINI',
  'c', 'cpp',
  version: '0.8.5',
  license: 'Apache-2.0 license',
  default_options: ['cpp_std=c++17'],
  meson_version: '>=0.63.0',
)

inc = include_directories('src')

mini_dependencies = []
mini_compile_args = ['-O3']

mini_dep = declare_dependency(
    include_directories: inc,
    dependencies: mini_dependencies
)
