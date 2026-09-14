#include "characterfile.h"
#include "characterinfo.hpp"
#include "itemdatabase.h"
#include "resourcepathmanager.hpp"
#include "reversebitreader.h"
#include "structs.h"

#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMetaEnum>

using namespace Enums;

namespace
{
const QByteArray kItemHeader("JM");
const QByteArray kSkillsHeader("if");
const QByteArray kIronGolemHeader("kf");

QString binaryStringFromNumber(quint64 number, int fieldWidth)
{
    return QString("%1").arg(number, fieldWidth, 2, QChar('0'));
}

// Per-class starting stats. Prefers the mod's basestats.dat (same source and format the GUI used to read via
// MedianXLOfflineTools::loadBaseStats()), falling back to Median XL's known default values if that file is
// missing so the tool still works against a bare checkout of the repo.
const QHash<ClassName::ClassNameEnum, BaseStats> &allBaseStats()
{
    static QHash<ClassName::ClassNameEnum, BaseStats> baseStatsMap;
    if (baseStatsMap.isEmpty())
    {
        QByteArray fileData = ItemDataBase::decompressedFileData(ResourcePathManager::dataPathForFileName("basestats.dat"), "Base stats data not loaded, using predefined one.");
        if (!fileData.isEmpty())
        {
            foreach (const QByteArray &s, fileData.split('\n'))
            {
                if (!s.isEmpty() && s.at(0) != '#')
                {
                    QList<QByteArray> numbers = s.trimmed().split('\t');
                    if (numbers.size() >= 12)
                        baseStatsMap[static_cast<ClassName::ClassNameEnum>(numbers.at(0).toUInt())] = BaseStats
                            (
                            // order is correct: energy value comes before vitality in the file
                            BaseStats::StatsAtStart (numbers.at(1).toInt(), numbers.at(2).toInt(),  numbers.at(4).toInt(), numbers.at(3).toInt(), numbers.at(5).toInt()),
                            BaseStats::StatsPerLevel(numbers.at(6).toInt(), numbers.at(7).toInt(),  numbers.at(8).toInt()),
                            BaseStats::StatsPerPoint(numbers.at(9).toInt(), numbers.at(10).toInt(), numbers.at(11).toInt())
                            );
                }
            }
        }

        if (baseStatsMap.isEmpty())
        {
            baseStatsMap[ClassName::Amazon]      = BaseStats(BaseStats::StatsAtStart(25, 25, 20, 15, 84), BaseStats::StatsPerLevel(100, 40, 60), BaseStats::StatsPerPoint( 8, 8, 18));
            baseStatsMap[ClassName::Sorceress]   = BaseStats(BaseStats::StatsAtStart(10, 25, 15, 35, 74), BaseStats::StatsPerLevel(100, 40, 60), BaseStats::StatsPerPoint( 8, 8, 18));
            baseStatsMap[ClassName::Necromancer] = BaseStats(BaseStats::StatsAtStart(15, 25, 20, 25, 79), BaseStats::StatsPerLevel( 80, 20, 80), BaseStats::StatsPerPoint( 4, 8, 24));
            baseStatsMap[ClassName::Paladin]     = BaseStats(BaseStats::StatsAtStart(25, 20, 25, 15, 89), BaseStats::StatsPerLevel(120, 60, 40), BaseStats::StatsPerPoint(12, 8, 12));
            baseStatsMap[ClassName::Barbarian]   = BaseStats(BaseStats::StatsAtStart(30, 20, 30,  5, 92), BaseStats::StatsPerLevel(120, 60, 40), BaseStats::StatsPerPoint(12, 8, 12));
            baseStatsMap[ClassName::Druid]       = BaseStats(BaseStats::StatsAtStart(25, 20, 15, 25, 84), BaseStats::StatsPerLevel( 80, 20, 80), BaseStats::StatsPerPoint( 4, 8, 24));
            baseStatsMap[ClassName::Assassin]    = BaseStats(BaseStats::StatsAtStart(20, 35, 15, 15, 95), BaseStats::StatsPerLevel(100, 40, 60), BaseStats::StatsPerPoint( 8, 8, 18));
        }
    }
    return baseStatsMap;
}

BaseStats baseStatsForClass(ClassName::ClassNameEnum classCode)
{
    return allBaseStats().value(classCode);
}

const int kStatPointsPerLevel = 5;
const int kSkillPointsPerLevel = 1;
const int kStatPointsPerLamEsensTome = 10;
const quint32 kFileSignature = 0xAA55AA55;
} // namespace

int CharacterFile::totalPossibleStatPoints(int level, quint8 lamEsensTomeQuestsCompleted)
{
    return (level - 1) * kStatPointsPerLevel + kStatPointsPerLamEsensTome * lamEsensTomeQuestsCompleted
         + CharacterInfo::instance().valueOfStatistic(CharacterStats::SignetsOfLearningEaten);
}

int CharacterFile::totalPossibleSkillPoints(int level, quint8 doe, quint8 rad, quint8 iz)
{
    return (level - 1) * kSkillPointsPerLevel + doe + rad + iz * 2
         + CharacterInfo::instance().valueOfStatistic(CharacterStats::SignetsOfSkillEaten);
}

quint32 CharacterFile::checksum(const QByteArray &data)
{
    quint32 sum = 0;
    for (int i = 0, n = data.size(); i < n; ++i)
    {
        bool mostSignificantByte = sum & 0x80000000;
        sum <<= 1;
        sum += mostSignificantByte;
        sum &= 0xFFFFFFFF;
        if (i < Offsets::Checksum || i >= Offsets::Checksum + 4) // bytes 12-15 - file checksum
            sum += static_cast<quint8>(data.at(i));
    }
    return sum;
}

bool CharacterFile::load(const QString &path, QString *error)
{
    QFile inputFile(path);
    if (!inputFile.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = QString("Error opening file '%1': %2").arg(path, inputFile.errorString());
        return false;
    }
    _fileContents = inputFile.readAll();
    inputFile.close();
    _skillsZeroRequested = false;

    QDataStream in(_fileContents);
    in.setByteOrder(QDataStream::LittleEndian);

    quint32 signature;
    in >> signature;
    if (signature != kFileSignature)
    {
        if (error)
            *error = QString("Wrong file signature: should be 0x%1, got 0x%2.").arg(kFileSignature, 0, 16).arg(signature, 0, 16);
        return false;
    }

    in.device()->seek(Offsets::Checksum);
    quint32 fileChecksum = 0, computedChecksum = checksum(_fileContents);
    in >> fileChecksum;
    if (fileChecksum != computedChecksum)
    {
        if (error)
            *error = "Character checksum doesn't match. Looks like it's corrupted.";
        return false;
    }

    CharacterInfo &charInfo = CharacterInfo::instance();
    charInfo.basicInfo.originalName = QString::fromLocal8Bit(_fileContents.constData() + Offsets::Name);
    charInfo.basicInfo.newName = charInfo.basicInfo.originalName;

    in.device()->seek(Offsets::Status);
    quint8 status, progression, classCode, clvl, skillsNumber;
    in >> status >> progression;
    in.device()->seek(Offsets::Class);
    in >> classCode;
    in.device()->seek(Offsets::SkillsCount);
    in >> skillsNumber;
    in.device()->seek(Offsets::Level);
    in >> clvl;

    if (!(status & StatusBits::IsExpansion))
    {
        if (error)
            *error = "This is not an Expansion character.";
        return false;
    }
    charInfo.basicInfo.isHardcore = status & StatusBits::IsHardcore;
    charInfo.basicInfo.hadDied = status & StatusBits::HadDied;
    charInfo.basicInfo.isLadder = status & StatusBits::IsLadder;

    if (classCode > ClassName::Assassin)
    {
        if (error)
            *error = QString("Wrong class value: got %1").arg(classCode);
        return false;
    }
    charInfo.basicInfo.classCode = static_cast<ClassName::ClassNameEnum>(classCode);

    if (progression >= Progression::Completed)
    {
        if (error)
            *error = QString("Wrong progression value: got %1").arg(progression);
        return false;
    }
    charInfo.basicInfo.titleCode = progression;

    if (!clvl || clvl > CharacterStats::MaxLevel)
    {
        if (error)
            *error = QString("Wrong level: got %1").arg(clvl);
        return false;
    }
    charInfo.basicInfo.level = clvl;

    if (_fileContents.mid(Offsets::StatsHeader, 2) != "gf")
    {
        if (error)
            *error = "Stats data not found!";
        return false;
    }
    in.device()->seek(Offsets::StatsData);

    int skillsOffset = _fileContents.indexOf(kSkillsHeader, Offsets::StatsData);
    if (skillsOffset == -1)
    {
        if (error)
            *error = "Skills data not found!";
        return false;
    }
    // apparently "if" can occur multiple times before items section, so we need the last occurrence before skills data
    int firstItemOffset = _fileContents.indexOf(kItemHeader, skillsOffset);
    while (skillsOffset != -1 && skillsOffset < firstItemOffset)
    {
        charInfo.skillsOffset = skillsOffset;
        skillsOffset = _fileContents.indexOf(kSkillsHeader, skillsOffset + 1);
    }

    int statsSize = charInfo.skillsOffset - Offsets::StatsData;
    QString statsBitData;
    statsBitData.reserve(statsSize * 8);
    for (int i = 0; i < statsSize; ++i)
    {
        quint8 aByte;
        in >> aByte;
        statsBitData.prepend(binaryStringFromNumber(aByte, 8));
    }

    charInfo.basicInfo.statsDynamicData.clear();

    int count = 0;
    const int maxTries = 1000;
    ReverseBitReader bitReader(statsBitData);
    for (; count < maxTries; ++count)
    {
        CharacterStats::StatisticEnum statCode = static_cast<CharacterStats::StatisticEnum>(bitReader.readNumber(CharacterStats::StatCodeLength));
        if (statCode == CharacterStats::End)
            break;

        ItemPropertyTxt *txtProp = ItemDataBase::Properties() ? ItemDataBase::Properties()->value(statCode) : 0;
        int statLength = txtProp ? txtProp->bitsSave : 0;
        if (!statLength)
        {
            if (error)
                *error = QString("Unknown statistic code found: %1. This is not a recognized character save (wrong mod data?).").arg(statCode);
            return false;
        }

        QList<QVariant> statData;
        if (txtProp->paramBitsSave)
            statData << bitReader.readNumber(txtProp->paramBitsSave);

        qint64 statValue = bitReader.readNumber(statLength);
        if (statCode == CharacterStats::Level && statValue != clvl)
            statValue = clvl;
        else if (statCode >= CharacterStats::Life && statCode <= CharacterStats::BaseStamina)
            statValue >>= 8;

        statData << statValue;
        charInfo.basicInfo.statsDynamicData.insert(statCode, statData);
    }
    if (count == maxTries)
    {
        if (error)
            *error = "Stats data is corrupted!";
        return false;
    }

    // skills
    quint16 investedSkillPoints = 0;
    charInfo.basicInfo.skills.clear();
    charInfo.basicInfo.skills.reserve(skillsNumber);

    in.skipRawData(kSkillsHeader.length());
    const Skills::SkillsOrderPair skillsIndexes = Skills::currentCharacterSkillsIndexes();
    QList<SkillInfo *> *allSkills = ItemDataBase::Skills();
    for (quint8 i = 0; i < skillsNumber; ++i)
    {
        quint8 skillValue;
        in >> skillValue;

        // Sigma 2.11 characters have "invisible" skills
        SkillInfo *skill = (allSkills && i < skillsIndexes.first.size()) ? allSkills->at(skillsIndexes.first.at(i)) : 0;
        if (skill && skill->tab > 0)
        {
            investedSkillPoints += skillValue;
            charInfo.basicInfo.skills += skillValue;
        }
    }
    charInfo.basicInfo.totalSkillPoints = investedSkillPoints + charInfo.valueOfStatistic(CharacterStats::FreeSkillPoints);

    // items are intentionally left completely unparsed: everything from here to the end of the file (character
    // items, corpse marker, mercenary items, and the Iron Golem item, if any) is treated as one opaque blob and
    // is preserved automatically, since only stats/skills are edited.
    int charItemsOffset = in.device()->pos();
    if (_fileContents.mid(charItemsOffset, kItemHeader.length()) != kItemHeader)
    {
        if (error)
            *error = "Items data not found!";
        return false;
    }
    charInfo.itemsOffset = charItemsOffset + kItemHeader.length();

    return true;
}

void CharacterFile::respec(int targets)
{
    CharacterInfo &charInfo = CharacterInfo::instance();

    if (targets & RespecStats)
    {
        BaseStats base = baseStatsForClass(charInfo.basicInfo.classCode);
        static const CharacterStats::StatisticEnum primaryStats[4] =
        {
            CharacterStats::Strength, CharacterStats::Dexterity, CharacterStats::Energy, CharacterStats::Vitality
        };

        int investedDelta = 0;
        for (int i = 0; i < 4; ++i)
        {
            CharacterStats::StatisticEnum statCode = primaryStats[i];
            int currentValue = charInfo.valueOfStatistic(statCode);
            int baseValue = base.statsAtStart.statFromCode(statCode);
            investedDelta += currentValue - baseValue;
            charInfo.setValueForStatistic(baseValue, statCode);
        }

        quint32 currentFreeStatPoints = charInfo.valueOfStatistic(CharacterStats::FreeStatPoints);
        charInfo.setValueForStatistic(currentFreeStatPoints + investedDelta, CharacterStats::FreeStatPoints);
    }

    if (targets & RespecSkills)
    {
        charInfo.setValueForStatistic(charInfo.basicInfo.totalSkillPoints, CharacterStats::FreeSkillPoints);
        _skillsZeroRequested = true;
    }
}

void CharacterFile::addStatisticBits(QString &bitsString, quint64 number, int fieldWidth)
{
    bitsString.prepend(binaryStringFromNumber(number, fieldWidth));
}

QByteArray CharacterFile::statisticBytes() const
{
    CharacterInfo &charInfo = CharacterInfo::instance();
    QString result;
    QMetaEnum statisticMetaEnum = CharacterStats::statisticMetaEnum();
    QList<QVariant> achievements = charInfo.basicInfo.statsDynamicData.values(CharacterStats::Achievements);

    for (int i = 0, achievementIndex = 0; i < statisticMetaEnum.keyCount(); ++i)
    {
        CharacterStats::StatisticEnum statCode = static_cast<CharacterStats::StatisticEnum>(statisticMetaEnum.value(i));
        bool isAchievement = false;
        quint64 value = 0;

        if (statCode == CharacterStats::Achievements)
        {
            isAchievement = true;
        }
        else if (statCode == CharacterStats::End)
        {
            addStatisticBits(result, statCode, 16 - result.length() % 8);
            break;
        }
        else
        {
            value = charInfo.valueOfStatistic(statCode);
            if (statCode >= CharacterStats::Life && statCode <= CharacterStats::BaseStamina)
                value <<= 8;
        }

        if (value || isAchievement)
        {
            if (!isAchievement || !achievements.isEmpty())
                addStatisticBits(result, statCode, CharacterStats::StatCodeLength);

            ItemPropertyTxt *txtProp = ItemDataBase::Properties()->value(statCode);
            if (isAchievement && !achievements.isEmpty())
            {
                QList<QVariant> achievementData = achievements.at(achievementIndex).toList();
                if (++achievementIndex < achievements.size())
                    --i;

                addStatisticBits(result, achievementData.at(0).toULongLong(), txtProp->paramBitsSave);
                value = achievementData.at(1).toULongLong();
            }
            if (value)
                addStatisticBits(result, value, txtProp->bitsSave);
        }
    }

    int bitsCount = result.length();
    if (bitsCount % 8)
        return QByteArray(); // stats string is not byte aligned - should never happen for a valid, unmodified format

    QByteArray resultBytes;
    resultBytes.reserve(bitsCount / 8);
    for (int startPos = bitsCount - 8; startPos >= 0; startPos -= 8)
    {
        quint8 aByte = result.mid(startPos, 8).toUShort(0, 2);
        resultBytes += aByte;
    }
    return resultBytes;
}

bool CharacterFile::save(const QString &path, bool makeBackup, QString *backupPath, QString *error)
{
    if (backupPath)
        backupPath->clear();

    CharacterInfo &charInfo = CharacterInfo::instance();
    QByteArray tempFileContents(_fileContents);

    QByteArray statsBytes = statisticBytes();
    if (statsBytes.isEmpty())
    {
        if (error)
            *error = "Failed to rebuild stats data (internal error - stats string was not byte aligned).";
        return false;
    }

    tempFileContents.replace(Offsets::StatsData, charInfo.skillsOffset - Offsets::StatsData, statsBytes);
    int diff = Offsets::StatsData + statsBytes.size() - charInfo.skillsOffset;
    charInfo.skillsOffset = Offsets::StatsData + statsBytes.size();
    charInfo.itemsOffset += diff;

    if (_skillsZeroRequested)
    {
        int skillsBytesLength = charInfo.itemsOffset - kItemHeader.length() - charInfo.skillsOffset - kSkillsHeader.length();
        tempFileContents.replace(charInfo.skillsOffset + kSkillsHeader.length(), skillsBytesLength, QByteArray(skillsBytesLength, 0));

        // drop the currently summoned Iron Golem item reference (if any), since the Golem skill will no longer
        // be usable; item bytes are otherwise left completely untouched (opaque blob, not decoded)
        int golemHeaderPos = -1, golemFlagPos = -1, attempts = 0;
        char golemFlag = 0;
        do
        {
            golemHeaderPos = tempFileContents.lastIndexOf(kIronGolemHeader, golemHeaderPos);
            if (golemHeaderPos == -1)
                break;
            golemFlagPos = golemHeaderPos + kIronGolemHeader.length();
            if (golemFlagPos >= tempFileContents.size())
                break;
            golemFlag = tempFileContents.at(golemFlagPos);
        }
        while (!(++attempts == 3 || (!golemFlag && golemFlagPos == tempFileContents.size() - 1) ||
                 (golemFlag && tempFileContents.mid(golemFlagPos + 1, kItemHeader.length()) == kItemHeader)));

        if (golemHeaderPos != -1 && golemFlag)
            tempFileContents[golemFlagPos] = 0;
    }

    quint32 sum = checksum(tempFileContents);
    QDataStream out(&tempFileContents, QIODevice::ReadWrite);
    out.setByteOrder(QDataStream::LittleEndian);
    out.device()->seek(Offsets::Checksum);
    out << sum;

    if (makeBackup)
    {
        QFile existingFile(path);
        if (existingFile.exists())
        {
            QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss");
            QString candidateBackupPath = QString("%1_%2.bak").arg(path, timestamp);
            if (!QFile::copy(path, candidateBackupPath))
            {
                if (error)
                    *error = QString("Failed to create backup for '%1': %2").arg(path, existingFile.errorString());
                return false;
            }
            if (backupPath)
                *backupPath = candidateBackupPath;
        }
    }

    QFile outputFile(path);
    if (!outputFile.open(QIODevice::WriteOnly))
    {
        if (error)
            *error = QString("Error opening file '%1' for writing: %2").arg(path, outputFile.errorString());
        return false;
    }
    outputFile.write(tempFileContents);
    outputFile.close();

    _fileContents = tempFileContents;
    return true;
}

QString CharacterFile::summary() const
{
    const CharacterInfo &charInfo = CharacterInfo::instance();
    const CharacterInfo::CharacterInfoBasic &basicInfo = charInfo.basicInfo;

    int totalStats = totalPossibleStatPoints(basicInfo.level, charInfo.questsInfo.lamEsensTomeQuestsCompleted());
    int totalSkills = totalPossibleSkillPoints(basicInfo.level, charInfo.questsInfo.denOfEvilQuestsCompleted(),
                                                charInfo.questsInfo.radamentQuestsCompleted(), charInfo.questsInfo.izualQuestsCompleted());

    QString result;
    result += QString("Name:           %1\n").arg(basicInfo.originalName);
    result += QString("Class:          %1\n").arg(ClassName::classes().value(basicInfo.classCode));
    result += QString("Level:          %1\n").arg(basicInfo.level);
    result += QString("Mode:           %1%2\n").arg(basicInfo.isHardcore ? "Hardcore" : "Softcore", basicInfo.isHardcore && basicInfo.hadDied ? " (dead)" : "");
    result += QString("Ladder:         %1\n").arg(basicInfo.isLadder ? "yes" : "no");
    result += "\n";
    result += QString("Strength:       %1\n").arg(charInfo.valueOfStatistic(CharacterStats::Strength));
    result += QString("Dexterity:      %1\n").arg(charInfo.valueOfStatistic(CharacterStats::Dexterity));
    result += QString("Vitality:       %1\n").arg(charInfo.valueOfStatistic(CharacterStats::Vitality));
    result += QString("Energy:         %1\n").arg(charInfo.valueOfStatistic(CharacterStats::Energy));
    result += QString("Free stats:     %1 (of %2 possible)\n").arg(charInfo.valueOfStatistic(CharacterStats::FreeStatPoints)).arg(totalStats);
    result += "\n";
    result += QString("Invested skills:%1\n").arg(basicInfo.totalSkillPoints - charInfo.valueOfStatistic(CharacterStats::FreeSkillPoints));
    result += QString("Free skills:    %1 (of %2 possible)\n").arg(charInfo.valueOfStatistic(CharacterStats::FreeSkillPoints)).arg(totalSkills);
    return result;
}
