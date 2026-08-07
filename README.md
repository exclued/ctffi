# ctffi

CTF metadata to libffi bridge for dynamically calling C functions.

## Overview

`ctffi` is a small C library that uses [CTF (Compact Type Format)](https://github.com/CTF-tools/ctf-spec) metadata embedded in ELF objects to obtain function type information and construct corresponding [libffi](https://sourceware.org/libffi/) call interfaces.

The intended use case is software that needs to discover C function signatures from compiled objects at runtime instead of maintaining a separate set of manually defined `libffi` type descriptions.

The basic data flow is:

```text
ELF object with CTF metadata
            |
            v
          libctf
            |
            v
          ctffi
            |
            v
          libffi
            |
            v
     dynamic function call
```

`ctffi` deliberately focuses on this CTF-to-libffi conversion problem. It is not intended to be a general-purpose debug-information framework or a replacement for CTF, DWARF, BTF, or libffi itself.

## Requirements

Building the project requires:

- CMake 3.16 or newer;
- a C compiler with CTF generation support (`-gctf`);
- `libctf` development files from GNU binutils;
- `libffi` development files;
- `pkg-config`.

The compiler must support `-gctf` because the test suite builds a shared-library fixture containing CTF metadata.

## Building

Configure and build with CMake:

```sh
cmake -S . -B build
cmake --build build
```

Run the test suite with:

```sh
ctest --test-dir build --output-on-failure
```

The CTF-enabled test fixture is generated as part of the normal build.

### Producing CTF metadata

A target object must contain CTF metadata for `ctffi` to obtain its type information. With a compiler supporting CTF generation, this can be enabled with:

```sh
gcc -gctf -o library.so library.c
```

The exact command-line options required by a particular toolchain may differ. CTF may also be removed by subsequent binary-processing or stripping steps.

## Runtime dependencies

`ctffi` uses:

- **libctf** from GNU binutils to read CTF metadata;
- **libffi** to describe and invoke functions dynamically;
- the platform dynamic-loader interface to resolve the function symbol in the target object.

The project intentionally relies on `libctf` rather than implementing its own CTF parser. This keeps `ctffi` small and delegates CTF format handling to the existing library.

## CTF and ELF

CTF is a compact binary type-information format designed for use with object files and executables. When present in an ELF object, it can describe information needed to reconstruct function signatures and their associated C types.

CTF metadata is not guaranteed to be present in every ELF object. A binary must be built with suitable CTF information, and that information must remain available in the final object presented to `ctffi`.

## Project scope

The project is intentionally narrow:

- CTF is the source of type information;
- libctf is used to read that information;
- libffi is used for dynamic calls;
- the project does not define a new type-description format;
- the project does not provide bindings for other programming languages;
- the project does not attempt to become a general debugger or reflection framework.

This narrow scope is intentional: the goal is to provide a small, understandable bridge between two existing technologies.

## License

`ctffi` is distributed under the [MIT License](LICENSE).

### Important dependency notice

`ctffi` currently depends on **libctf**, which is distributed as part of GNU binutils under the **GNU General Public License, version 3 or later (GPLv3+)**.

The MIT license applies to the `ctffi` source code itself and does not change the licensing terms of its dependencies. Anyone redistributing software that uses `ctffi` should review the applicable licensing requirements of `libctf` and the other dependencies, especially when distributing proprietary software.

This notice is informational and is not legal advice.

## References

- [CTF Format Specification](https://github.com/CTF-tools/ctf-spec)
- [libctf documentation](https://sourceware.org/binutils/docs/libctf/)
- [libffi documentation](https://sourceware.org/libffi/)
- [GCC debugging options](https://gcc.gnu.org/onlinedocs/gcc/Debugging-Options.html)
- [ELF specification](https://refspecs.linuxfoundation.org/elf/elf.pdf)
