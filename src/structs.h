#ifndef STRUCTS_H
#define STRUCTS_H

#include "enums.h"


// character

struct BaseStats
{
    struct StatsAtStart
    {
        qint32 strength, dexterity, vitality, energy;
        qint32 stamina;
        // life == vitality + 50
        // mana == energy

        StatsAtStart() {}
        StatsAtStart(qint32 str, qint32 d, qint32 v, qint32 e, qint32 sta) : strength(str), dexterity(d), vitality(v), energy(e), stamina(sta) {}

        qint32 statFromCode(Enums::CharacterStats::StatisticEnum statCode)
        {
            switch (statCode)
            {
            case Enums::CharacterStats::Strength:
                return strength;
            case Enums::CharacterStats::Dexterity:
                return dexterity;
            case Enums::CharacterStats::Vitality:
                return vitality;
            case Enums::CharacterStats::Energy:
                return energy;
            default:
                return 0;
            }
        }
    } statsAtStart;

    typedef struct StatsStep
    {
        qint32 life, stamina, mana; // divide by 4 and floor

        StatsStep() {}
        StatsStep(qint32 l, qint32 s, qint32 m) : life(l), stamina(s), mana(m) {}
    } StatsPerLevel, StatsPerPoint;

    StatsPerLevel statsPerLevel;
    StatsPerPoint statsPerPoint;

    BaseStats() {}
    BaseStats(StatsAtStart s, StatsStep l, StatsStep p) : statsAtStart(s), statsPerLevel(l), statsPerPoint(p) {}
};


// txt

struct ItemPropertyTxt
{
    quint16 add;
    quint8 bits, paramBits;
    quint8 bitsSave, paramBitsSave; // saveBits != 0 only for properties from Enums::CharacterStats::StatisticEnum
    QList<quint16> groupIDs;
    QString descGroupNegative, descGroupPositive, descGroupStringAdd;
    QString descNegative, descPositive, descStringAdd;
    quint8 descFunc, descPriority, descVal;
    quint8 descGroupFunc, descGroupPriority, descGroupVal;
    QByteArray stat;
};

struct SkillInfo
{
    QString name;
    qint8 classCode, tab, row, col;
    quint16 imageId;
};

typedef QList<quint8> SkillList;

#endif // STRUCTS_H
