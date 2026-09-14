package character

import (
	"encoding/binary"
	"os"
	"path/filepath"
	"testing"

	"github.com/oudomsouk/MedianXLOfflineTools/go/internal/enums"
	"github.com/oudomsouk/MedianXLOfflineTools/go/internal/itemdb"
	"github.com/oudomsouk/MedianXLOfflineTools/go/internal/resources"
)

func realResourcesManager(t *testing.T) *resources.Manager {
	t.Helper()
	// go test runs with cwd == package dir (internal/character); repo resources dir is 3 levels up.
	return resources.NewManager(filepath.Join("..", "..", "..", "resources"), "en")
}

// buildSyntheticSave assembles a minimal but structurally valid .d2s buffer (fixed header fields, real
// stats bytes produced by statisticBytes(), and a placeholder item section) so Load/Respec/Save can be
// exercised end-to-end without needing an actual game save file.
func buildSyntheticSave(t *testing.T, c *Character, classCode enums.ClassName, level uint8, skillValues []uint8) []byte {
	t.Helper()

	buf := make([]byte, enums.OffsetStatsData)
	binary.LittleEndian.PutUint32(buf[0:4], kFileSignature)
	copy(buf[enums.OffsetName:], "GoTest\x00")
	buf[enums.OffsetStatus] = enums.StatusIsExpansion
	buf[enums.OffsetProgression] = enums.ProgressionNormal
	buf[enums.OffsetClass] = byte(classCode)
	buf[enums.OffsetSkillsCount] = byte(len(skillValues))
	buf[enums.OffsetLevel] = level
	copy(buf[enums.OffsetStatsHeader:], "gf")

	statsBytes, err := c.statisticBytes()
	if err != nil {
		t.Fatalf("statisticBytes: %v", err)
	}
	buf = append(buf, statsBytes...)
	buf = append(buf, []byte(kSkillsHeader)...)
	buf = append(buf, skillValues...)
	buf = append(buf, []byte(kItemHeader)...)
	buf = append(buf, []byte{0, 0, 0, 0}...) // stand-in for the (unparsed) item bytes

	sum := checksum(buf)
	binary.LittleEndian.PutUint32(buf[enums.OffsetChecksum:], sum)
	return buf
}

func TestLoadSaveRoundTrip(t *testing.T) {
	res := realResourcesManager(t)
	setupChar := New(res)

	// Give the synthesized character a few nonzero primary stats plus free points, using the real
	// props.dat bit widths so the encode/decode is representative of an actual save.
	setupChar.insertStat(enums.Strength, statEntry{50})
	setupChar.insertStat(enums.Dexterity, statEntry{40})
	setupChar.insertStat(enums.Vitality, statEntry{60})
	setupChar.insertStat(enums.Energy, statEntry{30})
	setupChar.insertStat(enums.FreeStatPoints, statEntry{5})
	setupChar.insertStat(enums.FreeSkillPoints, statEntry{2})
	setupChar.insertStat(enums.Level, statEntry{10})

	db := itemdb.New(res)
	allSkills, err := db.Skills()
	if err != nil {
		t.Fatalf("Skills: %v", err)
	}
	amazonIndexes := itemdb.SkillIndexesForClass(allSkills, int(enums.Amazon))
	if len(amazonIndexes) == 0 {
		t.Fatal("expected at least one Amazon skill in the real skills.dat")
	}
	// find one spendable (tab > 0) skill slot to invest a point into
	spendableSlot := -1
	for i, idx := range amazonIndexes {
		if allSkills[idx].Tab > 0 {
			spendableSlot = i
			break
		}
	}
	if spendableSlot == -1 {
		t.Fatal("expected at least one spendable Amazon skill")
	}
	skillValues := make([]uint8, len(amazonIndexes))
	skillValues[spendableSlot] = 3

	raw := buildSyntheticSave(t, setupChar, enums.Amazon, 10, skillValues)

	dir := t.TempDir()
	path := filepath.Join(dir, "test.d2s")
	if err := os.WriteFile(path, raw, 0o644); err != nil {
		t.Fatalf("WriteFile: %v", err)
	}

	c := New(res)
	if err := c.Load(path); err != nil {
		t.Fatalf("Load: %v", err)
	}

	if c.basic.classCode != enums.Amazon {
		t.Errorf("classCode = %v, want Amazon", c.basic.classCode)
	}
	if c.basic.level != 10 {
		t.Errorf("level = %d, want 10", c.basic.level)
	}
	if got := c.valueOfStatistic(enums.Strength); got != 50 {
		t.Errorf("Strength = %d, want 50", got)
	}
	if got := c.valueOfStatistic(enums.Vitality); got != 60 {
		t.Errorf("Vitality = %d, want 60", got)
	}
	if got := c.basic.totalSkillPoints; got != 5 { // 3 invested + 2 free
		t.Errorf("totalSkillPoints = %d, want 5", got)
	}

	t.Logf("summary before respec:\n%s", c.Summary())

	c.Respec(RespecStats | RespecSkills)

	backupPath, err := c.Save(path, true)
	if err != nil {
		t.Fatalf("Save: %v", err)
	}
	if backupPath == "" {
		t.Error("expected a backup to be created for an existing file")
	}
	if _, err := os.Stat(backupPath); err != nil {
		t.Errorf("backup file missing: %v", err)
	}

	// reload to make sure the freshly written file is itself well-formed (valid checksum, parses cleanly)
	reloaded := New(res)
	if err := reloaded.Load(path); err != nil {
		t.Fatalf("reload after save: %v", err)
	}

	base := reloaded.baseStatsForClass(enums.Amazon)
	if got := reloaded.valueOfStatistic(enums.Strength); got != uint64(base.atStart.strength) {
		t.Errorf("post-respec Strength = %d, want base %d", got, base.atStart.strength)
	}
	if got := reloaded.valueOfStatistic(enums.FreeStatPoints); got != 5+(50-uint64(base.atStart.strength))+(40-uint64(base.atStart.dexterity))+(60-uint64(base.atStart.vitality))+(30-uint64(base.atStart.energy)) {
		t.Errorf("post-respec FreeStatPoints = %d", got)
	}
	if got := reloaded.valueOfStatistic(enums.FreeSkillPoints); got != 5 {
		t.Errorf("post-respec FreeSkillPoints = %d, want 5 (all skill points freed)", got)
	}

	t.Logf("summary after respec:\n%s", reloaded.Summary())
}
