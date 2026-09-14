package qtcompress

import (
	"bytes"
	"compress/zlib"
	"encoding/binary"
	"testing"
)

// TestChecksumKnownVector cross-checks Checksum against the standard CRC-16/X-25 test vector ("123456789"
// -> 0x906E), which is exactly the algorithm Qt's qChecksum implements by default (Qt::ChecksumIso3309).
func TestChecksumKnownVector(t *testing.T) {
	got := Checksum([]byte("123456789"))
	if want := uint16(0x906E); got != want {
		t.Errorf("Checksum(%q) = 0x%04X, want 0x%04X", "123456789", got, want)
	}
}

func TestUncompressRoundTrip(t *testing.T) {
	original := []byte("the quick brown fox jumps over the lazy dog, repeated for good measure, " +
		"the quick brown fox jumps over the lazy dog")

	var zlibBuf bytes.Buffer
	w := zlib.NewWriter(&zlibBuf)
	if _, err := w.Write(original); err != nil {
		t.Fatalf("zlib write: %v", err)
	}
	if err := w.Close(); err != nil {
		t.Fatalf("zlib close: %v", err)
	}

	framed := make([]byte, 4+zlibBuf.Len())
	binary.BigEndian.PutUint32(framed[:4], uint32(len(original)))
	copy(framed[4:], zlibBuf.Bytes())

	got, err := Uncompress(framed)
	if err != nil {
		t.Fatalf("Uncompress: %v", err)
	}
	if !bytes.Equal(got, original) {
		t.Errorf("Uncompress() = %q, want %q", got, original)
	}
}

func TestUncompressEmpty(t *testing.T) {
	framed := make([]byte, 4) // length prefix 0, no payload
	got, err := Uncompress(framed)
	if err != nil {
		t.Fatalf("Uncompress: %v", err)
	}
	if len(got) != 0 {
		t.Errorf("Uncompress() = %v, want empty", got)
	}
}

func TestUncompressSizeMismatch(t *testing.T) {
	var zlibBuf bytes.Buffer
	w := zlib.NewWriter(&zlibBuf)
	_, _ = w.Write([]byte("hello"))
	_ = w.Close()

	framed := make([]byte, 4+zlibBuf.Len())
	binary.BigEndian.PutUint32(framed[:4], 999) // wrong expected length
	copy(framed[4:], zlibBuf.Bytes())

	if _, err := Uncompress(framed); err == nil {
		t.Fatal("expected an error for a decompressed-size mismatch")
	}
}
