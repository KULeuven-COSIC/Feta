project('distributed_verifier', 'cpp', default_options : ['cpp_std=c++17', 'buildtype=release', 'b_ndebug=if-release'])
# For debugging purposes, also enables ubsan and asan as extra sanity
# project('distributed_verifier', 'cpp', default_options : ['cpp_std=c++17', 'buildtype=debug', 'b_sanitize=address,undefined'])
# For debugging and valgrind benchmarking, valgridn didn't seem to play nice with the sanitizers
# project('distributed_verifier', 'cpp', default_options : ['cpp_std=c++17', 'buildtype=debug'])

add_project_arguments(['-maes', '-msse4.1', '-mpclmul', '-flto'], language : 'cpp')
add_project_arguments('-DPERFORM_TIMING=' + get_option('perform_timing').to_string(), language : 'cpp')

ssl = dependency('openssl')

common = static_library('common',
  'aes.cpp',
  'aes-ni.cpp',
  'gflifttables.cpp',
  'io.cpp',
  'networking.cpp',
  'player.cpp',
  'random.cpp',
  'Timer.cpp',
  'util.cpp',
  dependencies : [ssl],
)

executable('decoder',
  'gflifttables.cpp',
  'decoder.cpp',
)

executable('preprocessing.tn4', 
  'preprocessing.cpp',
  link_with : [common],
  cpp_args : ['-DCONFIG_FILE=tn4'],
)

executable('preprocessing.tn3', 
  'preprocessing.cpp',
  link_with : [common],
  cpp_args : ['-DCONFIG_FILE=tn3'],
)

executable('preprocessing.log', 
  'preprocessing.cpp',
  link_with : [common],
  cpp_args : ['-DCONFIG_FILE=log'],
)

executable('prover.tn4',
  'tn4/prover.cpp',
  'Circuit.cpp',
  link_with : [common],
)

executable('verifier.tn4',
  'tn4/verifier.cpp',
  'Circuit.cpp',
  link_with : [common],
)

executable('prover.tn3',
  'tn3/prover.cpp',
  'Circuit.cpp',
  link_with : [common],
)

executable('verifier.tn3',
  'tn3/verifier.cpp',
  'Circuit.cpp',
  link_with : [common],
)

executable('prover.log',
  'log/prover.cpp',
  'Circuit.cpp',
  link_with : [common],
)

executable('verifier.log',
  'log/verifier.cpp',
  'Circuit.cpp',
  link_with : [common],
)
