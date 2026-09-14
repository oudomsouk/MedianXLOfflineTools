#ifndef HELPERS_H
#define HELPERS_H

// when building from visual studio
#ifndef IS_QT5
#define IS_QT5 (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
#endif

#include <QString>
#include <QDataStream>

// string building
static const QChar kZeroChar('0');
static const QString kHtmlLineBreak("<br />");
QString binaryStringFromNumber(quint64 number, bool needsInversion = false, int fieldWidth = 8);

// QMetaEnum getter. Moving definition to .cpp causes unresolved external symbols, so don't touch it.
#include <QMetaEnum>
template<class T>
QMetaEnum metaEnumFromName(const char *enumName)
{
    const QMetaObject &metaObject = T::staticMetaObject;
    return metaObject.enumerator(metaObject.indexOfEnumerator(enumName));
}

extern const QString modName;

void writeByteArrayDataWithoutNull(QDataStream &ds, const QByteArray &ba);

#endif // HELPERS_H
