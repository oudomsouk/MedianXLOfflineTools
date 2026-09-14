// Package enums mirrors the constant tables and small enum-like values from the original C++
// Enums namespace (src/enums.h / src/enums.cpp), trimmed down to what the CLI tool actually needs
// (item-related enums were dropped since items are never parsed).
package enums

// Byte offsets of fixed-position fields inside a .d2s save file.
const (
	OffsetFileSize        = 0x8
	OffsetChecksum        = 0xC
	OffsetName            = 0x14
	OffsetStatus          = 0x24
	OffsetProgression     = 0x25
	OffsetClass           = 0x28
	OffsetSkillsCount     = 0x2A
	OffsetLevel           = 0x2B
	OffsetSkillKeys       = 0x38
	OffsetCurrentLocation = 0xA8
	OffsetMercenary       = 0xB3
	OffsetQuestsHeader    = 0x14F
	OffsetQuestsData      = 0x159
	OffsetWaypointsHeader = 0x279
	OffsetWaypointsData   = 0x281
	OffsetNPCHeader       = 0x2CA
	OffsetStatsHeader     = 0x2FD
	OffsetStatsData       = 0x2FF
)

// Bit flags of the character status byte at OffsetStatus.
const (
	StatusIsHardcore  = 0x4
	StatusHadDied     = 0x8
	StatusIsExpansion = 0x20
	StatusIsLadder    = 0x40
)

// Progression (title) thresholds stored at OffsetProgression.
const (
	ProgressionNormal    = 0x4
	ProgressionNightmare = 0x9
	ProgressionHell      = 0xE
	ProgressionCompleted = 0x10
)

// ClassName is the character class code stored at OffsetClass.
type ClassName uint8

const (
	Amazon ClassName = iota
	Sorceress
	Necromancer
	Paladin
	Barbarian
	Druid
	Assassin
)

// ClassNames returns the display name for each ClassName value, in enum order (index == ClassName value).
var ClassNames = []string{"Amazon", "Sorceress", "Necromancer", "Paladin", "Barbarian", "Druid", "Assassin"}

// Stat is a character statistic code, as found in the stats bitstream (CharacterStats::StatisticEnum in the
// original C++). Values are not contiguous - unused codes are skipped/reserved by the mod's data.
type Stat int

const (
	Strength Stat = iota
	Energy
	Dexterity
	Vitality
	FreeStatPoints
	FreeSkillPoints
	Life
	BaseLife
	Mana
	BaseMana
	Stamina
	BaseStamina
	Level
	Experience
	InventoryGold
	StashGold
)

const (
	Achievements           Stat = 88
	SignetsOfLearningEaten Stat = 185
	SignetsOfSkillEaten    Stat = 186
	End                    Stat = 511
)

// Misc CharacterStats-related constants.
const (
	StatCodeLength = 9
	MaxLevel       = 150
)

// StatOrder mirrors the declaration order of CharacterStats::StatisticEnum as Qt's moc would enumerate it
// (Q_ENUMS iterates enumerators in source order). This order matters: it is the order in which stats are
// walked when rebuilding the stats bitstream on save.
var StatOrder = []Stat{
	Strength, Energy, Dexterity, Vitality, FreeStatPoints, FreeSkillPoints,
	Life, BaseLife, Mana, BaseMana, Stamina, BaseStamina,
	Level, Experience, InventoryGold, StashGold,
	Achievements, SignetsOfLearningEaten, SignetsOfSkillEaten, End,
}

// StatCodeLength-sized statistic constants used for a couple of quest checks.
const (
	KStatPointsPerLevel        = 5
	KSkillPointsPerLevel       = 1
	KStatPointsPerLamEsensTome = 10
)
