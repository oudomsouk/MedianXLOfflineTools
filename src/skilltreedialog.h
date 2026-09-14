#ifndef SKILLTREEDIALOG_H
#define SKILLTREEDIALOG_H

#include <QDialog>


class QTabWidget;

class SkillTreeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SkillTreeDialog(QWidget *parent = 0);
    virtual ~SkillTreeDialog() {}

public slots:
    virtual void reject();

private:
    QTabWidget *_tabWidget;
};

#endif // SKILLTREEDIALOG_H
