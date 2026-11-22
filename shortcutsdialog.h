#ifndef SHORTCUTSDIALOG_H
#define SHORTCUTSDIALOG_H

#include <QDialog>
#include <QMap>
#include <QString>

namespace Ui {
class ShortcutsDialog;
}

class ShortcutsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ShortcutsDialog(const QMap<QString, QString> &currentShortcuts, QWidget *parent = nullptr);
    ~ShortcutsDialog();

    QMap<QString, QString> shortcuts() const;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void setDefaults();

private:
    Ui::ShortcutsDialog *ui;
    QMap<QObject*, QString> m_fieldTexts; // Stocke la liste des touches pour chaque champ
    void updateFieldDisplay(QWidget *editWidget);
};

#endif // SHORTCUTSDIALOG_H
