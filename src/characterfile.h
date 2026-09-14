#ifndef CHARACTERFILE_H
#define CHARACTERFILE_H

// GUI-independent extraction of the parsing/respec/serialization logic that used to live directly in
// MedianXLOfflineTools (medianxlofflinetools.cpp), so it can be reused by both the GUI app and the CLI tool.
// Item bytes are never parsed here: everything from the first "JM" marker to the end of the file is treated
// as an opaque blob and preserved automatically (see the comment in characterinfo.hpp for details).

#include "enums.h"

#include <QByteArray>
#include <QString>

class CharacterFile
{
public:
    enum RespecTarget
    {
        RespecStats = 1 << 0,
        RespecSkills = 1 << 1
    };

    // reads and validates the save file, populating CharacterInfo::instance()
    bool load(const QString &path, QString *error);

    // applies the requested respec(s) to the in-memory character data (call load() first)
    void respec(int targets);

    // rebuilds the stats/skills bytes, recomputes the checksum, optionally backs up the existing file, and
    // writes the result to 'path'. On success, 'backupPath' (if non-null) receives the backup file path, or
    // stays empty if no backup was made (either not requested or the target file didn't exist yet).
    bool save(const QString &path, bool makeBackup, QString *backupPath, QString *error);

    // human-readable summary of the currently loaded character (class, level, stats, points, etc.)
    QString summary() const;

private:
    QByteArray _fileContents;
    bool _skillsZeroRequested;

    QByteArray statisticBytes() const;
    static void addStatisticBits(QString &bitsString, quint64 number, int fieldWidth);
    static quint32 checksum(const QByteArray &data);
    static int totalPossibleStatPoints(int level, quint8 lamEsensTomeQuestsCompleted);
    static int totalPossibleSkillPoints(int level, quint8 doe, quint8 rad, quint8 iz);

public:
    CharacterFile() : _skillsZeroRequested(false) {}
};

#endif // CHARACTERFILE_H
