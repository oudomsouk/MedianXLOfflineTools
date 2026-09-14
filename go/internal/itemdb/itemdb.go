// Package itemdb ports the character-stat/skill-related tables from src/itemdatabase.{h,cpp}. Everything
// item-specific (item bases, sets, uniques, runewords, etc.) was already dropped from the original
// stripped-down C++ build, since this tool never parses item bytes - only the "props" (item/stat property
// definitions) and "skills" tables survive, because the stats bitstream parser needs them.
package itemdb

import (
	"bufio"
	"bytes"
	"fmt"
	"os"
	"strconv"
	"strings"
	"sync"

	"github.com/oudomsouk/MedianXLOfflineTools/go/internal/qtcompress"
	"github.com/oudomsouk/MedianXLOfflineTools/go/internal/resources"
)

// Property is the subset of the original ItemPropertyTxt struct actually used once items are unparsed:
// the number of bits a statistic's optional parameter and value occupy in the save file's bitstream.
type Property struct {
	ParamBitsSave int
	BitsSave      int
}

// Skill is the subset of the original SkillInfo struct used to figure out which of a character's saved
// skill slots actually correspond to a spendable (tab > 0) skill.
type Skill struct {
	ClassCode int
	Tab       int
	Row       int
	Col       int
}

// DB lazily loads and caches the props/skills tables for one resources.Manager, mirroring the
// function-local static caches of the original ItemDataBase class.
type DB struct {
	res *resources.Manager

	propsOnce sync.Once
	props     map[int]*Property
	propsErr  error

	skillsOnce sync.Once
	skills     []*Skill
	skillsErr  error
}

// New creates a DB backed by the given resources manager.
func New(res *resources.Manager) *DB {
	return &DB{res: res}
}

// DecompressedFileData reads and decompresses one of the mod's .dat resource files (2-byte compressed CRC,
// 2-byte original CRC, then a qCompress'd blob), mirroring ItemDataBase::decompressedFileData.
func DecompressedFileData(path string) ([]byte, error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("opening file %q: %w", path, err)
	}
	if len(raw) < 4 {
		return nil, fmt.Errorf("file %q is too short to be a valid resource file", path)
	}

	compressedCRC := uint16(raw[0]) | uint16(raw[1])<<8
	originalCRC := uint16(raw[2]) | uint16(raw[3])<<8
	compressedBlob := raw[4:]

	if qtcompress.Checksum(compressedBlob) != compressedCRC {
		return nil, fmt.Errorf("error decrypting file %q: compressed data checksum mismatch", path)
	}

	original, err := qtcompress.Uncompress(compressedBlob)
	if err != nil {
		return nil, fmt.Errorf("error decrypting file %q: %w", path, err)
	}
	if qtcompress.Checksum(original) != originalCRC {
		return nil, fmt.Errorf("error decrypting file %q: decompressed data checksum mismatch", path)
	}
	return original, nil
}

// tabSeparatedLines yields the tab-split fields of each non-empty, non-comment line of data, mirroring
// ItemDataBase::stringArrayOfCurrentLineInFile (a leading '#' only comments out the very first line).
func tabSeparatedLines(data []byte) [][]string {
	var lines [][]string
	scanner := bufio.NewScanner(bytes.NewReader(data))
	scanner.Buffer(make([]byte, 0, 64*1024), 1024*1024)
	first := true
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		isFirst := first
		first = false
		if line == "" || (isFirst && strings.HasPrefix(line, "#")) {
			continue
		}
		lines = append(lines, strings.Split(line, "\t"))
	}
	return lines
}

// Properties returns the property-code -> {paramBitsSave, bitsSave} table loaded from props.dat, caching
// the result. Only the two bit-width fields are kept; everything else in the original .dat (description
// strings, group ids, etc.) is unused once items are no longer parsed/rendered.
func (db *DB) Properties() (map[int]*Property, error) {
	db.propsOnce.Do(func() {
		data, err := DecompressedFileData(db.res.LocalizedPath("props"))
		if err != nil {
			db.propsErr = fmt.Errorf("properties data not loaded: %w", err)
			return
		}
		result := make(map[int]*Property)
		for _, fields := range tabSeparatedLines(data) {
			// columns: code, add, bits, paramBitsSave, bitsSave, groupIDs, ... (20 columns total upstream;
			// only the first 5 are needed here)
			if len(fields) < 5 {
				continue
			}
			code, err := strconv.Atoi(fields[0])
			if err != nil {
				continue
			}
			paramBitsSave, _ := strconv.Atoi(fields[3])
			bitsSave, _ := strconv.Atoi(fields[4])
			result[code] = &Property{ParamBitsSave: paramBitsSave, BitsSave: bitsSave}
		}
		db.props = result
	})
	return db.props, db.propsErr
}

// SkillIndexesForClass returns the indexes into skills belonging to classCode, in ascending (save-file)
// order. This mirrors the ".first" (file order) half of Skills::characterSkillsIndexes() in enums.cpp; the
// sorted "visual order" half (".second") was only used by the GUI's skill tree and isn't needed here.
func SkillIndexesForClass(skills []*Skill, classCode int) []int {
	var indexes []int
	for i, s := range skills {
		if s.ClassCode == classCode {
			indexes = append(indexes, i)
		}
	}
	return indexes
}

// Skills returns the ordered skill table loaded from skills.dat, caching the result. Order matters: a
// character's saved skill values are indexed positionally against this table (see
// Skills.CurrentCharacterSkillsIndexes in the enums package).
func (db *DB) Skills() ([]*Skill, error) {
	db.skillsOnce.Do(func() {
		data, err := DecompressedFileData(db.res.LocalizedPath("skills"))
		if err != nil {
			db.skillsErr = fmt.Errorf("skills data not loaded: %w", err)
			return
		}
		var result []*Skill
		for _, fields := range tabSeparatedLines(data) {
			// columns: id, name, classCode, tab, row, col, imageId
			if len(fields) < 3 {
				continue
			}
			classCode, _ := strconv.Atoi(fields[2])
			skill := &Skill{ClassCode: classCode}
			if len(fields) > 5 {
				skill.Tab, _ = strconv.Atoi(fields[3])
				skill.Row, _ = strconv.Atoi(fields[4])
				skill.Col, _ = strconv.Atoi(fields[5])
			}
			result = append(result, skill)
		}
		db.skills = result
	})
	return db.skills, db.skillsErr
}
