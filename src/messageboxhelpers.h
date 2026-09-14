#ifndef MESSAGEBOXHELPERS_H
#define MESSAGEBOXHELPERS_H

// QMessageBox-based UI helpers used only by GUI code. Deliberately kept out of helpers.h so that non-GUI
// consumers (e.g. itemdatabase.cpp, and by extension the headless CLI tool) don't drag in QtWidgets.
#include <QMessageBox>

#define CUSTOM_BOX(type, message, buttons, defaultButton) QMessageBox::type(this, qApp->applicationName(), message, buttons, defaultButton)
#define QUESTION_BOX_YESNO(message, defaultButton) CUSTOM_BOX(question, message, QMessageBox::Yes | QMessageBox::No, defaultButton)
#define CUSTOM_BOX_OK(type, message) CUSTOM_BOX(type, message, QMessageBox::Ok, QMessageBox::Ok)
#define ERROR_BOX(message) CUSTOM_BOX_OK(critical, message)
#define INFO_BOX(message) CUSTOM_BOX_OK(information, message)
#define WARNING_BOX(message) CUSTOM_BOX_OK(warning, message)
#define ERROR_BOX_NO_PARENT(message) QMessageBox::critical(0, qApp->applicationName(), message)

#endif // MESSAGEBOXHELPERS_H
