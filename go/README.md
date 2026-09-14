# Median XL Offline Tools (Go port)

Go port of the C++/Qt CLI in `../src`. Command-line character editor for Diablo 2 - Median XL mod `.d2s`
save files:

- view a character's class, level, stats, and stat/skill points
- respec stats, skills, or both
- automatic timestamped backup before any file is modified

Item bytes are intentionally not parsed or touched, exactly like the original: everything from the first
item marker to the end of the save file is treated as an opaque blob and preserved byte-for-byte.

## Why this port is faithful

The original binary format relies on some very specific, non-obvious bit-level and checksum tricks. This
port mirrors them exactly rather than "cleaning them up", and has unit + integration tests that exercise the
real `resources/data` files (props/skills/basestats, which are Qt-`qCompress`'d and CRC-16'd) to prove it:

- `internal/qtcompress`: reimplements Qt's default `qChecksum` (CRC-16/X-25) and the `qCompress`/
  `qUncompress` on-disk framing (4-byte big-endian length + raw zlib stream) used by the mod's `.dat` files.
- `internal/bitreader`: ports the "reverse bit reader" trick the save format uses for the stats section.
- `internal/character`: ports `characterfile.cpp`'s load/respec/save/summary logic, including the quirky
  32-bit (not 64-bit) wraparound arithmetic used when rebuilding `FreeStatPoints`.

## Layout

```
go/
  cmd/mxl/                 CLI entrypoint (view/respec commands)
  internal/enums/          offsets, status bits, stat codes (mirrors src/enums.h)
  internal/bitreader/      reverse bit reader (mirrors src/reversebitreader.*)
  internal/qtcompress/     qChecksum + qUncompress reimplementation
  internal/itemdb/         props.dat/skills.dat loading (mirrors src/itemdatabase.*)
  internal/resources/      resource path/locale resolution (mirrors resourcepathmanager.hpp, languagemanager.hpp)
  internal/character/      .d2s parsing/respec/save/summary (mirrors src/characterfile.*, characterinfo.hpp)
```

## Building

Requires Go 1.21+.

```sh
cd go
go build -o mxl ./cmd/mxl
```

The binary expects a `resources` directory (containing the mod's `basestats.dat` and localized
`props.dat`/`skills.dat`) next to it at runtime, same as the original - or set the `DATA_PATH` environment
variable to point elsewhere. Locale defaults to `en`; override with `MXL_LANGUAGE=ru` etc.

## Usage

```sh
./mxl view <charfile.d2s>
./mxl respec <stats|skills|both> <charfile.d2s> [--no-backup]
```

## Testing

```sh
go test ./...
```

This includes a round-trip test (encode → decode → respec → checksum → save → reload) against the real
`resources/data` tables, and an integration test that builds the actual CLI binary and drives it as a
subprocess.
