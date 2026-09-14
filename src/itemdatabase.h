#ifndef ITEMDATABASE_H
#define ITEMDATABASE_H

#include "structs.h"
#include "languagemanager.hpp"


class QFile;

// Only the character-stat/skill related tables are kept in this stripped-down build: everything item-specific
// (item bases, sets, uniques, runewords, socketables, item name/color formatting, storage helpers, etc.) was removed
// since the tool no longer parses or displays items.
class ItemDataBase
{
    Q_DECLARE_TR_FUNCTIONS(ItemDataBase)

public:
    static QByteArray decompressedFileData(const QString &compressedFilePath, const QString &errorMessage);

    static QHash<uint, ItemPropertyTxt *> *Properties();
    static QList<SkillInfo *> *Skills();

    static QHash<QString, quint32> tblIndexLookup;
    static QHash<quint32, QString> *StringTable();
    static QString stringFromTblKey(const QString &key);

private:
    static QList<QByteArray> stringArrayOfCurrentLineInFile(QIODevice &d);
};
#endif // ITEMDATABASE_H
