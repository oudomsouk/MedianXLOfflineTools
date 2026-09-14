#include "medianxlofflinetools.h"
#include "ui_medianxlofflinetools.h"
#include "colorsmanager.h"
#include "qd2charrenamer.h"
#include "helpers.h"
#include "messageboxhelpers.h"
#include "itemdatabase.h"
#include "resourcepathmanager.hpp"
#include "reversebitreader.h"
#include "characterinfo.hpp"
#include "fileassociationmanager.h"
#include "messagecheckbox.h"
#include "experienceindicatorgroupbox.h"
#include "skilltreedialog.h"

#include <QCloseEvent>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QGridLayout>
#include <QFileDialog>
#include <QLabel>
#include <QMimeData>
#include <QTextEdit>
#include <QActionGroup>
#include <QRegularExpression>

#include <QSettings>
#include <QFile>
#include <QDataStream>
#include <QTranslator>
#include <QUrl>
#include <QTimer>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QDesktopServices>

#include <QNetworkAccessManager>
#include <QNetworkReply>

#ifndef QT_NO_DEBUG
#include <QDebug>
#endif

#if IS_QT5
#include <QStandardPaths>
#endif

#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
#define HAS_QVERSIONNUMBER 1
#include <QVersionNumber>
#endif

#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
#define HAS_QOPERATINGSYSTEMVERSION 1
#include <QOperatingSystemVersion>
#else
#include <QSysInfo>
#endif

#include <cfloat>
#include <cmath>

#ifdef Q_OS_MAC
#include <QTextCodec>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
// qt_mac_set_dock_menu() is a private Qt5-only symbol; it was removed in Qt6, so this is a no-op there
static inline void qt_mac_set_dock_menu(QMenu *) {}
#else
extern void qt_mac_set_dock_menu(QMenu *);
#endif
#endif

//#define MAKE_HC
//#define ENABLE_PERSONALIZE
//#define MAKE_FINISHED_CHARACTER
//#define DISABLE_CRC_CHECK


static bool isVersionLess(const QString &v1, const QString &v2)
{
#if HAS_QVERSIONNUMBER
    return QVersionNumber::fromString(v1).normalized() < QVersionNumber::fromString(v2).normalized();
#else
    QList<QString> version1 = v1.split('.');
    QList<QString> version2 = v2.split('.');

    const int longest = qMax(version1.size(), version2.size());
    foreach (QList<QString> *version, QList<QList<QString> *>() << &version1 << &version2)
    {
        while (version->size() < longest)
            version->append(QLatin1String("0"));
    }

    if (version1 == version2)
        return false;
    for (int i = 0; i < longest; ++i)
    {
        if (version1[i].toUInt() > version2[i].toUInt())
            return false;
    }
    return true;
#endif
}


// static const

static const QString kLastSavePathKey("lastSavePath"), kBackupExtension("bak"), kReadonlyCss("QLineEdit { background-color: rgb(227, 227, 227) }"), kTimeFormatReadable("yyyyMMdd-hhmmss"), kMedianXlServer("http://mxl.vn.cz/kambala/");
static const QByteArray kItemHeader("JM"), kMercHeader("jf"), kSkillsHeader("if"), kIronGolemHeader("kf");

const QString MedianXLOfflineTools::kCompoundFormat("%1, %2");
const QString MedianXLOfflineTools::kCharacterExtension("d2s");
const QString MedianXLOfflineTools::kCharacterExtensionWithDot("." + kCharacterExtension);
const quint32 MedianXLOfflineTools::kFileSignature = 0xAA55AA55;
const int MedianXLOfflineTools::kDifficultiesNumber = 3;
const int MedianXLOfflineTools::kStatPointsPerLevel = 5;
const int MedianXLOfflineTools::kSkillPointsPerLevel = 1;
const int MedianXLOfflineTools::kStatPointsPerLamEsensTome = 10;
const int MedianXLOfflineTools::kMaxRecentFiles = 15;


// ctor

MedianXLOfflineTools::MedianXLOfflineTools(const QString &cmdPath, LaunchMode launchMode, QWidget *parent, Qt::WindowFlags flags) : QMainWindow(parent, flags), ui(new Ui::MedianXLOfflineToolsClass),
    _backupLimitsGroup(new QActionGroup(this)), _isLoaded(false), kHackerDetected(tr("1337 hacker detected! Please, play legit.")),
    maxValueFormat(tr("Max: %1")), minValueFormat(tr("Min: %1")), investedValueFormat(tr("Invested: %1")),
    kForumThreadHtmlLinks(QString("<a href=\"https://forum.median-xl.com/viewtopic.php?f=40&t=342\">%1</a><br><a href=\"http://worldofplayers.ru/threads/34489/\">%2</a>").arg(tr("Official Median XL Forum thread"), tr("Official Russian Median XL Forum thread"))),
    _fsWatcher(new QFileSystemWatcher(this)), _fileChangeTimer(0), _isFileChangedMessageBoxRunning(false)
{
    Q_UNUSED(launchMode);

    ui->setupUi(this);

    ui->actionBackups1->setData(1);
    ui->actionBackups2->setData(2);
    ui->actionBackups5->setData(5);
    ui->actionBackups10->setData(10);

    _backupLimitsGroup->setExclusive(true);
    _backupLimitsGroup->addAction(ui->actionBackups1);
    _backupLimitsGroup->addAction(ui->actionBackups2);
    _backupLimitsGroup->addAction(ui->actionBackups5);
    _backupLimitsGroup->addAction(ui->actionBackups10);
    _backupLimitsGroup->addAction(ui->actionBackupsUnlimited);

    ui->actionBackupFormatReadable ->setText(tr("<filename>_<%1>", "param is date format expressed in yyyy, MM, hh, etc.").arg(kTimeFormatReadable) + "." + kBackupExtension);
    ui->actionBackupFormatTimestamp->setText(tr("<filename>_<UNIX timestamp>") + "." + kBackupExtension);

    QActionGroup *backupFormatsGroup = new QActionGroup(this);
    backupFormatsGroup->setExclusive(true);
    backupFormatsGroup->addAction(ui->actionBackupFormatReadable);
    backupFormatsGroup->addAction(ui->actionBackupFormatTimestamp);

    ui->statsTableWidget->setFocusPolicy(Qt::NoFocus);


#ifdef Q_OS_WIN32
    setAppUserModelID(); // is actually used only in Windows 7 and later

#if HAS_QOPERATINGSYSTEMVERSION
    const bool atLeastWin8 = QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows8;
#else
    const bool atLeastWin8 = QSysInfo::windowsVersion() >= QSysInfo::WV_WINDOWS8;
#endif
    if (atLeastWin8)
    {
        delete ui->actionAssociate; ui->actionAssociate = 0;
        delete ui->actionCheckFileAssociations; ui->actionCheckFileAssociations = 0;
    }
    else
#endif
    {
        delete ui->actionOpenFileAssociationUI; ui->actionOpenFileAssociationUI = 0;
    }

    loadData();
    createLanguageMenu();
    createLayout();
    loadSettings();
    fillMaps();
    connectSignals();

#ifndef DUPE_CHECK
#if defined(Q_OS_WIN32) || defined(Q_OS_MAC)
#ifdef Q_OS_WIN32
    if (!atLeastWin8) // Windows 8 and later mustn't call this code
#endif
    {
        bool isDefault = FileAssociationManager::isApplicationDefaultForExtension(kCharacterExtensionWithDot);
        if (!isDefault)
        {
            if (ui->actionCheckFileAssociations->isChecked())
            {
                MessageCheckBox box(tr("%1 is not associated with %2 files.\n\nDo you want to do it?").arg(qApp->applicationName(), kCharacterExtensionWithDot), ui->actionCheckFileAssociations->text(), this);
                box.setChecked(true);
                if (box.exec())
                {
                    FileAssociationManager::makeApplicationDefaultForExtension(kCharacterExtensionWithDot);
                    isDefault = true;
                }
                ui->actionCheckFileAssociations->setChecked(box.isChecked());
            }
        }
        updateAssociateAction(isDefault);
    }
#else
#warning Add implementation to check file association to e.g. fileassociationmanager_linux.cpp
#endif
#endif

#ifdef Q_OS_WIN32
    syncWindowsTaskbarRecentFiles(); // is actually used only in Windows 7 and later
#endif

#ifndef DUPE_CHECK
    if (ui->actionCheckForUpdateOnStart->isChecked())
        checkForUpdate();
#endif

#ifdef Q_OS_MAC
    QTimer::singleShot(2000, this, SLOT(moveUpdateActionToAppleMenu())); // needs a slight delay to create menu
#endif

    if (!cmdPath.isEmpty())
        loadFile(cmdPath);
    else if (ui->actionLoadLastUsedCharacter->isChecked() && !_recentFilesList.isEmpty())
        loadFile(_recentFilesList.at(0));
    else
    {
#ifdef Q_OS_WIN32
# if IS_QT5
        const QString homeDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
# else
        const QString homeDir = QDesktopServices::storageLocation(QDesktopServices::HomeLocation);
# endif
        QSettings settings;
        settings.beginGroup("recentItems");
        settings.setValue(kLastSavePathKey, homeDir + QLatin1String("/AppData/Roaming/MedianXL/save"));
#endif
        updateWindowTitle();
    }
}

MedianXLOfflineTools::~MedianXLOfflineTools()
{
    delete ui;
}


// slots

bool MedianXLOfflineTools::loadFile(const QString &charPath, bool shouldCheckExtension /*= true*/, bool shouldOpenItemsWindow /*= true*/)
{
    Q_UNUSED(shouldOpenItemsWindow);

    bool unsupportedFile = !charPath.endsWith(kCharacterExtensionWithDot);
    if (charPath.isEmpty() || (shouldCheckExtension && unsupportedFile) || !maybeSave())
    {
        if (!charPath.isEmpty() && unsupportedFile)
            ERROR_BOX(tr("'%1' files are not supported", "param is file extension").arg(charPath.right(4)));
        return false;
    }

    // don't call slot a lot of times while loading character
    disconnect(ui->mercTypeComboBox);
    disconnect(ui->mercNameComboBox);

    _fsWatcher->removePaths(_fsWatcher->files());
    _charPath = charPath;

    bool result;
    if ((result = processSaveFile()))
    {
        if (shouldCheckExtension) // disable UI updates when checking for dupes
        {
            addToRecentFiles();
            updateUI();

            raise();
            activateWindow();
        }

        // it is here because currentIndexChanged signal is emitted when items are added to the combobox
        connect(ui->mercTypeComboBox, SIGNAL(currentIndexChanged(int)), SLOT(modify()));
        connect(ui->mercNameComboBox, SIGNAL(currentIndexChanged(int)), SLOT(modify()));

        QSettings settings;
        settings.beginGroup("recentItems");
        settings.setValue(kLastSavePathKey, QDir::toNativeSeparators(QFileInfo(_charPath).canonicalPath()));
    }
    else
    {
        _saveFileContents.clear();
        _charPath.clear();

        clearUI();
        updateWindowTitle();
    }

    setModified(false);
#ifdef MAKE_FINISHED_CHARACTER
    ui->actionSaveCharacter->setEnabled(true);
#endif

    return result;
}

void MedianXLOfflineTools::loadFileSkipExtensionCheck(const QString &charPath)
{
    loadFile(charPath, false);
}

void MedianXLOfflineTools::switchLanguage(QAction *languageAction)
{
    QByteArray newLocale = languageAction->statusTip().toLatin1();
    if (LanguageManager::instance().currentLocale != newLocale)
    {
        LanguageManager::instance().currentLocale = newLocale;

        static bool wasMessageBoxShown = false;
        if (!wasMessageBoxShown)
        {
            INFO_BOX(tr("Language will be changed next time you run the application"));
            wasMessageBoxShown = true;
        }
    }
}

void MedianXLOfflineTools::setModified(bool modified)
{
    setWindowModified(modified);
    ui->actionSaveCharacter->setEnabled(modified);
}

void MedianXLOfflineTools::loadCharacter()
{
    QSettings settings;
    settings.beginGroup("recentItems");
    loadSaveFile(QDir::toNativeSeparators(QFileDialog::getOpenFileName(this, tr("Load Character"), settings.value(kLastSavePathKey).toString(), tr("Diablo 2 Save Files") + QString(" (*%1)").arg(kCharacterExtensionWithDot))));
}

void MedianXLOfflineTools::openRecentFile()
{
    loadSaveFile(qobject_cast<QAction *>(sender())->statusTip());
}

void MedianXLOfflineTools::reloadCharacter(bool shouldNotify /*= true*/)
{
    loadSaveFile(_charPath, shouldNotify, tr("Character reloaded"));
}

void MedianXLOfflineTools::saveCharacter()
{
    CharacterInfo &charInfo = CharacterInfo::instance();
#ifdef MAKE_FINISHED_CHARACTER
    charInfo.basicInfo.level = Enums::CharacterStats::MaxLevel;
    ui->levelSpinBox->setMaximum(Enums::CharacterStats::MaxLevel);
    ui->levelSpinBox->setValue(Enums::CharacterStats::MaxLevel);
    ui->signetsOfLearningEatenLineEdit->setText(QString::number(Enums::CharacterStats::SignetsOfLearningMax));
    charInfo.setValueForStatistic(Enums::CharacterStats::SignetsOfLearningMax, Enums::CharacterStats::SignetsOfLearningEaten);
    charInfo.setValueForStatistic(Enums::CharacterStats::SignetsOfSkillMax, Enums::CharacterStats::SignetsOfSkillEaten);
    ui->freeSkillPointsLineEdit->setText(QString::number(totalPossibleSkillPoints(charInfo.basicInfo.level, kDifficultiesNumber, kDifficultiesNumber, kDifficultiesNumber)));
    ui->freeStatPointsLineEdit->setText(QString::number(totalPossibleStatPoints(charInfo.basicInfo.level, kDifficultiesNumber)));
#endif

    QByteArray tempFileContents(_saveFileContents);
    if (_isLoaded) // this portion is untouched when saving through command line
    {
        QByteArray statsBytes = statisticBytes();
        if (statsBytes.isEmpty())
            return;

        tempFileContents.replace(Enums::Offsets::StatsData, charInfo.skillsOffset - Enums::Offsets::StatsData, statsBytes);
        int diff = Enums::Offsets::StatsData + statsBytes.size() - charInfo.skillsOffset;
        charInfo.skillsOffset = Enums::Offsets::StatsData + statsBytes.size();
        charInfo.itemsOffset += diff;
    }

    if (ui->respecSkillsCheckBox->isChecked())
    {
        int skills = charInfo.itemsOffset - kItemHeader.length() - charInfo.skillsOffset - kSkillsHeader.length();
        tempFileContents.replace(charInfo.skillsOffset + kSkillsHeader.length(), skills, QByteArray(skills, 0));
    }

#ifndef MAKE_FINISHED_CHARACTER
    if (ui->activateWaypointsCheckBox->isChecked())
#endif
    {
        QByteArray activatedWaypointsBytes(22, 0xFF);
        for (int startPos = Enums::Offsets::WaypointsData + 2, i = 0; i < kDifficultiesNumber; ++i, startPos += 24)
            tempFileContents.replace(startPos, activatedWaypointsBytes.size(), activatedWaypointsBytes);
    }

    if (ui->convertToSoftcoreCheckBox->isChecked())
        charInfo.basicInfo.isHardcore = false;

#ifdef MAKE_HC
    charInfo.basicInfo.isHardcore = true;
    charInfo.basicInfo.hadDied = false;
#endif
#if !defined(QT_NO_DEBUG_OUTPUT) && !defined(DUPE_CHECK)
    charInfo.basicInfo.isLadder = !_makeNonLadderCheckbox->isChecked();
#endif
    char statusValue = tempFileContents.at(Enums::Offsets::Status);
    if (charInfo.basicInfo.hadDied)
        statusValue |= Enums::StatusBits::HadDied;
    else
        statusValue &= ~Enums::StatusBits::HadDied;
    if (charInfo.basicInfo.isHardcore)
        statusValue |= Enums::StatusBits::IsHardcore;
    else
        statusValue &= ~Enums::StatusBits::IsHardcore;
    if (charInfo.basicInfo.isLadder)
        statusValue |= Enums::StatusBits::IsLadder;
    else
        statusValue &= ~Enums::StatusBits::IsLadder;
    tempFileContents[Enums::Offsets::Status] = statusValue;

    QDataStream outputDataStream(&tempFileContents, QIODevice::ReadWrite);
    outputDataStream.setByteOrder(QDataStream::LittleEndian);

#ifdef MAKE_FINISHED_CHARACTER
    outputDataStream.skipRawData(Enums::Offsets::Progression);
    outputDataStream << static_cast<quint16>(15); // become (m|p)atriarch

    quint16 one = 1;
    // enable all acts in all difficulties
    for (int i = 0; i < kDifficultiesNumber; ++i)
    {
        outputDataStream.device()->seek(Enums::Offsets::QuestsData + i * Enums::Quests::Size); // A1 (0) //-V807
        outputDataStream << one;
        outputDataStream.skipRawData(6);  // Cain (8)
        outputDataStream << one;
        outputDataStream.skipRawData(4);  // A2 (14)
        outputDataStream << one;
        outputDataStream.skipRawData(14); // A3 (30)
        outputDataStream << one;
        outputDataStream.skipRawData(12); // Mephisto (44) + A4 (46)
        outputDataStream << one << one;
        outputDataStream.skipRawData(4);  // A4Q3 (52)
        outputDataStream << one;
        outputDataStream.skipRawData(2);  // A5 (56)
        outputDataStream << one;
    }

    // set current difficulty to Destruction and act to 5
    outputDataStream.device()->seek(Enums::Offsets::CurrentLocation);
    outputDataStream << static_cast<quint8>(0);   // Hatred
    outputDataStream << static_cast<quint8>(0);   // Terror
    outputDataStream << static_cast<quint8>(132); // Destruction (10000100)

    // complete skill/stat points quests
    quint16 questComplete = static_cast<quint16>(0) | Enums::Quests::IsCompleted;
    QList<int> quests = QList<int>() << Enums::Quests::DenOfEvil << Enums::Quests::Radament << Enums::Quests::LamEsensTome << Enums::Quests::GoldenBird << Enums::Quests::Izual;
    for (int i = 0; i < kDifficultiesNumber; ++i)
    {
        int baseOffset = Enums::Offsets::QuestsData + i * Enums::Quests::Size;
        foreach (int q, quests)
        {
            outputDataStream.device()->seek(baseOffset + q);
            outputDataStream << questComplete;
        }
    }
#endif

#ifdef ENABLE_PERSONALIZE
    for (int i = 0; i < kDifficultiesNumber; ++i)
    {
        outputDataStream.device()->seek(Enums::Offsets::QuestsData + i * Enums::Quests::Size + Enums::Quests::Nihlathak);
        outputDataStream << quint16(Enums::Quests::IsTaskDone);
    }
#endif

    QString newName = charInfo.basicInfo.newName;
    bool hasNameChanged = !newName.isEmpty() && charInfo.basicInfo.originalName != newName;
    if (hasNameChanged)
    {
        outputDataStream.device()->seek(Enums::Offsets::Name);
#ifdef Q_OS_MAC
        QByteArray newNameByteArray = ColorsManager::macTextCodec()->fromUnicode(newName);
#else
        QByteArray newNameByteArray = newName.toLocal8Bit();
#endif
        newNameByteArray += QByteArray(QD2CharRenamer::kMaxNameLength + 1 - newName.length(), '\0'); // add trailing nulls
        writeByteArrayDataWithoutNull(outputDataStream, newNameByteArray);
    }
    else
        newName = charInfo.basicInfo.originalName;

    if (_isLoaded) // ui is inaccessible otherwise
    {
        quint8 newClvl = ui->levelSpinBox->value();
#ifndef MAKE_FINISHED_CHARACTER
        if (charInfo.basicInfo.level != newClvl)
#endif
        {
            charInfo.basicInfo.level = newClvl;
            charInfo.basicInfo.totalSkillPoints = ui->freeSkillPointsLineEdit->text().toUShort();
            recalculateStatPoints();

            outputDataStream.device()->seek(Enums::Offsets::Level);
            outputDataStream << newClvl;

            ui->levelSpinBox->setMaximum(newClvl);
        }

        if (charInfo.mercenary.exists)
        {
            quint16 newMercValue = Enums::Mercenary::mercBaseValueFromCode(charInfo.mercenary.code) + ui->mercTypeComboBox->currentIndex();
            charInfo.mercenary.code = Enums::Mercenary::mercCodeFromValue(newMercValue);
            charInfo.mercenary.nameIndex = ui->mercNameComboBox->currentIndex();
            outputDataStream.device()->seek(Enums::Offsets::Mercenary + 4);
            outputDataStream << charInfo.mercenary.nameIndex << newMercValue;
        }
    }

    // when respeccing skills, drop the currently summoned Iron Golem item (if any), since the Golem skill will
    // no longer be usable; item bytes are otherwise left completely untouched (opaque blob, not decoded)
    if (ui->respecSkillsCheckBox->isChecked())
    {
        int golemHeaderPos = -1, golemFlagPos, attempts = 0;
        char golemFlag;
        do
        {
            golemHeaderPos = tempFileContents.lastIndexOf(kIronGolemHeader, golemHeaderPos);
            golemFlagPos = golemHeaderPos + kIronGolemHeader.length();
            golemFlag = tempFileContents.at(golemFlagPos);
        } while (!(++attempts == 3 || (!golemFlag && golemFlagPos == tempFileContents.size() - 1) || (golemFlag && tempFileContents.mid(golemFlagPos + 1, kItemHeader.length()) == kItemHeader)));

        if (golemHeaderPos != -1 && golemFlag)
        {
            tempFileContents[golemFlagPos] = 0;
            tempFileContents.truncate(golemFlagPos + 1);
        }
    }

    // write file size & checksum
    quint32 fileSize = tempFileContents.size();
    outputDataStream.device()->seek(Enums::Offsets::FileSize);
    outputDataStream << fileSize;
    outputDataStream << checksum(tempFileContents);

    _fsWatcher->removePaths(_fsWatcher->files());

    QStringList backupedFiles;

    // save the character
    QString savePath, fileName, saveFileName;
    QFile outputFile;
    if (_isLoaded)
    {
        QSettings settings;
        settings.beginGroup("recentItems");
        savePath = settings.value(kLastSavePathKey).toString() + QDir::separator();
        fileName = savePath + newName;

        saveFileName = fileName;
        if (_charPath.endsWith(QLatin1String(".d2s"), Qt::CaseInsensitive))
            saveFileName += kCharacterExtensionWithDot;
        outputFile.setFileName(saveFileName);

        if (hasNameChanged)
        {
            QFile oldFile(QString("%1%2.%3").arg(savePath, charInfo.basicInfo.originalName, kCharacterExtension));
            backupedFiles += backupFile(oldFile);
        }
        else
            backupedFiles += backupFile(outputFile);
    }
    else
        outputFile.setFileName(_charPath);

    if (outputFile.open(QIODevice::WriteOnly))
    {
        int bytesWritten = outputFile.write(tempFileContents);
        if (bytesWritten == static_cast<int>(fileSize)) // shut the compiler up
        {
            outputFile.flush();
            outputFile.close(); // without this explicit call QFileSystemWatcher will report the file as modified after the method returns
            if (!_isLoaded)
                return;
            _saveFileContents = tempFileContents;

            if (hasNameChanged)
            {
                // delete .d2s and rename all other related files like .d2x, .key, .ma0, etc.
                QRegularExpression emptyNameRegex(QRegularExpression::anchoredPattern(QString("[ %1]+").arg(QChar(QChar::Nbsp))));
                bool isOldNameEmpty = emptyNameRegex.match(charInfo.basicInfo.originalName).hasMatch();
                bool hasNonAsciiChars = false;
                for (int i = 0; i < charInfo.basicInfo.originalName.length(); ++i)
                {
                    if (charInfo.basicInfo.originalName.at(i).unicode() > 255)
                    {
                        hasNonAsciiChars = true;
                        break;
                    }
                }

                bool isStrangeName = hasNonAsciiChars || isOldNameEmpty;
                QDir sourceFileDir(savePath, isStrangeName ? "*" : charInfo.basicInfo.originalName + ".*");
                foreach (const QFileInfo &fileInfo, sourceFileDir.entryInfoList())
                {
                    QString extension = fileInfo.suffix();
                    if ((isStrangeName && fileInfo.baseName() != charInfo.basicInfo.originalName) || extension == kBackupExtension)
                        continue;

                    QFile sourceFile(fileInfo.canonicalFilePath());
                    if (extension == kCharacterExtension) // delete
                    {
                        if (!sourceFile.remove())
                            showErrorMessageBoxForFile(tr("Error removing file '%1'"), sourceFile);
                    }
                    else // rename
                    {
                        if (!sourceFile.rename(fileName + "." + extension) && !isOldNameEmpty)
                            showErrorMessageBoxForFile(tr("Error renaming file '%1'"), sourceFile);
                    }
                }

                _charPath = saveFileName;
                charInfo.basicInfo.originalName = newName;

#ifdef Q_OS_WIN32
                removeFromWindowsRecentFiles(_recentFilesList.at(0)); // old file doesn't exist any more
#endif
                _recentFilesList[0] = saveFileName;
                updateRecentFilesActions();
            }

            setModified(false);
            loadFile(_charPath, true, false); // update all UI at once by reloading the file

            QString text = tr("File '%1' successfully saved!").arg(QDir::toNativeSeparators(saveFileName));
            if (!backupedFiles.isEmpty())
                text += kHtmlLineBreak + kHtmlLineBreak + tr("The following backups were created:") + QString("<ul><li>%1</li></ul>").arg(backupedFiles.join("</li><li>"));
            INFO_BOX(text);
        }
        else
            showErrorMessageBoxForFile(tr("Error writing file '%1'"), outputFile);
    }
    else
        showErrorMessageBoxForFile(tr("Error creating file '%1'"), outputFile);
}

#ifdef DUPE_CHECK
void MedianXLOfflineTools::showDupeCheck()
{
}
#endif

void MedianXLOfflineTools::statChanged(int newValue)
{
    QSpinBox *senderSpinBox = qobject_cast<QSpinBox *>(sender());
    int minimum = senderSpinBox->minimum();
    updateMinCompoundStatusTip(senderSpinBox, minimum, newValue - minimum);

    int *pOldValue = &_oldStatValues[_spinBoxesStatsMap.key(senderSpinBox)], diff = newValue - *pOldValue;
    if (_isLoaded && diff)
    {
        *pOldValue = newValue;

        ui->freeStatPointsLineEdit->setText(QString::number(ui->freeStatPointsLineEdit->text().toUInt() - diff));
        QString s = ui->freeStatPointsLineEdit->statusTip();
        int start = s.indexOf(": ") + 2, end = s.indexOf(","), total = s.mid(start, end - start).toInt();
        updateMaxCompoundStatusTip(ui->freeStatPointsLineEdit, total, investedStatPoints());

        foreach (QSpinBox *spinBox, _spinBoxesStatsMap)
            if (spinBox != senderSpinBox)
                spinBox->setMaximum(spinBox->maximum() - diff);
        // explicitly update status bar
        QStatusTipEvent event(senderSpinBox->statusTip());
        qApp->sendEvent(senderSpinBox, &event);

        updateTableStats(_baseStatsMap[CharacterInfo::instance().basicInfo.classCode].statsPerPoint, diff, senderSpinBox);
        setModified(true);
    }
}

void MedianXLOfflineTools::respecStats()
{
    for (int i = Enums::CharacterStats::Strength; i <= Enums::CharacterStats::Vitality; ++i)
    {
        Enums::CharacterStats::StatisticEnum statCode = static_cast<Enums::CharacterStats::StatisticEnum>(i);
        int baseStat = _baseStatsMap[CharacterInfo::instance().basicInfo.classCode].statsAtStart.statFromCode(statCode);
        _spinBoxesStatsMap[statCode]->setValue(baseStat);
    }
    ui->statusBar->clearMessage();
    setModified(true);
}

void MedianXLOfflineTools::respecSkills(bool shouldRespec)
{
    quint16 skills = CharacterInfo::instance().basicInfo.totalSkillPoints;
    quint32 freeSkills = CharacterInfo::instance().valueOfStatistic(Enums::CharacterStats::FreeSkillPoints);
    ui->freeSkillPointsLineEdit->setText(QString::number(shouldRespec ? skills : freeSkills));
    updateMaxCompoundStatusTip(ui->freeSkillPointsLineEdit, skills, shouldRespec ? 0 : skills - freeSkills);
    setModified(true);
}

void MedianXLOfflineTools::rename()
{
    QString &newName = CharacterInfo::instance().basicInfo.newName;
    QD2CharRenamer renameWidget(newName, ui->actionWarnWhenColoredName->isChecked(), this);
    if (renameWidget.exec())
    {
        newName = renameWidget.name();
        QD2CharRenamer::updateNamePreviewLabel(ui->charNamePreviewLabel, newName);
        setModified(true);

        ui->actionWarnWhenColoredName->setChecked(renameWidget.shouldWarnAboutColor());
    }
}

void MedianXLOfflineTools::levelChanged(int newClvl)
{
    int lvlDiff = _oldClvl - newClvl;
    const CharacterInfo::CharacterInfoBasic &basicInfo = CharacterInfo::instance().basicInfo;
    if (_isLoaded && qAbs(lvlDiff) < basicInfo.level)
    {
        updateTableStats(_baseStatsMap[basicInfo.classCode].statsPerLevel, -lvlDiff);

        _oldClvl = newClvl;
        int statsDiff = lvlDiff * kStatPointsPerLevel, newFreeStats = ui->freeStatPointsLineEdit->text().toInt() - statsDiff;
        if (newFreeStats < 0)
        {
            respecStats();
            newFreeStats = ui->freeStatPointsLineEdit->text().toInt() - statsDiff;
        }
        ui->freeStatPointsLineEdit->setText(QString::number(newFreeStats));
        foreach (QSpinBox *spinBox, _spinBoxesStatsMap)
            spinBox->setMaximum(spinBox->maximum() - statsDiff);

        bool hasLevelChanged = newClvl != basicInfo.level;
        if (_resurrectionPenalty != ResurrectPenaltyDialog::Skills)
        {
            ui->respecSkillsCheckBox->setChecked(hasLevelChanged);
            ui->respecSkillsCheckBox->setDisabled(hasLevelChanged);
        }

        int newSkillPoints = basicInfo.totalSkillPoints, investedSkillPoints = 0;
        if (hasLevelChanged)
        {
            newSkillPoints -= (basicInfo.level - newClvl) * kSkillPointsPerLevel;
            ui->freeSkillPointsLineEdit->setText(QString::number(newSkillPoints));
        }
        else if (_resurrectionPenalty == ResurrectPenaltyDialog::Skills)
            respecSkills(true);
        else
            investedSkillPoints = newSkillPoints - CharacterInfo::instance().valueOfStatistic(Enums::CharacterStats::FreeSkillPoints);

        int investedStats = investedStatPoints();
        updateStatusTips(newFreeStats + investedStats, investedStats, newSkillPoints, investedSkillPoints);

        updateCharacterExperienceProgressbar(experienceTable.at(newClvl - 1));
    }
}

void MedianXLOfflineTools::resurrect()
{
    CharacterInfo::CharacterInfoBasic &basicInfo = CharacterInfo::instance().basicInfo;
    ResurrectPenaltyDialog dlg(this);
    if (dlg.exec())
    {
        basicInfo.hadDied = false;
        updateHardcoreUIElements();
        updateCharacterTitle(true);

        ui->levelSpinBox->setMaximum(basicInfo.level);
        ui->levelSpinBox->setValue(basicInfo.level);

        _resurrectionPenalty = dlg.resurrectionPenalty();
        switch (_resurrectionPenalty)
        {
        case ResurrectPenaltyDialog::Levels:
        {
            int newLevel = basicInfo.level - ResurrectPenaltyDialog::kLevelPenalty;
            if (newLevel < 1)
                newLevel = 1;
            ui->levelSpinBox->setMaximum(newLevel); // setValue() is invoked implicitly

            break;
        }
        case ResurrectPenaltyDialog::Skills:
        {
            if (!ui->respecSkillsCheckBox->isChecked())
                ui->respecSkillsCheckBox->setChecked(true);
            ui->respecSkillsCheckBox->setDisabled(true);

            ushort newSkillPoints = ui->freeSkillPointsLineEdit->text().toUShort();
            newSkillPoints -= newSkillPoints * ResurrectPenaltyDialog::kSkillPenalty;
            basicInfo.totalSkillPoints = newSkillPoints;

            ui->freeSkillPointsLineEdit->setText(QString::number(newSkillPoints));
            updateMaxCompoundStatusTip(ui->freeSkillPointsLineEdit, newSkillPoints, 0);

            break;
        }
        case ResurrectPenaltyDialog::Stats:
        {
            respecStats();

            ushort newStatPoints = ui->freeStatPointsLineEdit->text().toUShort(), diff = newStatPoints * ResurrectPenaltyDialog::kStatPenalty;
            newStatPoints -= diff;
            basicInfo.totalStatPoints = newStatPoints;

            ui->freeStatPointsLineEdit->setText(QString::number(newStatPoints));
            updateMaxCompoundStatusTip(ui->freeStatPointsLineEdit, newStatPoints, 0);

            foreach (QSpinBox *spinBox, _spinBoxesStatsMap)
                spinBox->setMaximum(spinBox->maximum() - diff);

            break;
        }
        default:
            break;
        }

        // don't allow resurrected characters stay on hardcore
        convertToSoftcore(true);
    }
}

void MedianXLOfflineTools::convertToSoftcore(bool isSoftcore)
{
    updateCharacterTitle(!isSoftcore);
    setModified(true);
}

void MedianXLOfflineTools::backupSettingTriggered(bool checked)
{
    if (!checked && QUESTION_BOX_YESNO(tr("Are you sure you want to disable automatic backups? Then don't blame me if your character gets corrupted."), QMessageBox::No) == QMessageBox::No)
        ui->actionBackup->setChecked(true);
    else
    {
        ui->menuBackupsLimit->setEnabled(checked);
        ui->menuBackupNameFormat->setEnabled(checked);
    }
}

void MedianXLOfflineTools::associateFiles()
{
#if defined(Q_OS_WIN32) || defined(Q_OS_MAC)
    FileAssociationManager::makeApplicationDefaultForExtension(kCharacterExtension);
#else
#warning Add implementation to set file association to e.g. fileassociationmanager_linux.cpp
#endif
    updateAssociateAction(true);
}

void MedianXLOfflineTools::checkForUpdate()
{
    _isManuallyCheckingForUpdate = sender() != 0;

    _qnamCheckForUpdate = new QNetworkAccessManager;
    connect(_qnamCheckForUpdate, SIGNAL(finished(QNetworkReply *)), SLOT(networkReplyCheckForUpdateFinished(QNetworkReply *)));
    qApp->processEvents(); // prevents UI from freezing
    _qnamCheckForUpdate->get(QNetworkRequest(QUrl(kMedianXlServer + "mxlot_version2.txt")));
}

void MedianXLOfflineTools::aboutApp()
{
    const QString appFullName = qApp->applicationName() + " v" + qApp->applicationVersion(), email("decapitator@ukr.net"), baseUserUrl("https://forum.median-xl.com/memberlist.php?mode=viewprofile&u");

    QMessageBox aboutBox(this);
    aboutBox.setWindowTitle(tr("About %1").arg(qApp->applicationName()));
    aboutBox.setIconPixmap(windowIcon().pixmap(64));
    aboutBox.setTextFormat(Qt::RichText);
    aboutBox.setText(QString("<b>%1</b>%2").arg(appFullName, kHtmlLineBreak)
        + tr("Compiled on: %1").arg(QLocale(QLocale::C).toDateTime(QString(__TIMESTAMP__).simplified(), "ddd MMM d hh:mm:ss yyyy").toString("dd.MM.yyyy hh:mm:ss")));
    aboutBox.setInformativeText(
        tr("<b>Author:</b> Filipenkov Andrey aka <a href=\"%1=531\">kambala</a>", "arg is base URL").arg(baseUserUrl)
        + QString(
            "<ul>"
              "<li>Telegram: <a href=\"https://telegram.me/kambala_decapitator\">@kambala_decapitator</a></li>"
              "<li>E-mail: <a href=\"mailto:%1?subject=%2\">%1</a></li>"
            "</ul>").arg(email, appFullName)
        + kForumThreadHtmlLinks + kHtmlLineBreak + kHtmlLineBreak
        + tr("<b>Credits:</b>"
             "<ul>"
               "<li><a href=\"http://modsbylaz.vn.cz/\">BrotherLaz</a> for this awesome mod</li>"
               "<li><a href=\"%1=2\">MarcoNecroX</a> for a hot extension of Median XL called <b>Ultimative</b></li>"
               "<li>grig for the Perl source of <a href=\"https://grig.vlexofree.com/\">Median XL Online Tools</a> and tips</li>"
               "<li><a href=\"https://d2mods.info/index.php?ind=reviews&op=section_view&idev=4\">Phrozen Keep File Guides</a> for tons of useful information on txt sources</li>"
               "<li><a href=\"%1=59\">aahz</a> for providing space on MXL server</li>"
               "<li><a href=\"%1=64\">whist</a> for helping with txt magic and D2 internals</li>"
               "<li>FixeR, Zelgadiss, moonra, Vilius, Delegus, aahz, HerrNieschnell, Quirinus, RollsRoyce, Aks_kun, Unremarkable and gAdlike for help, intensive testing and tips on GUI & functionality</li>"
             "</ul>", "arg is base URL").arg(baseUserUrl)
    );
    aboutBox.exec();
}


void MedianXLOfflineTools::showSkillTree()
{
    SkillTreeDialog dlg(this);
    dlg.exec();
}


// overridden protected methods

void MedianXLOfflineTools::closeEvent(QCloseEvent *e)
{
    if (maybeSave())
    {
        saveSettings();
        e->accept();
    }
    else
        e->ignore();
}

void MedianXLOfflineTools::dragEnterEvent(QDragEnterEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if (mimeData->hasUrls())
    {
        QString path = mimeData->urls().at(0).toLocalFile();
        if (path.right(4).toLower() == kCharacterExtensionWithDot)
            event->acceptProposedAction();
    }
}

void MedianXLOfflineTools::dropEvent(QDropEvent *event)
{
    loadSaveFile(QDir::toNativeSeparators(event->mimeData()->urls().at(0).toLocalFile()));
    event->acceptProposedAction();
}


// private methods

void MedianXLOfflineTools::loadData()
{
    loadExpTable();
    loadMercNames();
    loadBaseStats();

    QFile f(ResourcePathManager::dataPathForFileName("version.txt"));
    if (f.open(QIODevice::ReadOnly))
        _modDataVersion = f.readAll().trimmed();
}

void MedianXLOfflineTools::loadExpTable()
{
    QByteArray fileData = ItemDataBase::decompressedFileData(ResourcePathManager::dataPathForFileName("exptable.dat"), tr("Experience table data not loaded."));
    if (fileData.isEmpty())
        return;

    experienceTable.reserve(Enums::CharacterStats::MaxLevel);
    foreach (const QByteArray &numberString, fileData.split('\n'))
        if (!numberString.isEmpty())
            experienceTable.append(numberString.trimmed().toUInt());
}

void MedianXLOfflineTools::loadMercNames()
{
    QByteArray fileData = ItemDataBase::decompressedFileData(ResourcePathManager::localizedPathForFileName("mercs"), tr("Mercenary names not loaded."));
    if (fileData.isEmpty())
        return;

    mercNames.reserve(4);
    QStringList actNames;
    foreach (const QByteArray &mercName, fileData.split('\n'))
    {
        if (mercName.startsWith("-"))
        {
            mercNames += actNames;
            actNames.clear();
        }
        else
            actNames += QString::fromUtf8(mercName.trimmed());
    }
}

void MedianXLOfflineTools::loadBaseStats()
{
    QByteArray fileData = ItemDataBase::decompressedFileData(ResourcePathManager::dataPathForFileName("basestats.dat"), tr("Base stats data not loaded, using predefined one."));
    if (fileData.isEmpty())
    {
        _baseStatsMap[Enums::ClassName::Amazon]      = BaseStats(BaseStats::StatsAtStart(25, 25, 20, 15, 84), BaseStats::StatsPerLevel(100, 40, 60), BaseStats::StatsPerPoint( 8, 8, 18));
        _baseStatsMap[Enums::ClassName::Sorceress]   = BaseStats(BaseStats::StatsAtStart(10, 25, 15, 35, 74), BaseStats::StatsPerLevel(100, 40, 60), BaseStats::StatsPerPoint( 8, 8, 18));
        _baseStatsMap[Enums::ClassName::Necromancer] = BaseStats(BaseStats::StatsAtStart(15, 25, 20, 25, 79), BaseStats::StatsPerLevel( 80, 20, 80), BaseStats::StatsPerPoint( 4, 8, 24));
        _baseStatsMap[Enums::ClassName::Paladin]     = BaseStats(BaseStats::StatsAtStart(25, 20, 25, 15, 89), BaseStats::StatsPerLevel(120, 60, 40), BaseStats::StatsPerPoint(12, 8, 12));
        _baseStatsMap[Enums::ClassName::Barbarian]   = BaseStats(BaseStats::StatsAtStart(30, 20, 30,  5, 92), BaseStats::StatsPerLevel(120, 60, 40), BaseStats::StatsPerPoint(12, 8, 12));
        _baseStatsMap[Enums::ClassName::Druid]       = BaseStats(BaseStats::StatsAtStart(25, 20, 15, 25, 84), BaseStats::StatsPerLevel( 80, 20, 80), BaseStats::StatsPerPoint( 4, 8, 24));
        _baseStatsMap[Enums::ClassName::Assassin]    = BaseStats(BaseStats::StatsAtStart(20, 35, 15, 15, 95), BaseStats::StatsPerLevel(100, 40, 60), BaseStats::StatsPerPoint( 8, 8, 18));
        return;
    }

    foreach (const QByteArray &s, fileData.split('\n'))
    {
        if (!s.isEmpty() && s.at(0) != '#')
        {
            QList<QByteArray> numbers = s.trimmed().split('\t');
            _baseStatsMap[static_cast<Enums::ClassName::ClassNameEnum>(numbers.at(0).toUInt())] = BaseStats
                (
                // order is correct: energy value comes before vitality in the file
                BaseStats::StatsAtStart (numbers.at(1).toInt(), numbers.at(2).toInt(),  numbers.at(4).toInt(), numbers.at(3).toInt(), numbers.at(5).toInt()),
                BaseStats::StatsPerLevel(numbers.at(6).toInt(), numbers.at(7).toInt(),  numbers.at(8).toInt()),
                BaseStats::StatsPerPoint(numbers.at(9).toInt(), numbers.at(10).toInt(), numbers.at(11).toInt())
                );
        }
    }
}

void MedianXLOfflineTools::createLanguageMenu()
{
    QString appTranslationName = qApp->applicationName().remove(' ').toLower();
    QStringList fileNames = QDir(LanguageManager::instance().translationsPath, QString("%1_*.qm").arg(appTranslationName)).entryList(QDir::Files);
    if (!fileNames.isEmpty())
    {
        QMenu *languageMenu = new QMenu(tr("&Language", "Language menu"), this);
        ui->menuOptions->addSeparator();
        ui->menuOptions->addMenu(languageMenu);

        QActionGroup *languageActionGroup = new QActionGroup(this);
        connect(languageActionGroup, SIGNAL(triggered(QAction *)), SLOT(switchLanguage(QAction *)));

        // HACK: insert English language
        fileNames.prepend(QString("%1_%2.qm").arg(appTranslationName, LanguageManager::instance().defaultLocale));
        foreach (const QString &fileName, fileNames)
        {
            QTranslator translator;
            translator.load(fileName, LanguageManager::instance().translationsPath);

            QString locale = fileName.mid(fileName.indexOf('_') + 1, 2), language = translator.translate("Language", "English", "Your language name");
            if (language.isEmpty())
                language = "English";

            QAction *action = new QAction(language, this);
            action->setCheckable(true);
            action->setStatusTip(locale);
            languageMenu->addAction(action);
            languageActionGroup->addAction(action);

            if (locale == LanguageManager::instance().currentLocale)
                action->setChecked(true);
        }
    }
}

void MedianXLOfflineTools::createLayout()
{
    QList<QWidget *> widgetsToFixSize = QList<QWidget *>() << ui->charNamePreviewLabel << ui->classLineEdit << ui->titleLineEdit << ui->freeSkillPointsLineEdit
                                                           << ui->freeStatPointsLineEdit << ui->signetsOfLearningEatenLineEdit << ui->strengthSpinBox << ui->dexteritySpinBox
                                                           << ui->vitalitySpinBox << ui->energySpinBox << ui->inventoryGoldLineEdit << ui->stashGoldLineEdit << ui->mercLevelLineEdit;
    foreach (QWidget *w, widgetsToFixSize)
        w->setFixedSize(w->size());

    QList<QLineEdit *> readonlyLineEdits = QList<QLineEdit *>() << ui->freeSkillPointsLineEdit << ui->freeStatPointsLineEdit << ui->signetsOfLearningEatenLineEdit
                                                                << ui->inventoryGoldLineEdit << ui->stashGoldLineEdit << ui->mercLevelLineEdit << ui->classLineEdit;
    foreach (QLineEdit *lineEdit, readonlyLineEdits)
        lineEdit->setStyleSheet(kReadonlyCss);

    createCharacterGroupBoxLayout();
    createWaypointsGroupBoxLayout();
    createMercGroupBoxLayout();
    createStatsGroupBoxLayout();

    QGridLayout *grid = new QGridLayout(centralWidget());
    grid->addWidget(ui->characterGroupBox, 0, 0);
    grid->addWidget(ui->waypointsGroupBox, 1, 0);
    grid->addWidget(ui->mercGroupBox, 2, 0);
    grid->addWidget(ui->statsGroupBox, 0, 1);

#ifndef DUPE_CHECK
    createQuestsGroupBoxLayout();
    grid->addWidget(_questsGroupBox, 1, 1, 2, 1);
#endif

    ui->statsTableWidget->verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
    ui->statsTableWidget->horizontalHeader()->
#if IS_QT5
            setSectionResizeMode
#else
            setResizeMode
#endif
            (QHeaderView::Stretch);
    ui->statsTableWidget->setFixedHeight(ui->statsTableWidget->height());

    QD2CharRenamer::customizeNamePreviewLabel(ui->charNamePreviewLabel);
    ui->charNamePreviewLabel->clear(); // remove text set in .ui

    _charPathLabel = new QLabel(this);
    ui->statusBar->addPermanentWidget(_charPathLabel);

    // on Mac OS X height is calculated wrong
#ifndef Q_OS_MAC
    resize(minimumSizeHint());
    setFixedHeight(height());
#endif
}

void MedianXLOfflineTools::createCharacterGroupBoxLayout()
{
    QGridLayout *gridLayout = new QGridLayout;
    gridLayout->addWidget(new QLabel(tr("Name")), 0, 0, Qt::AlignRight);
    gridLayout->addWidget(ui->charNamePreviewLabel, 0, 1);
    gridLayout->addWidget(ui->renameButton, 0, 2);

    gridLayout->addWidget(new QLabel(tr("Class")), 1, 0, Qt::AlignRight);
    gridLayout->addWidget(ui->classLineEdit, 1, 1);
    gridLayout->addWidget(new QLabel(tr("Level")), 1, 2, Qt::AlignCenter);

    gridLayout->addWidget(new QLabel(tr("Title", "Character title - Slayer/Champion/etc.")), 2, 0, Qt::AlignRight);
    gridLayout->addWidget(ui->titleLineEdit, 2, 1);
    gridLayout->addWidget(ui->levelSpinBox, 2, 2);

    QHBoxLayout *hbl2 = new QHBoxLayout(ui->hardcoreGroupBox);
    hbl2->addWidget(ui->convertToSoftcoreCheckBox);
    hbl2->addStretch();
    hbl2->addWidget(ui->resurrectButton);

    QVBoxLayout *vbl = new QVBoxLayout(ui->characterGroupBox);
    vbl->addLayout(gridLayout);

    _expGroupBox = new ExperienceIndicatorGroupBox(this);
#if !defined(QT_NO_DEBUG_OUTPUT) && !defined(DUPE_CHECK)
    _makeNonLadderCheckbox = new QCheckBox(QLatin1String("Make non-ladder"), this);
    connect(_makeNonLadderCheckbox, SIGNAL(clicked(bool)), SLOT(modify()));

    QHBoxLayout *hbl = new QHBoxLayout;
    hbl->addWidget(_expGroupBox);
    hbl->addWidget(_makeNonLadderCheckbox);
    vbl->addLayout(hbl);
#else
    vbl->addWidget(_expGroupBox);
#endif

    vbl->addWidget(ui->hardcoreGroupBox);
}

void MedianXLOfflineTools::createWaypointsGroupBoxLayout()
{
    QHBoxLayout *hbl = new QHBoxLayout(ui->waypointsGroupBox);
    hbl->addWidget(ui->activateWaypointsCheckBox);
}

void MedianXLOfflineTools::createMercGroupBoxLayout()
{
    QHBoxLayout *hbl = new QHBoxLayout;
    hbl->addWidget(new QLabel(tr("Type")));
    hbl->addWidget(ui->mercTypeComboBox);
    hbl->addWidget(new QLabel(tr("Level")));
    hbl->addWidget(ui->mercLevelLineEdit);
    hbl->addWidget(new QLabel(tr("Name")));
    hbl->addWidget(ui->mercNameComboBox);

    _mercExpGroupBox = new ExperienceIndicatorGroupBox(this);

    QVBoxLayout *vbl = new QVBoxLayout(ui->mercGroupBox);
    vbl->addLayout(hbl);
    vbl->addWidget(_mercExpGroupBox);
}

void MedianXLOfflineTools::createStatsGroupBoxLayout()
{
    QGridLayout *gridLayout = new QGridLayout(ui->statsGroupBox);
    gridLayout->addWidget(new QLabel(tr("Inventory Gold")), 0, 0, Qt::AlignRight);
    gridLayout->addWidget(ui->inventoryGoldLineEdit, 0, 1);
    gridLayout->addWidget(new QLabel(tr("Stash Gold")), 0, 3, Qt::AlignRight);
    gridLayout->addWidget(ui->stashGoldLineEdit, 0, 4);

    gridLayout->addWidget(new QLabel(tr("Free Skills")), 1, 0, Qt::AlignRight);
    gridLayout->addWidget(ui->freeSkillPointsLineEdit, 1, 1);
    gridLayout->addWidget(ui->respecSkillsCheckBox, 1, 2);

    gridLayout->addWidget(ui->statsTableWidget, 2, 2, 4, 3, Qt::AlignCenter);
    gridLayout->addWidget(new QLabel(tr("Strength")), 2, 0, Qt::AlignRight);
    gridLayout->addWidget(new QLabel(tr("Dexterity")), 3, 0, Qt::AlignRight);
    gridLayout->addWidget(new QLabel(tr("Vitality")), 4, 0, Qt::AlignRight);
    gridLayout->addWidget(new QLabel(tr("Energy")), 5, 0, Qt::AlignRight);
    gridLayout->addWidget(ui->strengthSpinBox, 2, 1);
    gridLayout->addWidget(ui->dexteritySpinBox, 3, 1);
    gridLayout->addWidget(ui->vitalitySpinBox, 4, 1);
    gridLayout->addWidget(ui->energySpinBox, 5, 1);

    gridLayout->addWidget(new QLabel(tr("Free Stats")), 6, 0, Qt::AlignRight);
    gridLayout->addWidget(ui->freeStatPointsLineEdit, 6, 1);
    gridLayout->addWidget(ui->respecStatsButton, 6, 2);
    gridLayout->addWidget(new QLabel(tr("Signets of Learning")), 6, 3, Qt::AlignRight);
    gridLayout->addWidget(ui->signetsOfLearningEatenLineEdit, 6, 4);
}

void MedianXLOfflineTools::createQuestsGroupBoxLayout()
{
    _questsGroupBox = new QGroupBox(tr("Quests"), this);

    QList<int> questKeys = QList<int>() << Enums::Quests::DenOfEvil << Enums::Quests::Radament << Enums::Quests::Izual << Enums::Quests::LamEsensTome << Enums::Quests::GoldenBird << Enums::Quests::Anya;
    QStringList difficulites = QStringList() << tr("Normal") << tr("Nightmare") << tr("Hell");
    foreach (int quest, questKeys)
    {
        foreach (const QString &difficulty, difficulites)
        {
            QCheckBox *checkBox = new QCheckBox(difficulty, _questsGroupBox);
            checkBox->setDisabled(true);
            _checkboxesQuestsHash[quest] << checkBox;
        }
    }

    QLabel *doeLabel = new QLabel(tr("Den of Evil")), *radamentLabel = new QLabel(tr("Radament")), *izualLabel = new QLabel(tr("Izual")), *lamEsensTomeLabel = new QLabel(tr("Lam Esen's Tome")), *goldenBirdLabel = new QLabel(tr("Golden Bird")), *anyaLabel = new QLabel(tr("Malah's Scroll"));
    configureQuestLabel(doeLabel, tr("%n free skill point(s)", 0, 1), QLatin1String("A1Q1"));
    configureQuestLabel(radamentLabel, tr("%n free skill point(s)", 0, 1), QLatin1String("A2Q1"));
    configureQuestLabel(izualLabel, tr("%n free skill point(s)", 0, 2), QLatin1String("A4Q1"));
    configureQuestLabel(lamEsensTomeLabel, tr("%n free stat point(s)", 0, kStatPointsPerLamEsensTome), QLatin1String("A3Q4"));
    configureQuestLabel(goldenBirdLabel, tr("'+50 to Life' potion"), QLatin1String("A3Q1"));
    configureQuestLabel(anyaLabel, tr("'+10% to Elemental Resistances' scroll"), QLatin1String("A5Q3"));

    QGridLayout *gridLayout = new QGridLayout(_questsGroupBox);
    gridLayout->addWidget(doeLabel, 0, 0);
    gridLayout->addWidget(radamentLabel, 1, 0);
    gridLayout->addWidget(izualLabel, 2, 0);
    gridLayout->addWidget(lamEsensTomeLabel, 4, 0);
    gridLayout->addWidget(goldenBirdLabel, 5, 0);
    gridLayout->addWidget(anyaLabel, 6, 0);

    const int lineRow = 3;
    for (int i = 0; i < questKeys.size(); ++i)
        for (int j = 0; j < kDifficultiesNumber; ++j)
            gridLayout->addWidget(_checkboxesQuestsHash[questKeys.at(i)].at(j), i < lineRow ? i : i + 1, j + 1);

    QFrame *line = new QFrame(_questsGroupBox);
    line->setFrameShape(QFrame::HLine);
    gridLayout->addWidget(line, lineRow, 0, 1, 4);
}

void MedianXLOfflineTools::configureQuestLabel(QLabel *l, const QString &reward, const QString &questInfo)
{
    l->setStatusTip(tr("Reward: %1", "tooltip for quest label").arg(reward));
    l->setToolTip(questInfo);
}

void MedianXLOfflineTools::loadSettings()
{
    QSettings settings;
    if (settings.contains("origin"))
        move(settings.value("origin").toPoint());

    settings.beginGroup("recentItems");
    _recentFilesList = settings.value("recentFiles").toStringList();
    for (int i = 0; i < _recentFilesList.size(); ++i)
        _recentFilesList[i] = QDir::toNativeSeparators(_recentFilesList.at(i));
    updateRecentFilesActions();
    settings.endGroup();

    settings.beginGroup("options");
    ui->actionLoadLastUsedCharacter->setChecked(settings.value("loadLastCharacter", true).toBool());
    ui->actionWarnWhenColoredName->setChecked(settings.value("warnWhenColoredName", true).toBool());

    bool backupsEnabled = settings.value("makeBackups", true).toBool();
    ui->actionBackup->setChecked(backupsEnabled);
    ui->menuBackupsLimit->setEnabled(backupsEnabled);
    ui->menuBackupNameFormat->setEnabled(backupsEnabled);

    (settings.value("backupFormatIsTimestamp").toBool() ? ui->actionBackupFormatTimestamp : ui->actionBackupFormatReadable)->setChecked(true);
    QVariant backupLimit = settings.value("backupLimit");
    if (backupLimit.isValid())
    {
        switch (backupLimit.toInt())
        {
        case 1:
            ui->actionBackups1->setChecked(true);
            break;
        case 2:
            ui->actionBackups2->setChecked(true);
            break;
        case 5:
            ui->actionBackups5->setChecked(true);
            break;
        case 10:
            ui->actionBackups10->setChecked(true);
            break;
        default:
            ui->actionBackupsUnlimited->setChecked(true);
            break;
        }
    }
    else
        ui->actionBackups5->setChecked(true);

    if (ui->actionCheckFileAssociations)
        ui->actionCheckFileAssociations->setChecked(settings.value("checkAssociations", true).toBool());
    ui->actionCheckForUpdateOnStart->setChecked(settings.value("checkUpdates", true).toBool());

    settings.endGroup();
}

void MedianXLOfflineTools::saveSettings() const
{
    QSettings settings;
    settings.setValue(LanguageManager::instance().languageKey, LanguageManager::instance().currentLocale);
    settings.setValue("origin", pos());

    settings.beginGroup("recentItems");
    settings.setValue("recentFiles", _recentFilesList);
    settings.endGroup();

    settings.beginGroup("options");
    settings.setValue("loadLastCharacter", ui->actionLoadLastUsedCharacter->isChecked());
    settings.setValue("warnWhenColoredName", ui->actionWarnWhenColoredName->isChecked());

    settings.setValue("makeBackups", ui->actionBackup->isChecked());
    settings.setValue("backupFormatIsTimestamp", ui->actionBackupFormatTimestamp->isChecked());
    settings.setValue("backupLimit", _backupLimitsGroup->checkedAction()->data().toInt());

    if (ui->actionCheckFileAssociations)
        settings.setValue("checkAssociations", ui->actionCheckFileAssociations->isChecked());
    settings.setValue("checkUpdates", ui->actionCheckForUpdateOnStart->isChecked());

    settings.endGroup();
}

void MedianXLOfflineTools::fillMaps()
{
    _spinBoxesStatsMap[Enums::CharacterStats::Strength] = ui->strengthSpinBox;
    _spinBoxesStatsMap[Enums::CharacterStats::Dexterity] = ui->dexteritySpinBox;
    _spinBoxesStatsMap[Enums::CharacterStats::Vitality] = ui->vitalitySpinBox;
    _spinBoxesStatsMap[Enums::CharacterStats::Energy] = ui->energySpinBox;

    _lineEditsStatsMap[Enums::CharacterStats::FreeStatPoints] = ui->freeStatPointsLineEdit;
    _lineEditsStatsMap[Enums::CharacterStats::FreeSkillPoints] = ui->freeSkillPointsLineEdit;
    _lineEditsStatsMap[Enums::CharacterStats::InventoryGold] = ui->inventoryGoldLineEdit;
    _lineEditsStatsMap[Enums::CharacterStats::StashGold] = ui->stashGoldLineEdit;
    _lineEditsStatsMap[Enums::CharacterStats::SignetsOfLearningEaten] = ui->signetsOfLearningEatenLineEdit;
}

void MedianXLOfflineTools::connectSignals()
{
    // files
    connect(ui->actionLoadCharacter, SIGNAL(triggered()), SLOT(loadCharacter()));
    connect(ui->actionReloadCharacter, SIGNAL(triggered()), SLOT(reloadCharacter()));
    connect(ui->actionSaveCharacter, SIGNAL(triggered()), SLOT(saveCharacter()));

    // edit
    connect(ui->actionRename, SIGNAL(triggered()), SLOT(rename()));

    // options
    connect(ui->actionBackup, SIGNAL(triggered(bool)), SLOT(backupSettingTriggered(bool)));
    if (ui->actionAssociate)
        connect(ui->actionAssociate, SIGNAL(triggered()), SLOT(associateFiles()));
#ifdef Q_OS_WIN32
    if (ui->actionOpenFileAssociationUI)
        connect(ui->actionOpenFileAssociationUI, SIGNAL(triggered()), SLOT(showFileAssocaitionUI()));
#endif

    // help
    connect(ui->actionCheckForUpdate, SIGNAL(triggered()), SLOT(checkForUpdate()));
    connect(ui->actionAbout, SIGNAL(triggered()), SLOT(aboutApp()));
    connect(ui->actionAboutQt, SIGNAL(triggered()), qApp, SLOT(aboutQt()));

    connect(ui->levelSpinBox, SIGNAL(valueChanged(int)), SLOT(levelChanged(int)));
    foreach (QSpinBox *spinBox, _spinBoxesStatsMap)
        connect(spinBox, SIGNAL(valueChanged(int)), SLOT(statChanged(int)));

    // toolbar
    connect(ui->actionSkillTree, SIGNAL(triggered()), SLOT(showSkillTree()));

    // main window
    connect(ui->renameButton, SIGNAL(clicked()), SLOT(rename()));
    connect(ui->resurrectButton, SIGNAL(clicked()), SLOT(resurrect()));
    connect(ui->convertToSoftcoreCheckBox, SIGNAL(toggled(bool)), SLOT(convertToSoftcore(bool)));

    connect(ui->respecStatsButton, SIGNAL(clicked()), SLOT(respecStats()));
    connect(ui->respecSkillsCheckBox, SIGNAL(toggled(bool)), SLOT(respecSkills(bool)));

    connect(ui->activateWaypointsCheckBox, SIGNAL(toggled(bool)), SLOT(modify()));

    // misc
    connect(_fsWatcher, SIGNAL(fileChanged(const QString &)), SLOT(fileContentsChanged()));
}

void MedianXLOfflineTools::updateRecentFilesActions()
{
    ui->menuRecentCharacters->clear();
    for (int i = 0; i < _recentFilesList.length(); ++i)
    {
        QString filePath = _recentFilesList.at(i);
        if (QFile::exists(filePath))
            ui->menuRecentCharacters->addAction(createRecentFileAction(filePath, i + 1));
        else
        {
#ifdef Q_OS_WIN32
            removeFromWindowsRecentFiles(_recentFilesList.takeAt(i));
#else
            _recentFilesList.removeAt(i);
#endif
            --i;
        }
    }
    ui->menuRecentCharacters->setDisabled(_recentFilesList.isEmpty());

#ifdef Q_OS_MAC
    qt_mac_set_dock_menu(ui->menuRecentCharacters);
#endif
}

void MedianXLOfflineTools::addToRecentFiles()
{
    int index = _recentFilesList.indexOf(_charPath);
#ifdef Q_OS_WIN32
    // previous version didn't use native separators
    if (index == -1)
    {
        index = _recentFilesList.indexOf(QDir::fromNativeSeparators(_charPath));
        if (index != -1)
            _recentFilesList[index] = _charPath;
    }
#endif

    if (index != -1) // it's already in the list
        _recentFilesList.move(index, 0);
    else
    {
        if (_recentFilesList.length() == kMaxRecentFiles)
        {
#ifdef Q_OS_WIN32
            removeFromWindowsRecentFiles(_recentFilesList.takeLast());
#else
            _recentFilesList.removeLast();
#endif
        }
        _recentFilesList.prepend(_charPath);
    }
#ifdef Q_OS_WIN32
    addToWindowsRecentFiles(_charPath); // Windows moves file to the top itself if it already exists in the list
#endif
    updateRecentFilesActions();
}

QAction *MedianXLOfflineTools::createRecentFileAction(const QString &filePath, int index)
{
    // don't use numbers on Mac OS X because there're no mnemonics
#ifdef Q_OS_MAC
    Q_UNUSED(index);
    QAction *recentFileAction = new QAction(QFileInfo(filePath).fileName(), this);
#else
    QAction *recentFileAction = new QAction(QString("&%1 %2").arg(index).arg(QFileInfo(filePath).fileName()), this);
#endif
    recentFileAction->setStatusTip(QDir::toNativeSeparators(filePath));
    connect(recentFileAction, SIGNAL(triggered()), SLOT(openRecentFile()));
    return recentFileAction;
}

void MedianXLOfflineTools::loadSaveFile(const QString &filePath, bool shouldNotify, const QString &statusBarMessage)
{
    if (loadFile(filePath) && shouldNotify)
        ui->statusBar->showMessage(statusBarMessage, 3000);
}

bool MedianXLOfflineTools::processSaveFile()
{
    using namespace Enums;

    QFile inputFile(_charPath);
    if (!inputFile.open(QIODevice::ReadOnly))
    {
        showErrorMessageBoxForFile(tr("Error opening file '%1'"), inputFile);
        return false;
    }

    _saveFileContents = inputFile.readAll();
    inputFile.close();

    QDataStream inputDataStream(_saveFileContents);
    inputDataStream.setByteOrder(QDataStream::LittleEndian);

    quint32 signature;
    inputDataStream >> signature;
    if (signature != kFileSignature)
    {
        showLoadingError(tr("Wrong file signature: should be 0x%1, got 0x%2.").arg(kFileSignature, 0, 16).arg(signature, 0, 16));
        return false;
    }

    inputDataStream.device()->seek(Offsets::Checksum); //-V807
    quint32 fileChecksum, computedChecksum = checksum(_saveFileContents);
    inputDataStream >> fileChecksum;
#ifndef DISABLE_CRC_CHECK
    if (fileChecksum != computedChecksum)
    {
        showLoadingError(tr("Character checksum doesn't match. Looks like it's corrupted."));
        return false;
    }
#endif

    CharacterInfo &charInfo = CharacterInfo::instance();
    charInfo.basicInfo.originalName = _saveFileContents.constData() + Offsets::Name;
    charInfo.basicInfo.newName = charInfo.basicInfo.originalName.replace(ColorsManager::ansiColorHeader(), ColorsManager::unicodeColorHeader());

    inputDataStream.device()->seek(Offsets::Status);
    quint8 status, progression, classCode, clvl, skillsNumber;
    inputDataStream >> status >> progression;
    inputDataStream.device()->seek(Offsets::Class);
    inputDataStream >> classCode;
    inputDataStream.device()->seek(Offsets::SkillsCount);
    inputDataStream >> skillsNumber;
    inputDataStream.device()->seek(Offsets::Level);
    inputDataStream >> clvl;

    if (!(status & StatusBits::IsExpansion))
    {
        showLoadingError(tr("This is not Expansion character."));
        return false;
    }
    charInfo.basicInfo.isHardcore = status & StatusBits::IsHardcore;
    charInfo.basicInfo.hadDied = status & StatusBits::HadDied;
    charInfo.basicInfo.isLadder = status & StatusBits::IsLadder;

    if (classCode > ClassName::Assassin)
    {
        showLoadingError(tr("Wrong class value: got %1").arg(classCode));
        return false;
    }
    charInfo.basicInfo.classCode = static_cast<ClassName::ClassNameEnum>(classCode);

    if (progression >= Progression::Completed)
    {
        showLoadingError(tr("Wrong progression value: got %1").arg(progression));
        return false;
    }
    charInfo.basicInfo.titleCode = progression;

    if (!clvl || clvl > CharacterStats::MaxLevel)
    {
        showLoadingError(tr("Wrong level: got %1").arg(clvl));
        return false;
    }
    charInfo.basicInfo.level = clvl;

    const int hotkeyedSkillsSize = 16;
    QList<quint32> &hotkeyedSkills = charInfo.basicInfo.hotkeyedSkills;
    hotkeyedSkills.clear();
    hotkeyedSkills.reserve(hotkeyedSkillsSize);
    inputDataStream.device()->seek(Offsets::SkillKeys);
    for (int i = 0; i < hotkeyedSkillsSize; ++i)
    {
        quint32 skill;
        inputDataStream >> skill;
        hotkeyedSkills += skill;
    }
    inputDataStream >> charInfo.basicInfo.mainHandSkills.lmb >> charInfo.basicInfo.mainHandSkills.rmb;
    inputDataStream >> charInfo.basicInfo.altHandSkills.lmb >> charInfo.basicInfo.altHandSkills.rmb;

    inputDataStream.device()->seek(Offsets::Mercenary);
    quint32 mercID;
    inputDataStream >> mercID;
    if ((charInfo.mercenary.exists = (mercID != 0)))
    {
        quint16 mercName, mercValue;
        inputDataStream >> mercName >> mercValue;
        if (mercValue > Mercenary::MaxCode)
        {
            showLoadingError(tr("Wrong mercenary code: got %1").arg(mercValue));
            return false;
        }
        charInfo.mercenary.code = Mercenary::mercCodeFromValue(mercValue);
        charInfo.mercenary.nameIndex = mercName;

        quint32 mercExp;
        inputDataStream >> mercExp;
        charInfo.mercenary.experience = mercExp;
        for (quint8 i = 1; i <= CharacterStats::MaxLevel; ++i)
        {
            if (mercExp < mercExperienceForLevel(i))
            {
                charInfo.mercenary.level = i - 1;
                break;
            }
        }
    }

    // Quests
    if (_saveFileContents.mid(Offsets::QuestsHeader, 4) != "Woo!")
    {
        showLoadingError(tr("Quests data not found!"));
        return false;
    }
    charInfo.questsInfo.clear();
    for (int i = 0; i < kDifficultiesNumber; ++i)
    {
        int baseOffset = Offsets::QuestsData + i * Quests::Size;
        charInfo.questsInfo.denOfEvil    += static_cast<bool>(_saveFileContents.at(baseOffset + Quests::DenOfEvil)    &  Quests::IsCompleted);
        charInfo.questsInfo.radament     += static_cast<bool>(_saveFileContents.at(baseOffset + Quests::Radament)     & (Quests::IsCompleted | Quests::IsTaskDone));
        charInfo.questsInfo.goldenBird   += static_cast<bool>(_saveFileContents.at(baseOffset + Quests::GoldenBird)   &  Quests::IsCompleted);
        charInfo.questsInfo.lamEsensTome += static_cast<bool>(_saveFileContents.at(baseOffset + Quests::LamEsensTome) &  Quests::IsCompleted);
        charInfo.questsInfo.izual        += static_cast<bool>(_saveFileContents.at(baseOffset + Quests::Izual)        &  Quests::IsCompleted);
        charInfo.questsInfo.rescueAnya   += static_cast<bool>(_saveFileContents.at(baseOffset + Quests::Anya)         & (Quests::IsCompleted | Quests::IsTaskDone));
    }

    // WP
    if (_saveFileContents.mid(Offsets::WaypointsHeader, 2) != "WS")
    {
        showLoadingError(tr("Waypoint data not found!"));
        return false;
    }

    //inputDataStream.device->seek(Offsets::WaypointsData);
    //for (int i = 0; i < 3; ++i)
    //{
    //    inputDataStream.skipRawData(2);

    //    quint64 wpData;
    //    inputDataStream >> wpData;
    //    QString wpString = binaryStringFromNumber(wpData, true, 39);
    //    qDebug() << "wp data difficulty" << i << ":";
    //    int startPos = 0;
    //    for (int j = 0; j < 5; ++j)
    //    {
    //        int endPos = j == 3 ? 3 : 9;
    //        qDebug() << "act" << (j + 1) << ":" << wpString.mid(startPos, endPos);
    //        startPos += endPos;
    //    }

    //    inputDataStream.skipRawData(14);
    //}

    // NPC
    if (_saveFileContents.mid(Offsets::NPCHeader, 2) != "w4")
    {
        showLoadingError(tr("NPC data not found!"));
        return false;
    }

    // stats
    if (_saveFileContents.mid(Offsets::StatsHeader, 2) != "gf")
    {
        showLoadingError(tr("Stats data not found!"));
        return false;
    }
    inputDataStream.device()->seek(Offsets::StatsData);

    // find "if" header (skills)
    int skillsOffset = _saveFileContents.indexOf(kSkillsHeader, Offsets::StatsData);
    if (skillsOffset == -1)
    {
        showLoadingError(tr("Skills data not found!"));
        return false;
    }

    // apparently "if" can occur multiple times before items section, so we need the last occurrence before skills data
    int firstItemOffset = _saveFileContents.indexOf(kItemHeader, skillsOffset);
    while (skillsOffset != -1 && skillsOffset < firstItemOffset)
    {
        charInfo.skillsOffset = skillsOffset;
        skillsOffset = _saveFileContents.indexOf(kSkillsHeader, skillsOffset + 1);
    }

    int statsSize = charInfo.skillsOffset - Offsets::StatsData;
    QString statsBitData;
    statsBitData.reserve(statsSize * 8);
    for (int i = 0; i < statsSize; ++i)
    {
        quint8 aByte;
        inputDataStream >> aByte;
        statsBitData.prepend(binaryStringFromNumber(aByte));
    }

    // clear dynamic values
    charInfo.basicInfo.statsDynamicData.clear();

    int count = 0; // to prevent infinite loop if something goes wrong
    const int maxTries = 1000;
    int totalStats = 0;
    bool shouldShowHackWarning = false;
    ReverseBitReader bitReader(statsBitData);
    for (; count < maxTries; ++count)
    {
        CharacterStats::StatisticEnum statCode = static_cast<CharacterStats::StatisticEnum>(bitReader.readNumber(CharacterStats::StatCodeLength));
        if (statCode == CharacterStats::End)
            break;

        ItemPropertyTxt *txtProp = ItemDataBase::Properties()->value(statCode);
        int statLength = txtProp->bitsSave;
        if (!statLength)
        {
            showLoadingError(tr("Unknown statistic code found: %1. This is not %2 character.", "second param is mod name").arg(statCode).arg(modName));
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
        else if (statCode == CharacterStats::SignetsOfLearningEaten && statValue > CharacterStats::SignetsOfLearningMax)
        {
            if (statValue > CharacterStats::SignetsOfLearningMax + 1)
                shouldShowHackWarning = true;
            statValue = CharacterStats::SignetsOfLearningMax;
        }
        else if (statCode == CharacterStats::SignetsOfSkillEaten && statValue > CharacterStats::SignetsOfSkillMax)
        {
            shouldShowHackWarning = true;
            statValue = CharacterStats::SignetsOfSkillMax;
        }
        else if (statCode >= CharacterStats::Strength && statCode <= CharacterStats::Vitality)
        {
            int baseStat = _baseStatsMap[charInfo.basicInfo.classCode].statsAtStart.statFromCode(statCode);
            totalStats += statValue - baseStat;
        }
        else if (statCode == CharacterStats::FreeStatPoints)
            totalStats += statValue;

        statData << statValue;
        charInfo.basicInfo.statsDynamicData.insert(statCode, statData);
    }
    if (count == maxTries)
    {
        showLoadingError(tr("Stats data is corrupted!"));
        return false;
    }

#ifndef MAKE_FINISHED_CHARACTER
    int totalPossibleStats = totalPossibleStatPoints(charInfo.basicInfo.level);
    if (totalStats > totalPossibleStats) // check if stats are hacked
    {
        for (int i = CharacterStats::Strength; i <= CharacterStats::Vitality; ++i)
        {
            CharacterStats::StatisticEnum statCode = static_cast<CharacterStats::StatisticEnum>(i);
            int baseStat = _baseStatsMap[charInfo.basicInfo.classCode].statsAtStart.statFromCode(statCode);
            charInfo.setValueForStatistic(baseStat, statCode);
        }
        charInfo.setValueForStatistic(totalPossibleStats, CharacterStats::FreeStatPoints);
        shouldShowHackWarning = true;
    }
#endif

    // skills
    quint16 skills = 0, maxPossibleSkills = totalPossibleSkillPoints();
    charInfo.basicInfo.skills.clear();
    charInfo.basicInfo.skills.reserve(skillsNumber);

    inputDataStream.skipRawData(kSkillsHeader.length());
    int firstSkillOffset = charInfo.skillsOffset + kSkillsHeader.length();
    const Enums::Skills::SkillsOrderPair skillsIndexes = Enums::Skills::currentCharacterSkillsIndexes();
    for (quint8 i = 0; i < skillsNumber; ++i)
    {
        quint8 skillValue;
        inputDataStream >> skillValue;

        // Sigma 2.11 characters have "invisible" skills
        SkillInfo *skill = ItemDataBase::Skills()->at(skillsIndexes.first.at(i));
        if (skill->tab > 0)
        {
            skills += skillValue;
            charInfo.basicInfo.skills += skillValue;
        }
    }
    skills += charInfo.valueOfStatistic(CharacterStats::FreeSkillPoints);
#ifndef MAKE_FINISHED_CHARACTER
    if (skills > maxPossibleSkills) // check if skills are hacked
    {
        skills = maxPossibleSkills;
        charInfo.setValueForStatistic(maxPossibleSkills, CharacterStats::FreeSkillPoints);
        _saveFileContents.replace(firstSkillOffset, skillsNumber, QByteArray(skillsNumber, 0));
        shouldShowHackWarning = true;
    }
#endif
    charInfo.basicInfo.totalSkillPoints = qMax(skills, maxPossibleSkills); // v0.6.5 could return less skills points then needed when respeccing

    charInfo.basicInfo.skillsReadable.clear();
    charInfo.basicInfo.skillsReadable.reserve(skillsNumber);
    foreach (int skillIndex, skillsIndexes.second)
    {
        int skillReadableIndex = skillsIndexes.first.indexOf(skillIndex);
        charInfo.basicInfo.skillsReadable += skillReadableIndex >= 0 && skillReadableIndex < charInfo.basicInfo.skills.size() ? charInfo.basicInfo.skills.at(skillReadableIndex) : 0;
        //qDebug() << charInfo.basicInfo.skillsReadable.last() << ItemDataBase::Skills()->value(skillIndex)->name;
    }

    if (shouldShowHackWarning)
        showLoadingError(kHackerDetected);

    // items are intentionally left completely unparsed in this stripped-down build: everything from here to
    // the end of the save file (character items, corpse marker, mercenary items, and the Iron Golem item, if
    // any) is treated as one opaque blob and is preserved automatically, since only stats/skills are edited.
    int charItemsOffset = inputDataStream.device()->pos();
    if (_saveFileContents.mid(charItemsOffset, kItemHeader.length()) != kItemHeader)
    {
        showLoadingError(tr("Items data not found!"));
        return false;
    }
    charInfo.itemsOffset = charItemsOffset + kItemHeader.length();

    _fsWatcher->addPath(_charPath);

    return true;
}

void MedianXLOfflineTools::showLoadingError(const QString &error, bool warn)
{
    if (warn)
        WARNING_BOX(error);
    else
        ERROR_BOX(error);
}

quint32 MedianXLOfflineTools::checksum(const QByteArray &charByteArray) const
{
    quint32 sum = 0;
    for (int i = 0, n = charByteArray.size(); i < n; ++i)
    {
        bool mostSignificantByte = sum & 0x80000000;
        sum <<= 1;
        sum += mostSignificantByte;
        sum &= 0xFFFFFFFF;
        if (i < Enums::Offsets::Checksum || i >= Enums::Offsets::Checksum + 4) // bytes 12-15 - file checksum
            sum += static_cast<quint8>(charByteArray.at(i));
    }
    return sum;
}

inline int MedianXLOfflineTools::totalPossibleStatPoints(int level) const
{
    return totalPossibleStatPoints(level, CharacterInfo::instance().questsInfo.lamEsensTomeQuestsCompleted());
}

int MedianXLOfflineTools::totalPossibleStatPoints(int level, quint8 let) const
{
    return (level - 1) * kStatPointsPerLevel + kStatPointsPerLamEsensTome * let + CharacterInfo::instance().valueOfStatistic(Enums::CharacterStats::SignetsOfLearningEaten);
}

inline int MedianXLOfflineTools::totalPossibleSkillPoints() const
{
    const CharacterInfo &charInfo = CharacterInfo::instance();
    return totalPossibleSkillPoints(charInfo.basicInfo.level, charInfo.questsInfo.denOfEvilQuestsCompleted(), charInfo.questsInfo.radamentQuestsCompleted(), charInfo.questsInfo.izualQuestsCompleted());
}

int MedianXLOfflineTools::totalPossibleSkillPoints(int level, quint8 doe, quint8 rad, quint8 iz) const
{
    return (level - 1) * kSkillPointsPerLevel + doe + rad + iz * 2 + CharacterInfo::instance().valueOfStatistic(Enums::CharacterStats::SignetsOfSkillEaten);
}

int MedianXLOfflineTools::investedStatPoints()
{
    quint16 sum = 0;
    for (quint8 i = Enums::CharacterStats::Strength; i <= Enums::CharacterStats::Vitality; ++i)
    {
        Enums::CharacterStats::StatisticEnum ienum = static_cast<Enums::CharacterStats::StatisticEnum>(i);
        sum += _spinBoxesStatsMap[ienum]->value() - _baseStatsMap[CharacterInfo::instance().basicInfo.classCode].statsAtStart.statFromCode(ienum);
    }
    return sum;
}

void MedianXLOfflineTools::recalculateStatPoints()
{
    CharacterInfo::instance().basicInfo.totalStatPoints = investedStatPoints() + ui->freeStatPointsLineEdit->text().toUInt();
}

void MedianXLOfflineTools::clearUI()
{
    _isLoaded = false;
    _resurrectionPenalty = ResurrectPenaltyDialog::Nothing;

    QList<QLineEdit *> lineEdits = QList<QLineEdit *>() << ui->classLineEdit << ui->titleLineEdit << ui->mercLevelLineEdit;
    foreach (QLineEdit *lineEdit, lineEdits)
        lineEdit->clear();
    foreach (QLineEdit *lineEdit, _lineEditsStatsMap)
    {
        lineEdit->clear();
        lineEdit->setStatusTip(QString());
    }
    ui->charNamePreviewLabel->clear();

    QList<QComboBox *> comboBoxes = QList<QComboBox *>() << ui->mercNameComboBox << ui->mercTypeComboBox;
    foreach (QComboBox *combobx, comboBoxes)
        combobx->clear();

    foreach (QSpinBox *spinBox, _spinBoxesStatsMap)
    {
        spinBox->setMinimum(0);
        spinBox->setValue(0);
        spinBox->setStatusTip(QString());
    }
    ui->levelSpinBox->setValue(1);

    QList<QCheckBox *> checkBoxes = QList<QCheckBox *>() << ui->convertToSoftcoreCheckBox << ui->respecSkillsCheckBox << ui->activateWaypointsCheckBox;
    foreach (QList<QCheckBox *> questCheckBoxes, _checkboxesQuestsHash.values())
        checkBoxes << questCheckBoxes;
#if !defined(QT_NO_DEBUG_OUTPUT) && !defined(DUPE_CHECK)
    _makeNonLadderCheckbox->setEnabled(false);
    checkBoxes << _makeNonLadderCheckbox;
#endif
    foreach (QCheckBox *checkbox, checkBoxes)
        checkbox->setChecked(false);

    ui->respecStatsButton->setChecked(false);

    QList<QGroupBox *> groupBoxes = QList<QGroupBox *>() << ui->characterGroupBox << ui->statsGroupBox << ui->waypointsGroupBox << ui->mercGroupBox << _questsGroupBox;
    foreach (QGroupBox *groupBox, groupBoxes)
        groupBox->setDisabled(true);

    QList<QAction *> actions = QList<QAction *>() << ui->actionReloadCharacter << ui->actionSaveCharacter << ui->actionRename << ui->actionRespecStats << ui->actionRespecSkills << ui->actionActivateWaypoints
                                                  << ui->actionConvertToSoftcore << ui->actionResurrect << ui->actionSkillTree;
    foreach (QAction *action, actions)
        action->setDisabled(true);

    for (int i = 0; i < ui->statsTableWidget->rowCount(); ++i)
    {
        for (int j = 0; j < ui->statsTableWidget->columnCount(); ++j)
        {
            QTableWidgetItem *item = ui->statsTableWidget->item(i, j);
            if (!item)
            {
                item = new QTableWidgetItem;
                item->setTextAlignment(Qt::AlignCenter);
                item->setBackground(QColor(227, 227, 227)); // same color as in kReadonlyCss
                ui->statsTableWidget->setItem(i, j, item);
            }
            item->setText(QString());
        }
    }

    _expGroupBox->reset();
    _mercExpGroupBox->reset();
}

void MedianXLOfflineTools::updateUI()
{
    clearUI();

    QList<QAction *> actions = QList<QAction *>() << ui->actionReloadCharacter << ui->actionRename << ui->actionRespecStats << ui->actionRespecSkills << ui->actionActivateWaypoints
                                                  << ui->actionSkillTree;
    foreach (QAction *action, actions)
        action->setEnabled(true);
    ui->respecSkillsCheckBox->setEnabled(true);

    QList<QGroupBox *> groupBoxes = QList<QGroupBox *>() << ui->characterGroupBox << ui->statsGroupBox << ui->waypointsGroupBox << _questsGroupBox;
    foreach (QGroupBox *groupBox, groupBoxes)
        groupBox->setEnabled(true);

    const CharacterInfo &charInfo = CharacterInfo::instance();
    QD2CharRenamer::updateNamePreviewLabel(ui->charNamePreviewLabel, charInfo.basicInfo.originalName);

    ui->hardcoreGroupBox->setEnabled(charInfo.basicInfo.isHardcore);
    if (charInfo.basicInfo.isHardcore)
        updateHardcoreUIElements();

    ui->classLineEdit->setText(Enums::ClassName::classes().at(charInfo.basicInfo.classCode));
    updateCharacterTitle(charInfo.basicInfo.isHardcore);

    _oldClvl = charInfo.basicInfo.level;
    ui->levelSpinBox->setMaximum(_oldClvl);
    ui->levelSpinBox->setValue(_oldClvl);

    updateCharacterExperienceProgressbar(charInfo.valueOfStatistic(Enums::CharacterStats::Experience));

#if !defined(QT_NO_DEBUG_OUTPUT) && !defined(DUPE_CHECK)
    _makeNonLadderCheckbox->setEnabled(true);
    _makeNonLadderCheckbox->setChecked(true);
#endif

    setStats();
    recalculateStatPoints();

    int stats = charInfo.basicInfo.totalStatPoints, skills = charInfo.basicInfo.totalSkillPoints;
    updateStatusTips(stats, stats - charInfo.valueOfStatistic(Enums::CharacterStats::FreeStatPoints), skills, skills - charInfo.valueOfStatistic(Enums::CharacterStats::FreeSkillPoints));
    ui->signetsOfLearningEatenLineEdit->setStatusTip(maxValueFormat.arg(Enums::CharacterStats::SignetsOfLearningMax));
    ui->stashGoldLineEdit->setStatusTip(maxValueFormat.arg(QLocale().toString(Enums::CharacterStats::StashGoldMax)));

    if (charInfo.mercenary.exists)
    {
        int mercTypeIndex;
        QPair<int, int> mercArraySlice = Enums::Mercenary::allowedTypesForMercCode(charInfo.mercenary.code, &mercTypeIndex);
        ui->mercTypeComboBox->addItems(Enums::Mercenary::types().mid(mercArraySlice.first, mercArraySlice.second));
        ui->mercTypeComboBox->setCurrentIndex(mercTypeIndex);

        ui->mercNameComboBox->addItems(mercNames.at(Enums::Mercenary::mercNamesIndexFromCode(charInfo.mercenary.code)));
        ui->mercNameComboBox->setCurrentIndex(charInfo.mercenary.nameIndex);

        ui->mercLevelLineEdit->setText(QString::number(charInfo.mercenary.level));

        if (charInfo.mercenary.level == Enums::CharacterStats::MaxLevel - 1 && charInfo.mercenary.experience < mercExperienceForLevel(Enums::CharacterStats::MaxLevel - 1) + 5)
        {
            // display (maxlevel-1) as 100% of progressbar
            _mercExpGroupBox->setPreviousLevelExperience(mercExperienceForLevel(charInfo.mercenary.level - 1));
            _mercExpGroupBox->setNextLevelExperience(charInfo.mercenary.experience);
        }
        else
        {
            _mercExpGroupBox->setPreviousLevelExperience(mercExperienceForLevel(charInfo.mercenary.level));
            _mercExpGroupBox->setNextLevelExperience(mercExperienceForLevel(charInfo.mercenary.level + 1));
        }
        _mercExpGroupBox->setCurrentExperience(charInfo.mercenary.experience);

        ui->mercGroupBox->setEnabled(true);
    }

    for (int i = 0; i < kDifficultiesNumber; ++i)
    {
        _checkboxesQuestsHash[Enums::Quests::DenOfEvil]   [i]->setChecked(charInfo.questsInfo.denOfEvil   .at(i));
        _checkboxesQuestsHash[Enums::Quests::Radament]    [i]->setChecked(charInfo.questsInfo.radament    .at(i));
        _checkboxesQuestsHash[Enums::Quests::Izual]       [i]->setChecked(charInfo.questsInfo.izual       .at(i));
        _checkboxesQuestsHash[Enums::Quests::LamEsensTome][i]->setChecked(charInfo.questsInfo.lamEsensTome.at(i));
        _checkboxesQuestsHash[Enums::Quests::GoldenBird]  [i]->setChecked(charInfo.questsInfo.goldenBird  .at(i));
        _checkboxesQuestsHash[Enums::Quests::Anya]        [i]->setChecked(charInfo.questsInfo.rescueAnya  .at(i));
    }

    updateWindowTitle();

    _isLoaded = true;
}

void MedianXLOfflineTools::updateHardcoreUIElements()
{
    bool hadDied = CharacterInfo::instance().basicInfo.hadDied;
    ui->convertToSoftcoreCheckBox->setDisabled(hadDied);
    ui->actionConvertToSoftcore->setDisabled(hadDied);
    ui->resurrectButton->setEnabled(hadDied);
    ui->actionResurrect->setEnabled(hadDied);
}

void MedianXLOfflineTools::updateCharacterTitle(bool isHardcore)
{
    const CharacterInfo::CharacterInfoBasic &basicInfo = CharacterInfo::instance().basicInfo;
    bool isMale = basicInfo.classCode >= Enums::ClassName::Necromancer && basicInfo.classCode <= Enums::ClassName::Druid;
    quint8 progression = basicInfo.titleCode;
    QPair<QString, quint8> titleAndMaxDifficulty = Enums::Progression::titleNameAndMaxDifficultyFromValue(progression, isMale, isHardcore);
    QString newTitle = titleAndMaxDifficulty.first;
    if (basicInfo.isHardcore && basicInfo.hadDied)
        newTitle += QString(" (%1)").arg(tr("DEAD", "HC character is dead"));
    ui->titleLineEdit->setText(newTitle);
    ui->titleLineEdit->setStyleSheet(QString("%1; color: %2;").arg(kReadonlyCss, isHardcore ? "red" : "black"));
}

void MedianXLOfflineTools::setStats()
{
    QMetaEnum statisticMetaEnum = Enums::CharacterStats::statisticMetaEnum();
    for (int i = 0; i < statisticMetaEnum.keyCount(); ++i)
    {
        Enums::CharacterStats::StatisticEnum statCode = static_cast<Enums::CharacterStats::StatisticEnum>(statisticMetaEnum.value(i));
        quint32 value = CharacterInfo::instance().valueOfStatistic(statCode);

        if (statCode >= Enums::CharacterStats::Strength && statCode <= Enums::CharacterStats::Vitality)
        {
            _oldStatValues[statCode] = value;

            quint8 baseValue = _baseStatsMap[CharacterInfo::instance().basicInfo.classCode].statsAtStart.statFromCode(statCode);
            QSpinBox *spinBox = _spinBoxesStatsMap[statCode];
            spinBox->setMinimum(baseValue);
            spinBox->setMaximum(value);
            spinBox->setValue(value);
        }
        else if (statCode >= Enums::CharacterStats::Life && statCode <= Enums::CharacterStats::BaseStamina)
        {
            int j = statCode - Enums::CharacterStats::Life, row = j / 2, col = j % 2;
            ui->statsTableWidget->item(row, col)->setText(QString::number(value));
        }
        else if (QLineEdit *lineEdit = _lineEditsStatsMap.value(statCode, 0))
        {
            lineEdit->setText(QString::number(value));
        }
    }

    int freeStats = ui->freeStatPointsLineEdit->text().toInt();
    if (freeStats)
        foreach (QSpinBox *spinBox, _spinBoxesStatsMap)
            spinBox->setMaximum(spinBox->maximum() + freeStats);
}

void MedianXLOfflineTools::updateWindowTitle()
{
    _charPathLabel->setText(QDir::toNativeSeparators(_charPath));

    QString modVersion = modName;
    if (!_modDataVersion.isEmpty())
        modVersion += QLatin1String(" v") + _modDataVersion;

    // making setWindowFilePath() work correctly
#ifdef Q_OS_MAC
    setWindowTitle(_charPath.isEmpty() ? modVersion : QString("%1 (%2)").arg(QFileInfo(_charPath).fileName(), modVersion));
#else
    QString title = QString("%1 (%2)").arg(qApp->applicationName(), modVersion);
    setWindowTitle(_charPath.isEmpty() ? title : QString("%1[*] %2 %3").arg(QFileInfo(_charPath).fileName(), QChar(0x2014), title));
#endif
}

void MedianXLOfflineTools::updateTableStats(const BaseStats::StatsStep &statsPerStep, int diff, QSpinBox *senderSpinBox /*= 0*/)
{
    if (!senderSpinBox || (senderSpinBox && senderSpinBox == ui->vitalitySpinBox))
    {
        updateTableItemStat(ui->statsTableWidget->item(0, 0), diff, statsPerStep.life); // current life
        updateTableItemStat(ui->statsTableWidget->item(0, 1), diff, statsPerStep.life); // base life
        updateTableItemStat(ui->statsTableWidget->item(2, 0), diff, statsPerStep.stamina); // current stamina
        updateTableItemStat(ui->statsTableWidget->item(2, 1), diff, statsPerStep.stamina); // base stamina
    }
    if (!senderSpinBox || (senderSpinBox && senderSpinBox == ui->energySpinBox))
    {
        updateTableItemStat(ui->statsTableWidget->item(1, 0), diff, statsPerStep.mana); // current mana
        updateTableItemStat(ui->statsTableWidget->item(1, 1), diff, statsPerStep.mana); // base mana
    }
}

void MedianXLOfflineTools::updateTableItemStat(QTableWidgetItem *item, int diff, int statPerPoint)
{
    double newValue = item->text().toDouble() + diff * statPerPoint / 4.0; //-V636
    if (newValue > 8191)
        newValue = 8191;

    if (newValue > 0)
    {
        double foo;
        item->setText(modf(newValue, &foo) > DBL_EPSILON ? QString::number(newValue, 'f', 1) : QString::number(newValue));
    }
    else
        item->setText("0");
}

void MedianXLOfflineTools::updateStatusTips(int newStatPoints, int investedStatPoints, int newSkillPoints, int investedSkillPoints)
{
    updateMaxCompoundStatusTip(ui->freeStatPointsLineEdit, newStatPoints, investedStatPoints);
    updateMaxCompoundStatusTip(ui->freeSkillPointsLineEdit, newSkillPoints, investedSkillPoints);
    ui->inventoryGoldLineEdit->setStatusTip(maxValueFormat.arg(QLocale().toString(_oldClvl * Enums::CharacterStats::InventoryGoldFactor)));
}

void MedianXLOfflineTools::updateCompoundStatusTip(QWidget *widget, const QString &firstString, const QString &secondString)
{
    widget->setStatusTip(kCompoundFormat.arg(firstString, secondString));
}

void MedianXLOfflineTools::updateMinCompoundStatusTip(QWidget *widget, int minValue, int investedValue)
{
    updateCompoundStatusTip(widget, minValueFormat.arg(minValue), investedValueFormat.arg(investedValue));
}

void MedianXLOfflineTools::updateMaxCompoundStatusTip(QWidget *widget, int maxValue, int investedValue)
{
    updateCompoundStatusTip(widget, maxValueFormat.arg(maxValue), investedValueFormat.arg(investedValue));
}

void MedianXLOfflineTools::updateAssociateAction(bool disable)
{
    ui->actionAssociate->setDisabled(disable);
    ui->actionAssociate->setStatusTip(disable ? tr("Application is default already") : QString());
}

void MedianXLOfflineTools::updateCharacterExperienceProgressbar(quint32 newExperience)
{
    if (_oldClvl == Enums::CharacterStats::MaxLevel)
    {
        // display maxlevel as 100% of progressbar
        _expGroupBox->setPreviousLevelExperience(experienceTable.at(_oldClvl - 2));
        _expGroupBox->setNextLevelExperience(newExperience);
    }
    else
    {
        _expGroupBox->setPreviousLevelExperience(experienceTable.at(_oldClvl - 1));
        _expGroupBox->setNextLevelExperience(experienceTable.at(_oldClvl));
    }
    _expGroupBox->setCurrentExperience(newExperience);
}

QByteArray MedianXLOfflineTools::statisticBytes()
{
    QString result;
    QMetaEnum statisticMetaEnum = Enums::CharacterStats::statisticMetaEnum();
    bool isExpAndLevelNotSet = true;
    QList<QVariant> achievements = CharacterInfo::instance().basicInfo.statsDynamicData.values(Enums::CharacterStats::Achievements);
    for (int i = 0, achievementIndex = 0; i < statisticMetaEnum.keyCount(); ++i)
    {
        Enums::CharacterStats::StatisticEnum statCode = static_cast<Enums::CharacterStats::StatisticEnum>(statisticMetaEnum.value(i));
        bool isAchievement = false;
        quint32 value = 0;
        if (statCode >= Enums::CharacterStats::Strength && statCode <= Enums::CharacterStats::Vitality)
        {
            value = _spinBoxesStatsMap[statCode]->value();
        }
        else if (statCode >= Enums::CharacterStats::Life && statCode <= Enums::CharacterStats::BaseStamina)
        {
            int j = statCode - Enums::CharacterStats::Life, row = j / 2, col = j % 2;
            value = static_cast<qulonglong>(ui->statsTableWidget->item(row, col)->text().toDouble()) << 8;
        }
        else if (statCode == Enums::CharacterStats::Achievements)
        {
            isAchievement = true;
        }
        else if (statCode == Enums::CharacterStats::SignetsOfSkillEaten)
        {
            value = CharacterInfo::instance().valueOfStatistic(statCode);
        }
        else if (statCode != Enums::CharacterStats::End && statCode != Enums::CharacterStats::Level && statCode != Enums::CharacterStats::Experience)
        {
            value = _lineEditsStatsMap[statCode]->text().toULongLong();
#ifndef MAKE_FINISHED_CHARACTER
            if (statCode == Enums::CharacterStats::FreeStatPoints)
            {
                int totalPossibleFreeStats = totalPossibleStatPoints(ui->levelSpinBox->value()) - investedStatPoints();
                if (value > static_cast<quint32>(totalPossibleFreeStats)) // prevent hacks and shut the compiler up
                {
                    value = totalPossibleFreeStats;
                    WARNING_BOX(kHackerDetected);
                }
            }
#endif
        }
        else if (statCode != Enums::CharacterStats::End && isExpAndLevelNotSet) // level or exp
        {
            CharacterInfo &charInfo = CharacterInfo::instance();
            quint8 clvl = CharacterInfo::instance().basicInfo.level, newClvl = ui->levelSpinBox->value();
            if (clvl != newClvl) // set new level and experience explicitly
            {
                addStatisticBits(result, Enums::CharacterStats::Level, Enums::CharacterStats::StatCodeLength);
                addStatisticBits(result, newClvl, ItemDataBase::Properties()->value(Enums::CharacterStats::Level)->bitsSave);
                charInfo.setValueForStatistic(newClvl, Enums::CharacterStats::Level);

                quint32 newExp = experienceTable.at(newClvl - 1);
                if (newExp) // must not be present for level 1 character
                {
                    addStatisticBits(result, Enums::CharacterStats::Experience, Enums::CharacterStats::StatCodeLength);
                    addStatisticBits(result, newExp, ItemDataBase::Properties()->value(Enums::CharacterStats::Experience)->bitsSave);
                }
                charInfo.setValueForStatistic(newExp, Enums::CharacterStats::Experience);

                isExpAndLevelNotSet = false;
                continue;
            }
            else
                value = charInfo.valueOfStatistic(statCode);
        }
        else if (statCode == Enums::CharacterStats::End) // byte align
        {
            addStatisticBits(result, statCode, 16 - result.length() % 8);
            break; // not necessary actually
        }
        else
            continue;

        if (value || isAchievement)
        {
            if (!isAchievement || !achievements.isEmpty())
                addStatisticBits(result, statCode, Enums::CharacterStats::StatCodeLength);

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

        if (!isAchievement)
            CharacterInfo::instance().setValueForStatistic(value, statCode);
    }

    int bitsCount = result.length();
    if (bitsCount % 8)
    {
        ERROR_BOX(tr("Stats string is not byte aligned!"));
        return QByteArray();
    }

    QByteArray resultBytes;
    resultBytes.reserve(bitsCount / 8);
    for (int startPos = bitsCount - 8; startPos >= 0; startPos -= 8)
    {
        quint8 aByte = result.mid(startPos, 8).toUShort(0, 2);
        resultBytes += aByte;
    }
    return resultBytes;
}

void MedianXLOfflineTools::addStatisticBits(QString &bitsString, quint64 number, int fieldWidth)
{
    bitsString.prepend(binaryStringFromNumber(number, false, fieldWidth));
}

QString MedianXLOfflineTools::backupFile(QFile &file)
{
    if (!ui->actionBackup->isChecked() || !file.exists())
        return QString();

    QString dateOrTimestamp = ui->actionBackupFormatTimestamp->isChecked() ? QString::number(QDateTime::currentDateTime().toSecsSinceEpoch())
                                                                           : QDateTime::currentDateTime().toString(kTimeFormatReadable);
    QString backupPath = QString("%1_%2.%3").arg(file.fileName(), dateOrTimestamp, kBackupExtension);
    if (!QFile::copy(file.fileName(), backupPath))
    {
        showErrorMessageBoxForFile(tr("Error creating backup for file '%1'"), file);
        return QString();
    }

    // enforce the configured backup limit for this file (0/unchecked action data means unlimited)
    int limit = _backupLimitsGroup->checkedAction()->data().toInt();
    if (limit > 0)
    {
        QFileInfo fileInfo(file);
        QDir dir(fileInfo.absolutePath());
        QStringList existingBackups = dir.entryList(QStringList() << fileInfo.fileName() + "_*." + kBackupExtension, QDir::Files, QDir::Time);
        while (existingBackups.size() > limit)
            QFile::remove(dir.filePath(existingBackups.takeLast()));
    }

    return QDir::toNativeSeparators(backupPath);
}

void MedianXLOfflineTools::showErrorMessageBoxForFile(const QString &message, const QFile &file)
{
    CUSTOM_BOX_OK(critical, message.arg(QDir::toNativeSeparators(file.fileName())) + "\n" + tr("Reason: %1", "error with file").arg(file.errorString()));
}

bool MedianXLOfflineTools::maybeSave()
{
    if (isWindowModified())
    {
        QMessageBox msgBox(QMessageBox::Warning, qApp->applicationName(), tr("Character has been modified."), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
        msgBox.setInformativeText(tr("Do you want to save your changes?"));
        msgBox.setDefaultButton(QMessageBox::Save);
        msgBox.setWindowModality(Qt::WindowModal);
        switch (msgBox.exec())
        {
        case QMessageBox::Save:
            saveCharacter();
            break;
        case QMessageBox::Cancel:
            return false;
        case QMessageBox::Discard:
            setModified(false);
            break;
        default:
            break;
        }
    }
    return true;
}

void MedianXLOfflineTools::networkReplyCheckForUpdateFinished(QNetworkReply *reply)
{
    if (!reply->error())
        displayInfoAboutServerVersion(reply->readAll().trimmed());
    reply->deleteLater();
    _qnamCheckForUpdate->deleteLater();
}

void MedianXLOfflineTools::displayInfoAboutServerVersion(const QByteArray &version)
{
    QList<QByteArray> versions = version.split('\n');
#if !IS_RELEASE_BUILD
    Q_ASSERT(versions.size() == 2);
#endif

    QString updateText;
    QByteArray appVersion = versions.at(0), modDataVersion = versions.at(1);
    QLatin1String appVersionStr(appVersion);
    if (isVersionLess(qApp->applicationVersion(), appVersionStr))
        updateText = tr("New app version <b>%1</b> is available!").arg(appVersionStr);
    else if (!_modDataVersion.isEmpty() && isVersionLess(_modDataVersion, modDataVersion))
        updateText = tr("New mod data <b>%1</b> is available!").arg(QLatin1String(modDataVersion));

    if (!updateText.isEmpty())
        INFO_BOX(updateText + kHtmlLineBreak + kHtmlLineBreak + kForumThreadHtmlLinks);
    else if (_isManuallyCheckingForUpdate)
        INFO_BOX(tr("You have the latest version"));
}

void MedianXLOfflineTools::fileContentsChanged()
{
    if (_isFileChangedMessageBoxRunning)
        return;

    // more than 1 file can change at once (.d2s, .d2x, .sss), but the question box must be shown only after the last change is signaled
    if (!_fileChangeTimer)
    {
        _fileChangeTimer = new QTimer;
        _fileChangeTimer->setSingleShot(true);
        connect(_fileChangeTimer, SIGNAL(timeout()), SLOT(fileChangeTimerFired()));
    }
    _fileChangeTimer->start(500);
}

void MedianXLOfflineTools::fileChangeTimerFired()
{
    qApp->alert(this, 3000);

    _isFileChangedMessageBoxRunning = true;
    if (QUESTION_BOX_YESNO(tr("The character and/or extended stashes have been modified externally.\nDo you want to reload them?"), QMessageBox::Yes) == QMessageBox::Yes)
        reloadCharacter();
    _isFileChangedMessageBoxRunning = false;

    delete _fileChangeTimer; _fileChangeTimer = 0;
}
