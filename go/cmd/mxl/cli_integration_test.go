package main

import (
	"encoding/binary"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"github.com/oudomsouk/MedianXLOfflineTools/go/internal/enums"
	"github.com/oudomsouk/MedianXLOfflineTools/go/internal/itemdb"
	"github.com/oudomsouk/MedianXLOfflineTools/go/internal/resources"
)

// buildMxl compiles the CLI into a temp dir and returns the binary path.
func buildMxl(t *testing.T) string {
	t.Helper()
	dir := t.TempDir()
	bin := filepath.Join(dir, "mxl")
	cmd := exec.Command("go", "build", "-o", bin, ".")
	cmd.Dir = "."
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("go build: %v\n%s", err, out)
	}
	return bin
}

func runMxl(t *testing.T, bin, dataPath string, args ...string) (stdout, stderr string, exitCode int) {
	t.Helper()
	cmd := exec.Command(bin, args...)
	cmd.Env = append(os.Environ(), "DATA_PATH="+dataPath)
	var outBuf, errBuf strings.Builder
	cmd.Stdout = &outBuf
	cmd.Stderr = &errBuf
	err := cmd.Run()
	exitCode = 0
	if exitErr, ok := err.(*exec.ExitError); ok {
		exitCode = exitErr.ExitCode()
	} else if err != nil {
		t.Fatalf("running mxl: %v", err)
	}
	return outBuf.String(), errBuf.String(), exitCode
}

func TestCLIUsageErrors(t *testing.T) {
	bin := buildMxl(t)
	dataPath, err := filepath.Abs(filepath.Join("..", "..", "..", "resources"))
	if err != nil {
		t.Fatal(err)
	}

	cases := [][]string{
		{},
		{"bogus"},
		{"view"},
		{"respec", "bogus", "whatever.d2s"},
	}
	for _, args := range cases {
		_, stderr, code := runMxl(t, bin, dataPath, args...)
		if code != 1 {
			t.Errorf("args %v: exit code = %d, want 1", args, code)
		}
		if stderr == "" {
			t.Errorf("args %v: expected stderr output", args)
		}
	}
}

func TestCLIViewAndRespec(t *testing.T) {
	bin := buildMxl(t)
	dataPath, err := filepath.Abs(filepath.Join("..", "..", "..", "resources"))
	if err != nil {
		t.Fatal(err)
	}

	saveDir := t.TempDir()
	savePath := filepath.Join(saveDir, "char.d2s")
	writeSyntheticSave(t, savePath, dataPath)

	stdout, stderr, code := runMxl(t, bin, dataPath, "view", savePath)
	if code != 0 {
		t.Fatalf("view failed (code %d): %s", code, stderr)
	}
	if !strings.Contains(stdout, "Class:          Amazon") {
		t.Errorf("view output missing expected class line:\n%s", stdout)
	}
	if !strings.Contains(stdout, "Strength:       50") {
		t.Errorf("view output missing expected strength line:\n%s", stdout)
	}

	stdout, stderr, code = runMxl(t, bin, dataPath, "respec", "stats", savePath)
	if code != 0 {
		t.Fatalf("respec failed (code %d): %s", code, stderr)
	}
	if !strings.Contains(stdout, "Backup created:") {
		t.Errorf("expected a backup to be created by default:\n%s", stdout)
	}
	if !strings.Contains(stdout, "Strength:       25") { // Amazon base strength
		t.Errorf("respec output missing expected base strength:\n%s", stdout)
	}

	matches, _ := filepath.Glob(savePath + "_*.bak")
	if len(matches) != 1 {
		t.Errorf("expected exactly one backup file, found %d", len(matches))
	}

	// respec again with --no-backup: should not create a second backup
	_, stderr, code = runMxl(t, bin, dataPath, "respec", "skills", savePath, "--no-backup")
	if code != 0 {
		t.Fatalf("respec --no-backup failed (code %d): %s", code, stderr)
	}
	matches, _ = filepath.Glob(savePath + "_*.bak")
	if len(matches) != 1 {
		t.Errorf("--no-backup should not have created an extra backup, found %d", len(matches))
	}
}

// writeSyntheticSave builds a minimal but structurally valid .d2s file directly against the real
// resources/data props table (independently of the character package's own bit-encoding code, as a
// black-box cross-check that the CLI binary can parse it), and writes it to path.
func writeSyntheticSave(t *testing.T, path, dataPath string) {
	t.Helper()

	res := resources.NewManager(dataPath, "en")
	db := itemdb.New(res)
	props, err := db.Properties()
	if err != nil {
		t.Fatalf("Properties: %v", err)
	}

	binStr := func(n uint64, width int) string {
		s := ""
		for n > 0 {
			s = string(rune('0'+n&1)) + s
			n >>= 1
		}
		for len(s) < width {
			s = "0" + s
		}
		return s
	}

	// Encode Strength=50 and FreeStatPoints=5, then the End marker - mirroring
	// CharacterFile::statisticBytes()'s prepend order (later stats end up earlier in the byte stream).
	type stat struct {
		code  enums.Stat
		value uint64
	}
	statsToWrite := []stat{{enums.Strength, 50}, {enums.FreeStatPoints, 5}}

	bits := ""
	for _, s := range statsToWrite {
		prop := props[int(s.code)]
		if prop == nil {
			t.Fatalf("no property definition for stat %d", s.code)
		}
		bits = binStr(uint64(s.code), enums.StatCodeLength) + bits
		bits = binStr(s.value, prop.BitsSave) + bits
	}
	bits = binStr(uint64(enums.End), 16-len(bits)%8) + bits
	if len(bits)%8 != 0 {
		t.Fatalf("test bug: stat bits not byte aligned")
	}

	statBytes := make([]byte, 0, len(bits)/8)
	for start := len(bits) - 8; start >= 0; start -= 8 {
		b := byte(0)
		for _, ch := range bits[start : start+8] {
			b <<= 1
			if ch == '1' {
				b |= 1
			}
		}
		statBytes = append(statBytes, b)
	}

	buf := make([]byte, enums.OffsetStatsData)
	binary.LittleEndian.PutUint32(buf[0:4], 0xAA55AA55)
	copy(buf[enums.OffsetName:], "IntegrationTest\x00")
	buf[enums.OffsetStatus] = enums.StatusIsExpansion
	buf[enums.OffsetProgression] = enums.ProgressionNormal
	buf[enums.OffsetClass] = byte(enums.Amazon)
	buf[enums.OffsetSkillsCount] = 0
	buf[enums.OffsetLevel] = 10
	copy(buf[enums.OffsetStatsHeader:], "gf")
	buf = append(buf, statBytes...)
	buf = append(buf, []byte("if")...) // skills header, 0 skill bytes follow
	buf = append(buf, []byte("JM")...) // item header
	buf = append(buf, 0, 0, 0, 0)      // stand-in opaque item bytes

	var sum uint32
	for i, b := range buf {
		var msb uint32
		if sum&0x80000000 != 0 {
			msb = 1
		}
		sum <<= 1
		sum += msb
		if i < enums.OffsetChecksum || i >= enums.OffsetChecksum+4 {
			sum += uint32(b)
		}
	}
	binary.LittleEndian.PutUint32(buf[enums.OffsetChecksum:], sum)

	if err := os.WriteFile(path, buf, 0o644); err != nil {
		t.Fatalf("WriteFile: %v", err)
	}
}
