#include "characterfile.h"
#include "languagemanager.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QLocale>
#include <QTextStream>

namespace
{

QString dataPath(const QCoreApplication &app)
{
#ifdef DATA_PATH
    return DATA_PATH;
#else
    return QString("%1/resources").arg(app.applicationDirPath());
#endif
}

void printUsage(QTextStream &out, const QString &appName)
{
    out << "Usage:\n"
        << "  " << appName << " view <charfile.d2s>\n"
        << "  " << appName << " respec <stats|skills|both> <charfile.d2s> [--no-backup]\n";
}

int runView(QTextStream &out, QTextStream &err, const QString &path)
{
    CharacterFile file;
    QString error;
    if (!file.load(path, &error))
    {
        err << "Error: " << error << "\n";
        return 1;
    }
    out << file.summary();
    return 0;
}

int runRespec(QTextStream &out, QTextStream &err, const QString &mode, const QString &path, bool makeBackup)
{
    int targets = 0;
    if (mode == "stats")
        targets = CharacterFile::RespecStats;
    else if (mode == "skills")
        targets = CharacterFile::RespecSkills;
    else if (mode == "both")
        targets = CharacterFile::RespecStats | CharacterFile::RespecSkills;
    else
    {
        err << "Error: unknown respec target '" << mode << "', expected stats|skills|both\n";
        return 1;
    }

    CharacterFile file;
    QString error;
    if (!file.load(path, &error))
    {
        err << "Error: " << error << "\n";
        return 1;
    }

    file.respec(targets);

    QString backupPath;
    if (!file.save(path, makeBackup, &backupPath, &error))
    {
        err << "Error: " << error << "\n";
        return 1;
    }

    if (!backupPath.isEmpty())
        out << "Backup created: " << backupPath << "\n";
    out << "Respec (" << mode << ") applied to '" << path << "'\n";
    out << file.summary();
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("MedianXLOfflineTools");

    LanguageManager &langManager = LanguageManager::instance();
    langManager.currentLocale = QSettings().value(langManager.languageKey, QLocale::system().name().left(2)).toString();
    langManager.setResourcesPath(dataPath(app));

    QTextStream out(stdout);
    QTextStream err(stderr);

    QStringList args = app.arguments();
    QString appName = QFileInfo(args.value(0)).fileName();
    args.removeFirst();

    if (args.isEmpty())
    {
        printUsage(err, appName);
        return 1;
    }

    QString command = args.takeFirst();
    if (command == "view")
    {
        if (args.size() != 1)
        {
            printUsage(err, appName);
            return 1;
        }
        return runView(out, err, args.at(0));
    }
    else if (command == "respec")
    {
        if (args.size() < 2 || args.size() > 3)
        {
            printUsage(err, appName);
            return 1;
        }
        QString mode = args.at(0);
        QString path = args.at(1);
        bool makeBackup = true;
        if (args.size() == 3)
        {
            if (args.at(2) == "--no-backup")
                makeBackup = false;
            else
            {
                printUsage(err, appName);
                return 1;
            }
        }
        return runRespec(out, err, mode, path, makeBackup);
    }
    else
    {
        printUsage(err, appName);
        return 1;
    }
}
