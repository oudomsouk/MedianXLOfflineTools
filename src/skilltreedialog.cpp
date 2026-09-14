#include "skilltreedialog.h"
#include "characterinfo.hpp"
#include "itemdatabase.h"
#include "resourcepathmanager.hpp"
#include "colorsmanager.h"

#include <QTabWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QPixmap>

#include <QSettings>


class SkillWidget : public QWidget
{
public:
    SkillWidget(QWidget *parent = 0) : QWidget(parent), _skillNameLabel(new QLabel(this)), _skillImageLabel(new QLabel(this)), _skillPointsLabel(new QLabel(this))
    {
        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->addWidget(_skillNameLabel,   0, Qt::AlignHCenter);
        layout->addWidget(_skillImageLabel,  0, Qt::AlignHCenter);
        layout->addWidget(_skillPointsLabel, 0, Qt::AlignHCenter);
    }
    virtual ~SkillWidget() {}

    void setSkillName(const QString &name) { _skillNameLabel->setText(name); }

    void setSkillImageForClassWithId(int classCode, int imageId)
    {
        QString path = ResourcePathManager::pathForSkillImage(classCode, imageId);
        if (!QFile::exists(path))
            path = ResourcePathManager::pathForSkillImage(classCode, --imageId);
        _skillImageLabel->setPixmap(QPixmap(path));
    }

    void setSkillPoints(quint8 basePoints)
    {
        quint8 maxClvl = Enums::CharacterStats::MaxLevel;
        quint8 actualBasePoints = qMin(maxClvl, basePoints);

        _skillPointsLabel->setText(QString::number(actualBasePoints));
        if (!basePoints)
            setDisabled(true);
    }

private:
    QLabel *_skillNameLabel, *_skillImageLabel, *_skillPointsLabel;
};


// This dialog only shows base skill points invested by the character. Item-granted skill bonuses are not
// shown, since this stripped-down build no longer parses items.
SkillTreeDialog::SkillTreeDialog(QWidget *parent /*= 0*/) : QDialog(parent), _tabWidget(new QTabWidget(this))
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(tr("Skill tree"));

    const CharacterInfo::CharacterInfoBasic &charInfo = CharacterInfo::instance().basicInfo;

    // TODO: query number of tabs dynamically for each class
    // TODO: tab names
    const QList<int> skillsVisualOrder = Enums::Skills::currentCharacterSkillsIndexes().second;
    for (int i = 0, j = 0, skillsNumber = skillsVisualOrder.size(); i < 8; ++i)
    {
        int tabIndex = i + 1;
        QWidget *tab = new QWidget(this);
        _tabWidget->addTab(tab, tr("Tab %1").arg(tabIndex));

        QGridLayout *grid = new QGridLayout(tab);
        grid->setContentsMargins(QMargins());

        while (j < skillsNumber)
        {
            int skillIndex = skillsVisualOrder.at(j);
            SkillInfo *skill = ItemDataBase::Skills()->value(skillIndex);
            if (skill->tab != tabIndex)
                break;

            qint32 baseSkillPoints = charInfo.skillsReadable.at(j);

            QString skillName = skill->name;
            foreach (const QByteArray &color, ColorsManager::colorStrings())
                skillName.remove(color);

            SkillWidget *w = new SkillWidget(tab);
            w->setSkillName(skillName);
            w->setSkillImageForClassWithId(charInfo.classCode, skill->imageId);
            w->setSkillPoints(baseSkillPoints);
            grid->addWidget(w, skill->row - 1, skill->col - 1);
            ++j;
        }
    }

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, Qt::Horizontal, this);
    connect(buttonBox, SIGNAL(rejected()), SLOT(reject()));

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(_tabWidget);
    layout->addWidget(buttonBox);

    QVariant savedPos = QSettings().value("skillTreePos");
    if (savedPos.isValid())
        move(savedPos.toPoint());
}

void SkillTreeDialog::reject()
{
    QSettings().setValue("skillTreePos", pos());
    QDialog::reject();
}
