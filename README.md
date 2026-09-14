# Median XL Offline Tools

Command-line character editor for Diablo 2 - Median XL mod. Written in C++ using [Qt](https://qt.io/) (QtCore only - no GUI).

Supported operations on `.d2s` character save files:
- view a character's class, level, stats, and stat/skill points
- respec stats, skills, or both
- automatic timestamped backup before any file is modified

Items are intentionally not parsed or touched: everything from the first item marker to the end of the
save file is treated as an opaque blob and preserved byte-for-byte.

## Median XL

- the latest version: https://www.median-xl.com/
- the classic one by BrotherLaz: https://modsbylaz.vn.cz/welcome.html

## Code

**DISCLAIMER**: this is far from a great example of writing code and application architecture!

- current code should be compatible with C++03 / C++98 standard
- the latest code is compatible only with the latest mod version

## Building

You will need:

1. C++ compiler: msvc, clang, gcc etc.
2. Qt 4 or 5's QtCore module (e.g. from the Qt Online Installer). Building has only been tested against the latest versions - 4.8.7 and 5.15.x/6.x.
3. [CMake](https://cmake.org/) 3.18+

### IDE

- any IDE supporting CMake: open `CMakeLists.txt` or the repo root directory
- Xcode (macOS): invoke `cmake` on the command line with `-G Xcode`

### Command line

Assuming:
1. your CWD is some build dir, not necessarily inside the repo
2. you have shell variable `qtDir` set to the Qt root directory
3. you have shell variable `repoDir` set to the repo directory

1. Configure: `cmake -S "$repoDir" -B . -D "CMAKE_PREFIX_PATH=$qtDir" <other cmake params>`
2. Build: `cmake --build .`

This produces the `mxl` executable. It expects a `resources/data` directory (containing the mod's
`basestats.dat` and the localized `props.dat`/`skills.dat`) next to it at runtime (or via the `DATA_PATH`
compile definition, which is set automatically for Debug builds to point at the repo's own `resources` dir).

### Example

To generate Visual Studio project that uses x64 build tools (as most likely you're running Windows 64-bit) with Qt built for x86 (32-bit), invoke `cmake` on the command line with `-A Win32 -D "CMAKE_GENERATOR_TOOLSET=host=x64"`

## Usage

```
mxl view <charfile.d2s>
mxl respec <stats|skills|both> <charfile.d2s> [--no-backup]
```
