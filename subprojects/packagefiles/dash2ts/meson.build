project(
  'dash2ts',
  'c', 'cpp',
  version: '0.8.5',
  license: 'Apache-2.0 license',
  default_options: ['cpp_std=c++17'],
  meson_version: '>=0.56.2',
)

inc = include_directories(['src', 'src/mpegts', 'src/addons/kodi-dev-kit/include'])
lib = static_library('dash2ts',
                     'src/mpegts/common.cpp',
                     'src/mpegts/crc.cpp',
                     'src/mpegts/mpegts_muxer.cpp',
                     'src/mpegts/simple_buffer.cpp',
                     'src/mpegts/ts_packet.cpp',
                     'src/streamplayer.cpp',
                     'src/addonhandler.cpp',
                     'src/audioconverter.cpp',
                     'src/bitstreamconverter.cpp',
                     'src/curlhandler.cpp',
                     'src/demuxpacket.cpp',
                     'src/StringUtils.cpp',
                     'src/xmlhandler.cpp',
                     include_directories: inc
                    )

curl_dep = dependency('libcurl')
avutil_dep = dependency('libavutil')
avformat_dep = dependency('libavformat')
avfilter_dep = dependency('libavfilter')
avcodec_dep = dependency('libavcodec')

dash2ts_dependencies = [curl_dep, avutil_dep, avformat_dep, avfilter_dep, avcodec_dep]
dash2ts_compile_args = ['-O3']

dash2ts_dep = declare_dependency(
    include_directories: inc,
    link_with : lib,
    dependencies: dash2ts_dependencies
)
