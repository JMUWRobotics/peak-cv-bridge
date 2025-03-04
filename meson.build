project('genicvbridge', 'cpp')

cmake = import('cmake')

swss_opts = cmake.subproject_options()
swss_opts.add_cmake_defines({'USE_STANDALONE_ASIO': true})
swss_proj = cmake.subproject('swss', options: swss_opts)

opencv = dependency('opencv4')

dependencies = [
    # vendored depencencies
    dependency('fmt', version: '>=11.1.3'),
    dependency('cxxopts', version: '>=3.2.0'),
    swss_proj.dependency('simple-websocket-server'),

    # system dependencies
    opencv,
]

libsrcs = ['src/genicvbridge.cpp']
incdirs = ['include']

if opencv.version() >= '4.10.0'
    add_project_arguments('-DBRIDGE_V4L2LOOPBACK', language: 'cpp')
endif

ids_peak = dependency('ids_peak', method: 'cmake', required: false)
if ids_peak.found()
    add_project_arguments('-DBRIDGE_IDSPEAK', language: 'cpp')
    dependencies += ids_peak
    libsrcs += 'src/backend/ids-peak.cpp'
endif

spinnaker = meson.get_compiler('cpp').find_library(
    'Spinnaker',
    dirs: '/opt/spinnaker/lib',
    has_headers: 'Spinnaker.h',
    header_include_directories: include_directories('/opt/spinnaker/include'),
    required: false
)
if spinnaker.found()
    add_project_arguments('-DBRIDGE_SPINNAKER', language: 'cpp')
    dependencies += spinnaker
    libsrcs += 'src/backend/spinnaker.cpp'
    incdirs += '/opt/spinnaker/include'
endif

aravis = dependency('aravis-0.10', required: false)
if aravis.found()
    add_project_arguments('-DBRIDGE_ARAVIS', language: 'cpp')
    dependencies += aravis
    libsrcs += 'src/backend/aravis.cpp'
endif

if not ids_peak.found() and not spinnaker.found() and not aravis.found()
    error('No backend found!')
endif

lib = both_libraries(
    'genicvbridge',
    sources: libsrcs,
    include_directories: incdirs,
    dependencies: dependencies,
    install: true
)
