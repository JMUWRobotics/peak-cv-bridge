project('genicvbridge', 'cpp')

cmake = import('cmake')

swss_opts = cmake.subproject_options()
swss_opts.add_cmake_defines({'USE_STANDALONE_ASIO': true})
swss_proj = cmake.subproject('swss', options: swss_opts)

opencv = dependency('opencv4')

dependencies = [
    # vendored depencencies
    dependency('fmt', version: '>=11.1.1', static: true),
    dependency('cxxopts', version: '>=3.2.0', static: true),
    swss_proj.dependency('simple-websocket-server'),

    # system dependencies
    opencv,
    dependency('openssl')
]

cc = meson.get_compiler('cpp')

libsrcs = ['src/genicvbridge.cpp']
capsrcs = ['src/genicvcapture.cpp']
strsrcs = ['src/genicvstreamer.cpp', 'src/genicvstream_server.cpp']
incdirs = ['include']

has_v4l2_colorcode = cc.compiles(
    '''
#include <opencv2/imgproc.hpp>
int foo() { return cv::COLOR_BGR2YUV_YUYV; }
''',
    name: 'cv::COLOR_BGR2YUV_YUYV',
    dependencies: opencv
)
if has_v4l2_colorcode
    add_project_arguments('-DBRIDGE_V4L2LOOPBACK', language: 'cpp')
endif

ids_peak = dependency('ids_peak', required: false)
if ids_peak.found()
    add_project_arguments('-DBRIDGE_IDSPEAK', language: 'cpp')
    dependencies += ids_peak
    libsrcs += 'src/backend/ids-peak.cpp'
endif

spinnaker = cc.find_library(
    'Spinnaker',
    dirs: '/opt/spinnaker/lib',
    has_headers: 'Spinnaker.h',
    header_include_directories: include_directories('/opt/spinnaker/include'),
    required: false,
)
if spinnaker.found()
    add_project_arguments('-DBRIDGE_SPINNAKER', language: 'cpp')
    dependencies += spinnaker
    libsrcs += 'src/backend/spinnaker.cpp'
    incdirs += '/opt/spinnaker/include'
endif

aravis = dependency('aravis-0.10', fallback: ['aravis'], required: false)
if aravis.found()
    add_project_arguments('-DBRIDGE_ARAVIS', language: 'cpp')
    dependencies += aravis
    libsrcs += 'src/backend/aravis.cpp'
endif

if not ids_peak.found() and not spinnaker.found() and not aravis.found()
    error('No backend found!')
endif

lib = library(
    'genicvbridge',
    sources: libsrcs,
    include_directories: incdirs,
    dependencies: dependencies,
    install: true,
)

executable(
    'genicvcapture',
    sources: capsrcs,
    include_directories: incdirs,
    dependencies: dependencies,
    link_with: lib,
    install: true,
)

executable(
    'genicvstreamer',
    sources: strsrcs,
    include_directories: incdirs,
    dependencies: dependencies,
    link_with: lib,
    install: true,
)

install_headers('include/genicvbridge.hpp')

install_emptydir('/etc/genicvstreamers')
install_data('systemd/example.env', install_dir: 'share/genicvstreamer')
install_data('systemd/genicvstreamer@.service', install_dir: 'lib/systemd/system')
