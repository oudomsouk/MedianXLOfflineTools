package bitreader

import "testing"

func TestReadNumber(t *testing.T) {
	// "101" then "01100100" (=100) then "0" - read back MSB-first, tail-to-head, like the character stats
	// bitstream does.
	r := New("101" + "01100100" + "0")

	if v, err := r.ReadNumber(1); err != nil || v != 0 {
		t.Fatalf("ReadNumber(1) = %d, %v; want 0, nil", v, err)
	}
	if v, err := r.ReadNumber(8); err != nil || v != 100 {
		t.Fatalf("ReadNumber(8) = %d, %v; want 100, nil", v, err)
	}
	if v, err := r.ReadNumber(3); err != nil || v != 5 {
		t.Fatalf("ReadNumber(3) = %d, %v; want 5, nil", v, err)
	}
	if _, err := r.ReadNumber(1); err == nil {
		t.Fatal("expected error reading past the end of the bit string")
	}
}

func TestReadBool(t *testing.T) {
	r := New("10")
	if v, err := r.ReadBool(); err != nil || v != false {
		t.Fatalf("ReadBool() = %v, %v; want false, nil", v, err)
	}
	if v, err := r.ReadBool(); err != nil || v != true {
		t.Fatalf("ReadBool() = %v, %v; want true, nil", v, err)
	}
}

func TestPosAndSkip(t *testing.T) {
	r := New("11110000")
	if err := r.Skip(4); err != nil {
		t.Fatalf("Skip: %v", err)
	}
	if got := r.Pos(); got != 4 {
		t.Fatalf("Pos() = %d, want 4", got)
	}
	v, err := r.ReadNumber(4)
	if err != nil || v != 0b1111 {
		t.Fatalf("ReadNumber(4) = %d, %v; want 15, nil", v, err)
	}
}
