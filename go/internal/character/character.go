// Package character ports src/characterfile.{h,cpp} (plus the bits of characterinfo.hpp it depends on):
// parsing, respeccing, and re-serializing the stats/skills section of a Median XL .d2s save file.
//
// Item bytes are never parsed here: everything from the first "JM" marker to the end of the save file is
// treated as an opaque blob and preserved byte-for-byte, exactly like the original.
package character

import (
	"fmt"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/oudomsouk/MedianXLOfflineTools/go/internal/bitreader"
	"github.com/oudomsouk/MedianXLOfflineTools/go/internal/enums"
	"github.com/oudomsouk/MedianXLOfflineTools/go/internal/itemdb"
	"github.com/oudomsouk/MedianXLOfflineTools/go/internal/resources"
)

// RespecTarget selects which part(s) of a character to respec; the values are bit flags so they can be
// combined, mirroring CharacterFile::RespecTarget.
type RespecTarget int

const (
	RespecStats  RespecTarget = 1 << 0
	RespecSkills RespecTarget = 1 << 1
)

const (
	kItemHeader      = "JM"
	kSkillsHeader    = "if"
	kIronGolemHeader = "kf"

	kStatPointsPerLevel        = 5
	kSkillPointsPerLevel       = 1
	kStatPointsPerLamEsensTome = 10

	kFileSignature = 0xAA55AA55
)

// statAtStart / statStep mirror BaseStats::StatsAtStart / StatsStep in structs.h.
type statAtStart struct{ strength, dexterity, vitality, energy, stamina int }
type statStep struct{ life, stamina, mana int }

// baseStats mirrors struct BaseStats: a class's starting stats and per-level/per-point stat gains.
type baseStats struct {
	atStart  statAtStart
	perLevel statStep
	perPoint statStep
}

func (s statAtStart) statFromCode(stat enums.Stat) int {
	switch stat {
	case enums.Strength:
		return s.strength
	case enums.Dexterity:
		return s.dexterity
	case enums.Vitality:
		return s.vitality
	case enums.Energy:
		return s.energy
	default:
		return 0
	}
}

// hardcodedBaseStats are Median XL's known default per-class values, used as a fallback when basestats.dat
// can't be read - mirrors the fallback table in characterfile.cpp's allBaseStats().
var hardcodedBaseStats = map[enums.ClassName]baseStats{
	enums.Amazon:      {statAtStart{25, 25, 20, 15, 84}, statStep{100, 40, 60}, statStep{8, 8, 18}},
	enums.Sorceress:   {statAtStart{10, 25, 15, 35, 74}, statStep{100, 40, 60}, statStep{8, 8, 18}},
	enums.Necromancer: {statAtStart{15, 25, 20, 25, 79}, statStep{80, 20, 80}, statStep{4, 8, 24}},
	enums.Paladin:     {statAtStart{25, 20, 25, 15, 89}, statStep{120, 60, 40}, statStep{12, 8, 12}},
	enums.Barbarian:   {statAtStart{30, 20, 30, 5, 92}, statStep{120, 60, 40}, statStep{12, 8, 12}},
	enums.Druid:       {statAtStart{25, 20, 15, 25, 84}, statStep{80, 20, 80}, statStep{4, 8, 24}},
	enums.Assassin:    {statAtStart{20, 35, 15, 15, 95}, statStep{100, 40, 60}, statStep{8, 8, 18}},
}

// statEntry mirrors one QVariant list stored per statistic in the original QMultiMap<StatisticEnum,
// QVariant>: either [value] for a plain statistic, or [param, value] for one with a save-time parameter
// (only ever used for Achievements in practice).
type statEntry []uint64

// basicInfo mirrors CharacterInfo::CharacterInfoBasic (trimmed to fields the CLI actually reads/writes).
type basicInfo struct {
	originalName     string
	classCode        enums.ClassName
	titleCode        uint8
	level            uint8
	isHardcore       bool
	hadDied          bool
	isLadder         bool
	totalSkillPoints uint16
}

// Character holds one loaded .d2s file's parsed state plus the raw bytes needed to re-serialize it,
// combining the responsibilities of the original CharacterFile and CharacterInfo classes (which, in the
// C++ version, was a global singleton - here it's just a regular value with no shared mutable state).
type Character struct {
	db  *itemdb.DB
	res *resources.Manager

	fileContents        []byte
	skillsZeroRequested bool

	basic basicInfo
	stats map[enums.Stat][]statEntry // front-of-slice == "most recently inserted" (QMultiMap semantics)

	skillsOffset int
	itemsOffset  int
}

// New creates a Character backed by the given resources manager (used to locate basestats.dat/props.dat/
// skills.dat).
func New(res *resources.Manager) *Character {
	return &Character{res: res, db: itemdb.New(res), stats: make(map[enums.Stat][]statEntry)}
}

// insertStat prepends an entry for stat, mirroring QMultiMap::insert (newest first).
func (c *Character) insertStat(stat enums.Stat, entry statEntry) {
	existing := c.stats[stat]
	updated := make([]statEntry, 0, len(existing)+1)
	updated = append(updated, entry.clone())
	updated = append(updated, existing...)
	c.stats[stat] = updated
}

func (e statEntry) clone() statEntry {
	out := make(statEntry, len(e))
	copy(out, e)
	return out
}

// valueOfStatistic mirrors CharacterInfo::valueOfStatistic: the first element of the most-recently-inserted
// entry for stat, or 0 if there is none.
func (c *Character) valueOfStatistic(stat enums.Stat) uint64 {
	entries := c.stats[stat]
	if len(entries) == 0 || len(entries[0]) == 0 {
		return 0
	}
	return entries[0][0]
}

// setValueForStatistic mirrors CharacterInfo::setValueForStatistic (QMultiMap::replace): overwrites only
// the most-recently-inserted entry for stat with a single-element [value], leaving any older entries for
// the same key (e.g. Achievements) untouched.
func (c *Character) setValueForStatistic(stat enums.Stat, value uint64) {
	entries := c.stats[stat]
	if len(entries) == 0 {
		c.stats[stat] = []statEntry{{value}}
		return
	}
	entries[0] = statEntry{value}
}

func binaryString(number uint64, fieldWidth int) string {
	s := strconv.FormatUint(number, 2)
	if len(s) < fieldWidth {
		s = strings.Repeat("0", fieldWidth-len(s)) + s
	}
	return s
}

func checksum(data []byte) uint32 {
	var sum uint32
	for i, b := range data {
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
	return sum
}

func totalPossibleStatPoints(level int, lamEsensTomeQuestsCompleted int, signetsOfLearningEaten uint64) int {
	return (level-1)*kStatPointsPerLevel + kStatPointsPerLamEsensTome*lamEsensTomeQuestsCompleted + int(signetsOfLearningEaten)
}

func totalPossibleSkillPoints(level int, doe, rad, iz int, signetsOfSkillEaten uint64) int {
	return (level-1)*kSkillPointsPerLevel + doe + rad + iz*2 + int(signetsOfSkillEaten)
}

// baseStatsForClass returns the per-class starting stats, preferring the mod's own basestats.dat and
// falling back to hardcodedBaseStats if that file can't be read - mirrors allBaseStats()/baseStatsForClass()
// in characterfile.cpp.
func (c *Character) baseStatsForClass(classCode enums.ClassName) baseStats {
	data, err := itemdb.DecompressedFileData(c.res.DataPath("basestats.dat"))
	if err == nil {
		for classID, bs := range parseBaseStats(data) {
			if enums.ClassName(classID) == classCode {
				return bs
			}
		}
	}
	return hardcodedBaseStats[classCode]
}

func parseBaseStats(data []byte) map[int]baseStats {
	result := make(map[int]baseStats)
	for _, line := range strings.Split(string(data), "\n") {
		if line == "" || line[0] == '#' {
			continue
		}
		fields := strings.Split(strings.TrimSpace(line), "\t")
		if len(fields) < 12 {
			continue
		}
		n := make([]int, 12)
		for i, f := range fields[:12] {
			n[i], _ = strconv.Atoi(f)
		}
		classID := n[0]
		// order is correct: energy value comes before vitality in the file
		result[classID] = baseStats{
			atStart:  statAtStart{strength: n[1], dexterity: n[2], vitality: n[4], energy: n[3], stamina: n[5]},
			perLevel: statStep{life: n[6], stamina: n[7], mana: n[8]},
			perPoint: statStep{life: n[9], stamina: n[10], mana: n[11]},
		}
	}
	return result
}

// Load reads and validates a .d2s save file, populating the Character's in-memory state.
func (c *Character) Load(path string) error {
	raw, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("opening file %q: %w", path, err)
	}
	c.fileContents = raw
	c.skillsZeroRequested = false
	c.stats = make(map[enums.Stat][]statEntry)

	if len(raw) < 4 {
		return fmt.Errorf("file too short to contain a valid signature")
	}
	signature := leUint32(raw, 0)
	if signature != kFileSignature {
		return fmt.Errorf("wrong file signature: should be 0x%X, got 0x%X", uint32(kFileSignature), signature)
	}

	if len(raw) < enums.OffsetChecksum+4 {
		return fmt.Errorf("file too short to contain a checksum")
	}
	fileChecksum := leUint32(raw, enums.OffsetChecksum)
	if fileChecksum != checksum(raw) {
		return fmt.Errorf("character checksum doesn't match. Looks like it's corrupted")
	}

	if len(raw) <= enums.OffsetName {
		return fmt.Errorf("file too short to contain a name")
	}
	c.basic.originalName = cString(raw[enums.OffsetName:])

	if len(raw) <= enums.OffsetLevel {
		return fmt.Errorf("file too short to contain basic status fields")
	}
	status := raw[enums.OffsetStatus]
	classCode := raw[enums.OffsetClass]
	skillsNumber := raw[enums.OffsetSkillsCount]
	clvl := raw[enums.OffsetLevel]

	if status&enums.StatusIsExpansion == 0 {
		return fmt.Errorf("this is not an Expansion character")
	}
	c.basic.isHardcore = status&enums.StatusIsHardcore != 0
	c.basic.hadDied = status&enums.StatusHadDied != 0
	c.basic.isLadder = status&enums.StatusIsLadder != 0

	if classCode > uint8(enums.Assassin) {
		return fmt.Errorf("wrong class value: got %d", classCode)
	}
	c.basic.classCode = enums.ClassName(classCode)

	progression := raw[enums.OffsetProgression]
	if progression >= enums.ProgressionCompleted {
		return fmt.Errorf("wrong progression value: got %d", progression)
	}
	c.basic.titleCode = progression

	if clvl == 0 || clvl > enums.MaxLevel {
		return fmt.Errorf("wrong level: got %d", clvl)
	}
	c.basic.level = clvl

	if len(raw) < enums.OffsetStatsHeader+2 || string(raw[enums.OffsetStatsHeader:enums.OffsetStatsHeader+2]) != "gf" {
		return fmt.Errorf("stats data not found")
	}

	skillsOffset := indexOf(raw, kSkillsHeader, enums.OffsetStatsData)
	if skillsOffset == -1 {
		return fmt.Errorf("skills data not found")
	}
	// apparently "if" can occur multiple times before items section, so we need the last occurrence before
	// skills data
	firstItemOffset := indexOf(raw, kItemHeader, skillsOffset)
	for skillsOffset != -1 && (firstItemOffset == -1 || skillsOffset < firstItemOffset) {
		c.skillsOffset = skillsOffset
		skillsOffset = indexOf(raw, kSkillsHeader, skillsOffset+1)
	}

	statsSize := c.skillsOffset - enums.OffsetStatsData
	if statsSize < 0 {
		return fmt.Errorf("stats data is corrupted")
	}

	// Build the reverse bit string exactly like the original: each byte (read in file order) is turned into
	// an 8-char '0'/'1' string and prepended, so the first file byte ends up at the tail.
	statsBitData := buildReversedBits(raw[enums.OffsetStatsData : enums.OffsetStatsData+statsSize])

	props, err := c.db.Properties()
	if err != nil {
		return err
	}

	const maxTries = 1000
	reader := bitreader.New(statsBitData)
	count := 0
	for ; count < maxTries; count++ {
		codeVal, err := reader.ReadNumber(enums.StatCodeLength)
		if err != nil {
			return fmt.Errorf("stats data is corrupted: %w", err)
		}
		statCode := enums.Stat(codeVal)
		if statCode == enums.End {
			break
		}

		prop := props[int(statCode)]
		statLength := 0
		if prop != nil {
			statLength = prop.BitsSave
		}
		if statLength == 0 {
			return fmt.Errorf("unknown statistic code found: %d. This is not a recognized character save (wrong mod data?)", statCode)
		}

		var entry statEntry
		if prop.ParamBitsSave != 0 {
			param, err := reader.ReadNumber(prop.ParamBitsSave)
			if err != nil {
				return fmt.Errorf("stats data is corrupted: %w", err)
			}
			entry = append(entry, param)
		}

		statValue, err := reader.ReadNumber(statLength)
		if err != nil {
			return fmt.Errorf("stats data is corrupted: %w", err)
		}
		if statCode == enums.Level && statValue != uint64(clvl) {
			statValue = uint64(clvl)
		} else if statCode >= enums.Life && statCode <= enums.BaseStamina {
			statValue >>= 8
		}

		entry = append(entry, statValue)
		c.insertStat(statCode, entry)
	}
	if count == maxTries {
		return fmt.Errorf("stats data is corrupted")
	}

	// skills
	var investedSkillPoints uint16
	skillsDataStart := c.skillsOffset + len(kSkillsHeader)
	if len(raw) < skillsDataStart+int(skillsNumber) {
		return fmt.Errorf("skills data is corrupted")
	}

	allSkills, err := c.db.Skills()
	if err != nil {
		return err
	}
	skillIndexes := itemdb.SkillIndexesForClass(allSkills, int(c.basic.classCode))

	for i := 0; i < int(skillsNumber); i++ {
		skillValue := raw[skillsDataStart+i]
		// Sigma 2.11 characters have "invisible" skills
		var skill *itemdb.Skill
		if i < len(skillIndexes) {
			skill = allSkills[skillIndexes[i]]
		}
		if skill != nil && skill.Tab > 0 {
			investedSkillPoints += uint16(skillValue)
		}
	}
	c.basic.totalSkillPoints = investedSkillPoints + uint16(c.valueOfStatistic(enums.FreeSkillPoints))

	// items are intentionally left completely unparsed: everything from here to the end of the file
	// (character items, corpse marker, mercenary items, and the Iron Golem item, if any) is treated as one
	// opaque blob and is preserved automatically, since only stats/skills are edited.
	charItemsOffset := skillsDataStart + int(skillsNumber)
	if charItemsOffset+len(kItemHeader) > len(raw) || string(raw[charItemsOffset:charItemsOffset+len(kItemHeader)]) != kItemHeader {
		return fmt.Errorf("items data not found")
	}
	c.itemsOffset = charItemsOffset + len(kItemHeader)

	return nil
}

// Respec applies the requested respec(s) to the in-memory character data. Call Load first.
func (c *Character) Respec(targets RespecTarget) {
	if targets&RespecStats != 0 {
		base := c.baseStatsForClass(c.basic.classCode)
		primaryStats := [4]enums.Stat{enums.Strength, enums.Dexterity, enums.Energy, enums.Vitality}

		investedDelta := 0
		for _, statCode := range primaryStats {
			currentValue := int(c.valueOfStatistic(statCode))
			baseValue := base.atStart.statFromCode(statCode)
			investedDelta += currentValue - baseValue
			c.setValueForStatistic(statCode, uint64(baseValue))
		}

		// The original stores/returns these as quint32 (32-bit), so arithmetic - including wraparound on a
		// corrupt/unusual save where invested delta is negative - happens at 32 bits, not 64.
		currentFreeStatPoints := uint32(c.valueOfStatistic(enums.FreeStatPoints))
		c.setValueForStatistic(enums.FreeStatPoints, uint64(currentFreeStatPoints+uint32(investedDelta)))
	}

	if targets&RespecSkills != 0 {
		c.setValueForStatistic(enums.FreeSkillPoints, uint64(c.basic.totalSkillPoints))
		c.skillsZeroRequested = true
	}
}

func addStatisticBits(bits *string, number uint64, fieldWidth int) {
	*bits = binaryString(number, fieldWidth) + *bits
}

// statisticBytes rebuilds the stats section's bytes from the in-memory statistic values, mirroring
// CharacterFile::statisticBytes.
func (c *Character) statisticBytes() ([]byte, error) {
	props, err := c.db.Properties()
	if err != nil {
		return nil, err
	}

	achievements := c.stats[enums.Achievements]
	achievementIndex := 0

	var result string
statLoop:
	for i := 0; i < len(enums.StatOrder); i++ {
		statCode := enums.StatOrder[i]
		isAchievement := false
		var value uint64

		switch statCode {
		case enums.Achievements:
			isAchievement = true
		case enums.End:
			addStatisticBits(&result, uint64(statCode), 16-len(result)%8)
			break statLoop
		default:
			value = c.valueOfStatistic(statCode)
			if statCode >= enums.Life && statCode <= enums.BaseStamina {
				value <<= 8
			}
		}

		if value != 0 || isAchievement {
			if !isAchievement || len(achievements) > 0 {
				addStatisticBits(&result, uint64(statCode), enums.StatCodeLength)
			}

			prop := props[int(statCode)]
			if isAchievement && len(achievements) > 0 {
				achData := achievements[achievementIndex]
				achievementIndex++
				if achievementIndex < len(achievements) {
					i--
				}
				addStatisticBits(&result, achData[0], prop.ParamBitsSave)
				value = achData[1]
			}
			if value != 0 {
				addStatisticBits(&result, value, prop.BitsSave)
			}
		}
	}

	bitsCount := len(result)
	if bitsCount%8 != 0 {
		return nil, fmt.Errorf("failed to rebuild stats data (internal error - stats string was not byte aligned)")
	}

	resultBytes := make([]byte, 0, bitsCount/8)
	for startPos := bitsCount - 8; startPos >= 0; startPos -= 8 {
		b, err := strconv.ParseUint(result[startPos:startPos+8], 2, 8)
		if err != nil {
			return nil, fmt.Errorf("failed to rebuild stats data: %w", err)
		}
		resultBytes = append(resultBytes, byte(b))
	}
	return resultBytes, nil
}

// Save rebuilds the stats/skills bytes, recomputes the checksum, optionally backs up the existing file, and
// writes the result to path. Returns the backup file path (empty if none was made).
func (c *Character) Save(path string, makeBackup bool) (backupPath string, err error) {
	tempFileContents := append([]byte(nil), c.fileContents...)

	statsBytes, err := c.statisticBytes()
	if err != nil {
		return "", err
	}

	tempFileContents = replaceBytes(tempFileContents, enums.OffsetStatsData, c.skillsOffset-enums.OffsetStatsData, statsBytes)
	diff := enums.OffsetStatsData + len(statsBytes) - c.skillsOffset
	c.skillsOffset = enums.OffsetStatsData + len(statsBytes)
	c.itemsOffset += diff

	if c.skillsZeroRequested {
		skillsBytesLength := c.itemsOffset - len(kItemHeader) - c.skillsOffset - len(kSkillsHeader)
		if skillsBytesLength > 0 {
			tempFileContents = replaceBytes(tempFileContents, c.skillsOffset+len(kSkillsHeader), skillsBytesLength, make([]byte, skillsBytesLength))
		}

		// drop the currently summoned Iron Golem item reference (if any), since the Golem skill will no
		// longer be usable; item bytes are otherwise left completely untouched (opaque blob, not decoded)
		golemHeaderPos := -1
		golemFlagPos := -1
		var golemFlag byte
		for attempts := 0; ; attempts++ {
			golemHeaderPos = lastIndexOf(tempFileContents, kIronGolemHeader, golemHeaderPos)
			if golemHeaderPos == -1 {
				break
			}
			golemFlagPos = golemHeaderPos + len(kIronGolemHeader)
			if golemFlagPos >= len(tempFileContents) {
				break
			}
			golemFlag = tempFileContents[golemFlagPos]

			done := attempts+1 == 3 ||
				(golemFlag == 0 && golemFlagPos == len(tempFileContents)-1) ||
				(golemFlag != 0 && golemFlagPos+1+len(kItemHeader) <= len(tempFileContents) &&
					string(tempFileContents[golemFlagPos+1:golemFlagPos+1+len(kItemHeader)]) == kItemHeader)
			if done {
				break
			}
		}
		if golemHeaderPos != -1 && golemFlag != 0 {
			tempFileContents[golemFlagPos] = 0
		}
	}

	sum := checksum(tempFileContents)
	putLeUint32(tempFileContents, enums.OffsetChecksum, sum)

	if makeBackup {
		if _, statErr := os.Stat(path); statErr == nil {
			timestamp := time.Now().Format("20060102-150405")
			candidateBackupPath := fmt.Sprintf("%s_%s.bak", path, timestamp)
			if copyErr := copyFile(path, candidateBackupPath); copyErr != nil {
				return "", fmt.Errorf("failed to create backup for %q: %w", path, copyErr)
			}
			backupPath = candidateBackupPath
		}
	}

	if err := os.WriteFile(path, tempFileContents, 0o644); err != nil {
		return "", fmt.Errorf("error opening file %q for writing: %w", path, err)
	}

	c.fileContents = tempFileContents
	return backupPath, nil
}

// Summary returns a human-readable summary of the currently loaded character.
func (c *Character) Summary() string {
	// questsInfo is never populated by this stripped-down build (quest data isn't parsed), so quest
	// completion counts are always 0 here - this mirrors the original's inherited behavior/limitation.
	const lamEsensTomeQuestsCompleted, denOfEvilQuestsCompleted, radamentQuestsCompleted, izualQuestsCompleted = 0, 0, 0, 0

	totalStats := totalPossibleStatPoints(int(c.basic.level), lamEsensTomeQuestsCompleted, c.valueOfStatistic(enums.SignetsOfLearningEaten))
	totalSkills := totalPossibleSkillPoints(int(c.basic.level), denOfEvilQuestsCompleted, radamentQuestsCompleted, izualQuestsCompleted, c.valueOfStatistic(enums.SignetsOfSkillEaten))

	mode := "Softcore"
	if c.basic.isHardcore {
		mode = "Hardcore"
		if c.basic.hadDied {
			mode += " (dead)"
		}
	}
	ladder := "no"
	if c.basic.isLadder {
		ladder = "yes"
	}

	var b strings.Builder
	fmt.Fprintf(&b, "Name:           %s\n", c.basic.originalName)
	fmt.Fprintf(&b, "Class:          %s\n", enums.ClassNames[c.basic.classCode])
	fmt.Fprintf(&b, "Level:          %d\n", c.basic.level)
	fmt.Fprintf(&b, "Mode:           %s\n", mode)
	fmt.Fprintf(&b, "Ladder:         %s\n", ladder)
	b.WriteString("\n")
	fmt.Fprintf(&b, "Strength:       %d\n", c.valueOfStatistic(enums.Strength))
	fmt.Fprintf(&b, "Dexterity:      %d\n", c.valueOfStatistic(enums.Dexterity))
	fmt.Fprintf(&b, "Vitality:       %d\n", c.valueOfStatistic(enums.Vitality))
	fmt.Fprintf(&b, "Energy:         %d\n", c.valueOfStatistic(enums.Energy))
	fmt.Fprintf(&b, "Free stats:     %d (of %d possible)\n", c.valueOfStatistic(enums.FreeStatPoints), totalStats)
	b.WriteString("\n")
	fmt.Fprintf(&b, "Invested skills:%d\n", int(c.basic.totalSkillPoints)-int(c.valueOfStatistic(enums.FreeSkillPoints)))
	fmt.Fprintf(&b, "Free skills:    %d (of %d possible)\n", c.valueOfStatistic(enums.FreeSkillPoints), totalSkills)
	return b.String()
}

// --- small helpers (byte/string plumbing with no direct Qt equivalent worth a whole package) ---

func leUint32(b []byte, offset int) uint32 {
	return uint32(b[offset]) | uint32(b[offset+1])<<8 | uint32(b[offset+2])<<16 | uint32(b[offset+3])<<24
}

func putLeUint32(b []byte, offset int, v uint32) {
	b[offset] = byte(v)
	b[offset+1] = byte(v >> 8)
	b[offset+2] = byte(v >> 16)
	b[offset+3] = byte(v >> 24)
}

// cString returns the NUL-terminated string starting at the beginning of b.
func cString(b []byte) string {
	n := 0
	for n < len(b) && b[n] != 0 {
		n++
	}
	return string(b[:n])
}

// indexOf finds the next occurrence of sep in data at or after from, or -1 if not found.
func indexOf(data []byte, sep string, from int) int {
	if from >= len(data) {
		return -1
	}
	idx := strings.Index(string(data[from:]), sep)
	if idx == -1 {
		return -1
	}
	return from + idx
}

// lastIndexOf finds the last occurrence of sep in data at or before from (from == -1 means "search the
// whole slice"), mirroring QByteArray::lastIndexOf.
func lastIndexOf(data []byte, sep string, from int) int {
	end := len(data)
	if from >= 0 {
		end = from + len(sep)
		if end > len(data) {
			end = len(data)
		}
	}
	return strings.LastIndex(string(data[:end]), sep)
}

// replaceBytes replaces length bytes starting at offset with replacement, growing/shrinking the slice as
// needed (mirroring QByteArray::replace(pos, len, after)).
func replaceBytes(data []byte, offset, length int, replacement []byte) []byte {
	out := make([]byte, 0, len(data)-length+len(replacement))
	out = append(out, data[:offset]...)
	out = append(out, replacement...)
	out = append(out, data[offset+length:]...)
	return out
}

func copyFile(src, dst string) error {
	data, err := os.ReadFile(src)
	if err != nil {
		return err
	}
	return os.WriteFile(dst, data, 0o644)
}

// buildReversedBits mirrors the load-time construction of the stats bit string: each byte of statsBytes (in
// file order) becomes an 8-char '0'/'1' string, prepended to the result, so the first byte ends up at the
// tail of the returned string.
func buildReversedBits(statsBytes []byte) string {
	var result string
	for _, b := range statsBytes {
		result = binaryString(uint64(b), 8) + result
	}
	return result
}
