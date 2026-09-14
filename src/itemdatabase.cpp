#include "itemdatabase.h"
#include "helpers.h"
#include "resourcepathmanager.hpp"

#include <QBuffer>
#include <QFile>

#ifndef QT_NO_DEBUG
#include <QDebug>
#endif

#define TO_UINT16(byteArray) (static_cast<quint8>(byteArray.at(0)) + (static_cast<quint8>(byteArray.at(1)) << 8))
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#define CRC_OF_BYTEARRAY(byteArray) qChecksum(byteArray) // qChecksum takes a single QByteArrayView starting with Qt6
#else
#define CRC_OF_BYTEARRAY(byteArray) qChecksum(byteArray.constData(), byteArray.length())
#endif


QHash<QString, quint32> ItemDataBase::tblIndexLookup;

QByteArray ItemDataBase::decompressedFileData(const QString &compressedFilePath, const QString &errorMessage)
{
    QFile f(compressedFilePath);
    if (!f.open(QIODevice::ReadOnly))
    {
        ERROR_BOX_NO_PARENT(errorMessage + "\n" + tr("Reason: %1").arg(f.errorString()));
        return QByteArray();
    }

    QByteArray compressedCrcData = f.read(2), originalCrcData = f.read(2);
    quint16 compressedCrc = TO_UINT16(compressedCrcData), originalCrc = TO_UINT16(originalCrcData);
    static const QString decompressError(tr("Error decrypting file '%1'"));
    QByteArray compressedDataFile = f.readAll();
    if (CRC_OF_BYTEARRAY(compressedDataFile) != compressedCrc)
    {
        ERROR_BOX_NO_PARENT(decompressError.arg(compressedFilePath));
        return QByteArray();
    }

    QByteArray originalFileData = qUncompress(compressedDataFile);
    if (CRC_OF_BYTEARRAY(originalFileData) != originalCrc)
    {
        ERROR_BOX_NO_PARENT(decompressError.arg(compressedFilePath));
        return QByteArray();
    }

    return originalFileData;
}

QHash<uint, ItemPropertyTxt *> *ItemDataBase::Properties()
{
    static QHash<uint, ItemPropertyTxt *> allProperties;
    if (allProperties.isEmpty())
    {
        QByteArray fileData = decompressedFileData(ResourcePathManager::localizedPathForFileName("props"), tr("Properties data not loaded."));
        if (fileData.isEmpty())
            return 0;

        QBuffer buf(&fileData);
        if (!buf.open(QIODevice::ReadOnly))
            return 0;
        while (!buf.atEnd())
        {
            QList<QByteArray> data = stringArrayOfCurrentLineInFile(buf);
            if (data.isEmpty())
                continue;

            ItemPropertyTxt *prop = new ItemPropertyTxt;
            prop->add = data.at(1).toUShort();
            prop->bits = data.at(2).toUShort();
            prop->paramBitsSave = data.at(3).toUShort();
            prop->bitsSave = data.at(4).toUShort();
            QList<QByteArray> groupIDs = data.at(5).split(',');
            if (!groupIDs.at(0).isEmpty())
                foreach (const QByteArray &id, groupIDs)
                    prop->groupIDs += id.toUShort();
            prop->descGroupNegative = QString::fromUtf8(data.at(6));
            prop->descGroupPositive = QString::fromUtf8(data.at(7));
            prop->descGroupStringAdd = QString::fromUtf8(data.at(8));
            prop->descNegative = QString::fromUtf8(data.at(9));
            prop->descPositive = QString::fromUtf8(data.at(10));
            prop->descStringAdd = QString::fromUtf8(data.at(11));
            prop->descFunc = data.at(12).toUShort();
            prop->descPriority = data.at(13).toUShort();
            prop->descVal = data.at(14).toUShort();
            prop->descGroupFunc = data.at(15).toUShort();
            prop->descGroupPriority = data.at(16).toUShort();
            prop->descGroupVal = data.at(17).toUShort();
            prop->paramBits = data.at(18).toUShort();
            prop->stat = data.at(19);
            allProperties[data.at(0).toUInt()] = prop;
        }
    }
    return &allProperties;
}

QList<SkillInfo *> *ItemDataBase::Skills()
{
    static QList<SkillInfo *> allSkills;
    if (allSkills.isEmpty())
    {
        QByteArray fileData = decompressedFileData(ResourcePathManager::localizedPathForFileName("skills"), tr("Skills data not loaded."));
        if (fileData.isEmpty())
            return 0;

        QBuffer buf(&fileData);
        if (!buf.open(QIODevice::ReadOnly))
            return 0;
        while (!buf.atEnd())
        {
            QList<QByteArray> data = stringArrayOfCurrentLineInFile(buf);
            if (data.isEmpty())
                continue;

            SkillInfo *skill = new SkillInfo;
            skill->name = QString::fromUtf8(data.at(1));
            skill->classCode = data.at(2).toShort();
            if (data.size() > 3)
            {
                skill->tab = data.at(3).toShort();
                skill->row = data.at(4).toShort();
                skill->col = data.at(5).toShort();
                skill->imageId = data.at(6).toUShort();
            }
            allSkills.push_back(skill);
        }
    }
    return &allSkills;
}

QString unquotedString(const QByteArray &ba)
{
    QString s = QString::fromUtf8(ba);
    return s.size() > 2 ? s.mid(1, s.size() - 2) : QString();
}

QHash<quint32, QString> *ItemDataBase::StringTable()
{
    static QHash<quint32, QString> strings;
    if (strings.isEmpty())
    {
        quint32 i = 0;
        foreach (QLatin1String tblName, QList<QLatin1String>() << QLatin1String("string") << QLatin1String("patchstring") << QLatin1String("expansionstring"))
        {
            QFile f(ResourcePathManager::localizedPathForFileName(tblName));
            if (!f.open(QIODevice::ReadOnly))
            {
                ERROR_BOX_NO_PARENT(tr("String table '%1' not loaded.").arg(tblName) + "\n" + tr("Reason: %1").arg(f.errorString()));
                return 0;
            }

            QByteArray fileData = f.readAll();
            QBuffer buf(&fileData);
            if (!buf.open(QIODevice::ReadOnly))
                return 0;

            quint32 j = i;
            while (!buf.atEnd())
            {
                QList<QByteArray> data = stringArrayOfCurrentLineInFile(buf);
                if (!data.isEmpty())
                {
                    strings[j] = data.size() > 1 ? unquotedString(data.at(1)) : QString();
                    tblIndexLookup[unquotedString(data.at(0))] = j;
                    ++j;
                }
            }

            i += 10000;
        }
    }
    return &strings;
}

QString ItemDataBase::stringFromTblKey(const QString &key)
{
    return key.isEmpty() ? QString() : StringTable()->value(tblIndexLookup.value(key));
}

QList<QByteArray> ItemDataBase::stringArrayOfCurrentLineInFile(QIODevice &d)
{
    bool isFirstPos = d.pos() == 0;
    QByteArray itemString = d.readLine().trimmed();
    return itemString.isEmpty() || (isFirstPos && itemString.startsWith('#')) ? QList<QByteArray>() : itemString.split('\t');
}
