// Package bitreader implements the "reverse" bit reading scheme used by Diablo 2 / Median XL character
// save files, ported from src/reversebitreader.{h,cpp}.
//
// The stats section of a .d2s file is read one byte at a time (in normal file order), each byte turned into
// an 8-character '0'/'1' string, and *prepended* to a growing buffer. The net effect is a buffer where the
// bits of the first file byte end up at the tail end of the string. Reading then proceeds from the tail
// backwards, which reconstructs the original forward, MSB-first bit order across byte boundaries. This
// package mirrors that scheme exactly (including using a plain string of '0'/'1' characters) for behavioral
// fidelity with the original implementation.
package bitreader

import (
	"fmt"
	"strconv"
)

// ReverseBitReader reads unsigned integers off the tail of a '0'/'1' bit string, moving the read position
// towards the front as bits are consumed.
type ReverseBitReader struct {
	bits string
	pos  int
}

// New creates a reader positioned at the end of bits (i.e. nothing has been read yet).
func New(bits string) *ReverseBitReader {
	return &ReverseBitReader{bits: bits, pos: len(bits)}
}

// ReadBool reads a single bit as a boolean.
func (r *ReverseBitReader) ReadBool() (bool, error) {
	v, err := r.ReadNumber(1)
	return v != 0, err
}

// ReadNumber reads length bits and returns them as an unsigned integer.
func (r *ReverseBitReader) ReadNumber(length int) (uint64, error) {
	if length == 0 {
		return 0, nil
	}
	if r.pos-length >= 0 {
		r.pos -= length
		v, err := strconv.ParseUint(r.bits[r.pos:r.pos+length], 2, 64)
		if err != nil {
			return 0, fmt.Errorf("bitreader: malformed bit string: %w", err)
		}
		return v, nil
	}
	r.pos = len(r.bits) + 1
	return 0, fmt.Errorf("bitreader: attempt to read past bitstring length")
}

// Pos returns the number of bits consumed so far.
func (r *ReverseBitReader) Pos() int { return len(r.bits) - r.pos }

// AbsolutePos returns the raw internal cursor (distance remaining from the front of the string).
func (r *ReverseBitReader) AbsolutePos() int { return r.pos }

// Skip advances the read position by length bits without decoding them.
func (r *ReverseBitReader) Skip(length int) error {
	if r.pos-length > 0 && r.pos-length <= len(r.bits) {
		r.pos -= length
		return nil
	}
	return fmt.Errorf("bitreader: attempt to skip past bitstring length")
}

// NotReadBits returns the still-unread portion of the bit string (nearest the front).
func (r *ReverseBitReader) NotReadBits() string { return r.bits[:r.pos] }
