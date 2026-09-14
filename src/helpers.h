#ifndef HELPERS_H
#define HELPERS_H

// when building from visual studio
#ifndef IS_QT5
#define IS_QT5 (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
#endif

// message boxes
#include <QMessageBox>
#define CUSTOM_BOX(type, message, buttons, defaultButton) QMessageBox::type(this, qApp->applicationName(), message, buttons, defaultButton)
#define QUESTION_BOX_YESNO(message, defaultButton) CUSTOM_BOX(question, message, QMessageBox::Yes | QMessageBox::No, defaultButton)
#define CUSTOM_BOX_OK(type, message) CUSTOM_BOX(type, message, QMessageBox::Ok, QMessageBox::Ok)
#define ERROR_BOX(message) CUSTOM_BOX_OK(critical, message)
#define INFO_BOX(message) CUSTOM_BOX_OK(information, message)
#define WARNING_BOX(message) CUSTOM_BOX_OK(warning, message)
#define ERROR_BOX_NO_PARENT(message) QMessageBox::critical(0, qApp->applicationName(), message)

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
