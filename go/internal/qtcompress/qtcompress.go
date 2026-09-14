// Package qtcompress re-implements the two small pieces of Qt's QtCore that the mod's .dat resource files
// depend on: qChecksum's default (Qt::ChecksumIso3309, i.e. CRC-16/X-25) algorithm, and the qCompress /
// qUncompress on-disk framing (a 4-byte big-endian uncompressed-size header followed by a raw zlib stream).
//
// Neither has a suitable off-the-shelf Go package: CRC-16/X-25 is a named standard but the stdlib only
// ships CRC-32/64, and the qCompress framing is Qt-specific. Both are tiny, so they're implemented directly
// here rather than pulling in a third-party CRC-16 dependency; the actual decompression work is delegated
// to the standard library's compress/zlib.
package qtcompress

import (
	"bytes"
	"compress/zlib"
	"encoding/binary"
	"fmt"
	"io"
)

// crcTable is Qt's exact nibble-wise CRC-16/X-25 table (see qChecksum in qbytearray.cpp).
var crcTable = [16]uint16{
	0x0000, 0x1081, 0x2102, 0x3183,
	0x4204, 0x5285, 0x6306, 0x7387,
	0x8408, 0x9489, 0xa50a, 0xb58b,
	0xc60c, 0xd68d, 0xe70e, 0xf78f,
}

// Checksum reproduces Qt's qChecksum(data, Qt::ChecksumIso3309), the default overload used throughout Qt.
func Checksum(data []byte) uint16 {
	crc := uint16(0xffff)
	for _, b := range data {
		crc = (crc>>4)&0x0fff ^ crcTable[(crc^uint16(b))&0xf]
		crc = (crc>>4)&0x0fff ^ crcTable[(crc^uint16(b>>4))&0xf]
	}
	return ^crc
}

// Uncompress reproduces qUncompress: a 4-byte big-endian length prefix followed by a standard zlib stream.
func Uncompress(data []byte) ([]byte, error) {
	if len(data) < 4 {
		return nil, fmt.Errorf("qtcompress: input too short")
	}
	expectedLen := binary.BigEndian.Uint32(data[:4])
	if expectedLen == 0 {
		return []byte{}, nil
	}

	r, err := zlib.NewReader(bytes.NewReader(data[4:]))
	if err != nil {
		return nil, fmt.Errorf("qtcompress: %w", err)
	}
	defer r.Close()

	out, err := io.ReadAll(r)
	if err != nil {
		return nil, fmt.Errorf("qtcompress: %w", err)
	}
	if uint32(len(out)) != expectedLen {
		return nil, fmt.Errorf("qtcompress: decompressed size mismatch: expected %d, got %d", expectedLen, len(out))
	}
	return out, nil
}
