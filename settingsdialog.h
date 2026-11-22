#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>


namespace Ui { class SettingsDialog; }

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(int currentChannel, bool globalSoak, bool globalPowerEq, QWidget *parent = nullptr);
    ~SettingsDialog();

    int selectedChannel() const;
    bool isGlobalSoak() const;
    bool isGlobalPowerEq() const;

private:
    Ui::SettingsDialog *ui;
};


#endif // SETTINGSDIALOG_H
