## Copyright (C) 2024 by LH_Mouse <lh_mouse@126.com>
##
## Permission to use, copy, modify, and/or distribute this
## software for any purpose with or without fee is hereby granted.
##
## THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
## WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
## WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
## THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
## CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
## LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
## NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
## CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

project('taxon', 'cpp', version: '1.0.alpha-0')

#===========================================================
# Global configuration
#===========================================================
ver = configuration_data()
ver.set('abi_major', meson.project_version().split('.')[0])
ver.set('abi_minor', meson.project_version().split('.')[1])
ver.set_quoted('abi_string', meson.project_version())

cxx = meson.get_compiler('cpp')
cxx_is_i386 = cxx.compiles('int foo = __i386__;')
cxx_is_amd64 = cxx.compiles('int foo = __amd64__;')
cxx_asm_intel = cxx.compiles('int foo(int a) { return a + 1; }', args: [ '-masm=intel' ])

if cxx_asm_intel
  add_project_arguments('-masm=intel', language: [ 'c', 'cpp' ])
endif

if cxx_is_i386
  add_project_arguments('-msse2', '-mfpmath=sse', language: [ 'c', 'cpp' ])
endif

if get_option('enable-avx') and (cxx_is_i386 or cxx_is_amd64)
  add_project_arguments('-mavx', language: [ 'c', 'cpp' ])
endif

if get_option('enable-debug-checks')
  add_project_arguments('-D_GLIBCXX_DEBUG', '-D_LIBCPP_DEBUG', language: [ 'c', 'cpp' ])
endif

add_project_arguments(
    '-Wall', '-Wextra', '-Winvalid-pch',
    '-D_FILE_OFFSET_BITS=64', '-D_POSIX_C_SOURCE=200809', '-D_DEFAULT_SOURCE',
    '-D_DARWIN_C_SOURCE', '-U_FORTIFY_SOURCE', '-fno-stack-protector',
    '-fstrict-aliasing', '-fstrict-overflow', '-fmerge-all-constants',
    '-frounding-math', '-fno-math-errno', '-fno-trapping-math', '-fno-ident',
    '-fno-devirtualize-speculatively', '-fno-semantic-interposition',
    '-Werror=conversion', '-Werror=sign-compare', '-Werror=sign-conversion',
    '-Werror=write-strings', '-Werror=return-type', '-Werror=double-promotion',
    '-Wmissing-declarations', '-Wmissing-field-initializers', '-Wshadow',
    '-Wsuggest-attribute=noreturn', '-Wswitch-enum', '-Wunused-function',
    '-Wunused-label', '-Wunused-local-typedefs', '-Wunused-but-set-variable',
    '-Wunused-but-set-parameter',
    language: [ 'c', 'cpp' ])

add_project_arguments(
    '-std=c++14', '-fno-elide-constructors', '-fvisibility-inlines-hidden',
    '-fnothrow-opt', '-fno-enforce-eh-specs', '-Wzero-as-null-pointer-constant',
    '-Wno-redundant-move', '-Werror=non-virtual-dtor',
    language: 'cpp')

#===========================================================
# Build rules
#===========================================================
dep_asteria = dependency('asteria')

install_headers('taxon.hpp')

lib_taxon = both_libraries('taxon',
      sources: [ 'taxon.cpp' ],
      dependencies: [ dep_asteria ],
      soversion: ver.get('abi_major'),
      version: '.'.join([ ver.get('abi_major'), ver.get('abi_minor'), '0' ]),
      install: true)

import('pkgconfig').generate(lib_taxon,
      name: 'taxon',
      url: 'https://github.com/lhmouse/taxon',
      description: 'The Type Annotation eXtension for JSON (TAXON) library '
                   + 'encodes 64-bit integers, binary data, timestamps, etc. '
                   + 'in strict JSON syntax.')

#===========================================================
# Test rules
#===========================================================
t = executable('test_main', 'test_main.cpp', link_with: lib_taxon)
test('test_main', t)
