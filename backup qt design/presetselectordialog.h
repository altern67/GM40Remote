#ifndef PRESETSELECTORDIALOG_H
#define PRESETSELECTORDIALOG_H

#include <QDialog>
#include <QStringList>

namespace Ui { class PresetSelectorDialog; }

class PresetSelectorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PresetSelectorDialog(const QStringList &presetNames, int currentPreset, QWidget *parent = nullptr);
    ~PresetSelectorDialog();

    int selectedPresetIndex() const;

private:
    Ui::PresetSelectorDialog *ui;
};

#endif // PRESETSELECTORDIALOG_H
