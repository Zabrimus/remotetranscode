project('remotetranscode', 'cpp', 'c',
  version : '0.1',
  default_options : ['warning_level=1', 'c_std=c17', 'cpp_std=c++17'],
  meson_version: '>=0.56.2')

add_global_arguments(language : 'cpp')

cmake = import('cmake')
cpp = meson.get_compiler('cpp')

#
# openssl
#
openssl_dep = dependency('openssl')

#
# spdlog
#
spdlog_proj = subproject('spdlog')
spdlog_dep = spdlog_proj.get_variable('spdlog_dep', ['tests=disabled', 'compile_library=true'] )

#
# tiny-process-library
#
tiny_process_library_proj = subproject('tiny_process_library')
tiny_process_library_dep = tiny_process_library_proj.get_variable('tiny_process_library_dep')

#
# mINI
#
mini_proj = subproject('mini')
mini_dep = mini_proj.get_variable('mini_dep')

exe = executable('remotrans', 'main.cpp', 'logger.cpp', 'ffmpeghandler.cpp', 'browserclient.cpp', 'transcodeconfig.cpp', 'vdrclient.cpp',
                 'm3u8handler.cpp',
                 install : true,
                 install_dir : meson.current_build_dir() + '/Release',
                 dependencies: [mini_dep, spdlog_dep, tiny_process_library_dep, openssl_dep])

#
# install
#
install_subdir('static-content/movie', install_dir : meson.current_build_dir() + '/Release')
