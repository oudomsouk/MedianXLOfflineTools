#include "helpers.h"

#include <QString>

#include <algorithm>


const QString modName(QChar(0x03A3));

QString binaryStringFromNumber(quint64 number, bool needsInversion /*= false*/, int fieldWidth /*= 8*/)
{
    QString binaryString = QString("%1").arg(number, fieldWidth, 2, kZeroChar);
    if (needsInversion)
        std::reverse(binaryString.begin(), binaryString.end());
    return binaryString;
}

void writeByteArrayDataWithoutNull(QDataStream &ds, const QByteArray &ba)
{
    ds.writeRawData(ba.constData(), ba.length());
}
