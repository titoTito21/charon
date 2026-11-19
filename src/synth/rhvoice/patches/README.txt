RHVoice 1.18.5 patches for the Charon module (applied in order with
`git apply -p1` to a copy of RHVoice/src by tools/build-charon-synths.py;
the downloaded checkout is never modified). LGPL-2.1-or-later, as the files.

0001-voice-no-boost.patch
  src/core/voice.cpp is the only core file that uses Boost
  (boost::to_lower_copy + replace_all_copy for the voice id). Replaced by an
  equivalent local loop (classic-locale tolower, '-' -> '_'), so the core
  builds without Boost. Behaviour unchanged.

0002-msvc-typename.patch
  property.hpp, document.hpp, xml.hpp: `#ifdef _MSC_VER` branches that omit
  `typename` are replaced by the standard form already used for GCC. Needed
  only by the hosted reference build (Clang in MSVC mode); the EFI build
  never defined _MSC_VER. Behaviour unchanged.

Not patched: exceptions, iostreams, std::locale, pthread mutexes. The module
uses real C++ exceptions (libc++abi + libunwind in bare-metal mode over the
image's own .eh_frame), libc++ streams over the read-only VFS, and no-op
pthread mutexes (one caller at a time by the ABI contract).
