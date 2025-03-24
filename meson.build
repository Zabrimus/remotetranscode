project('remotetranscode', 'cpp', 'c',
  version : '0.1',
  default_options : ['warning_level=1', 'c_std=c17', 'cpp_std=c++17'],
  meson_version: '>=0.63.0')

add_global_arguments('-O3', language : 'cpp')

cmake = import('cmake')
cpp = meson.get_compiler('cpp')

#
# openssl
#
openssl_dep = dependency('openssl')

# thrift
thrift_opts = cmake.subproject_options()
thrift_opts.add_cmake_defines({'WITH_AS3': 'OFF',
                               'WITH_QT5': 'OFF',
                               'BUILD_JAVA': 'OFF',
                               'BUILD_JAVASCRIPT': 'OFF',
                               'BUILD_PYTHON': 'OFF',
                               'BUILD_TESTING': 'OFF',
                               'WITH_C_GLIB': 'OFF',
                               'WITH_OPENSSL': 'OFF',
                               'BUILD_SHARED_LIBS': 'OFF',
                               'BUILD_COMPILER': 'OFF',
                               'CMAKE_POSITION_INDEPENDENT_CODE': 'ON' })

thrift_proj = cmake.subproject('thrift', options: thrift_opts)
thrift_dep = thrift_proj.get_variable('thrift_dep')

#
# spdlog
#
spdlog_proj = subproject('spdlog')
spdlog_dep = spdlog_proj.get_variable('spdlog_dep', ['tests=disabled', 'compile_library=true'] )

#
# tiny-process-libraryle
#
tiny_process_library_proj = subproject('tiny_process_library')
tiny_process_library_dep = tiny_process_library_proj.get_variable('tiny_process_library_dep')

#
# mINI
#
mini_proj = subproject('mini')
mini_dep = mini_proj.get_variable('mini_dep')

#
# dash2ts
#
dash2ts_proj = subproject('dash2ts')
dash2ts_dep = dash2ts_proj.get_variable('dash2ts_dep')

incdir = include_directories(['thrift-services/src-gen', 'thrift-services/src-client'])

remotrans = executable('remotrans', 'main.cpp', 'logger.cpp', 'streamhandler.cpp', 'transcodeconfig.cpp',
                        'm3u8handler.cpp', 'thrift-services/src-client/BrowserClient.cpp', 'thrift-services/src-client/VdrClient.cpp',
                        'thrift-services/src-gen/CommonService.cpp', 'thrift-services/src-gen/common_types.cpp',
	                    'thrift-services/src-gen/CefBrowser.cpp', 'thrift-services/src-gen/cefbrowser_types.cpp',
	                    'thrift-services/src-gen/VdrPluginWeb.cpp', 'thrift-services/src-gen/pluginweb_types.cpp',
                        'thrift-services/src-gen/RemoteTranscoder.cpp', 'thrift-services/src-gen/remotetranscoder_types.cpp',
                        include_directories : incdir,
                        install : true,
                        install_dir : meson.current_build_dir() + '/Release',
                        dependencies: [mini_dep, spdlog_dep, tiny_process_library_dep, openssl_dep, thrift_dep])

r_dash2ts = executable('r_dash2ts', 'r_dash2ts.cpp', 'logger.cpp',
                       install : true,
                       install_dir : meson.current_build_dir() + '/Release',
                       dependencies: [spdlog_dep, dash2ts_dep])

r_dash2tsd = executable('r_dash2ts_debug', 'r_dash2ts_debug.cpp', 'logger.cpp',
                       install : true,
                       install_dir : meson.current_build_dir() + '/Release',
                       dependencies: [spdlog_dep, dash2ts_dep])

#
# install
#
install_subdir('static-content/movie', install_dir : meson.current_build_dir() + '/Release')
