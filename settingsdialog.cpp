#include "settingsdialog.h"
#include "ui_settingsdialog.h"

SettingsDialog::SettingsDialog(int currentChannel, bool globalSoak, bool globalPowerEq, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);

    ui->channelCombo->clear();
    ui->channelCombo->addItem("OMNI (All MIDI Channels)", -1);
    for (int ch = 1; ch <= 16; ++ch) {
        ui->channelCombo->addItem(QString("MIDI channel %1").arg(ch), ch - 1);
    }
    int comboIndex = ui->channelCombo->findData(currentChannel);
    ui->channelCombo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 1);

    // Initialisation des cases à cocher globales
    ui->chkGlobalSoak->setChecked(globalSoak);
    ui->chkGlobalPowerEq->setChecked(globalPowerEq);
}

bool SettingsDialog::isGlobalSoak() const { return ui->chkGlobalSoak->isChecked(); }
bool SettingsDialog::isGlobalPowerEq() const { return ui->chkGlobalPowerEq->isChecked(); }


SettingsDialog::~SettingsDialog()
{
    delete ui;
}

int SettingsDialog::selectedChannel() const
{
    return ui->channelCombo->currentData().toInt();
}
