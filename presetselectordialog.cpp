#include "presetselectordialog.h"
#include "ui_presetselectordialog.h"
#include <QListWidgetItem>

PresetSelectorDialog::PresetSelectorDialog(const QStringList &presetNames, int currentPreset, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PresetSelectorDialog)
{
    ui->setupUi(this);

    const QString slotsStr = "ABCD";
    for (int i = 0; i < presetNames.size(); ++i) {
        int bank = (i / 4) + 1;
        int slotIdx = i % 4;
        QString label = QString("%1  [%2-%3]  —  %4")
                            .arg(i + 1, 3, 10, QChar('0'))
                            .arg(bank)
                            .arg(slotsStr.at(slotIdx))
                            .arg(presetNames.at(i));

        auto *item = new QListWidgetItem(label, ui->presetListWidget);
        item->setData(Qt::UserRole, i); // Sauvegarde l'index exact (0..127)

        // Style gris et italique pour les emplacements vides
        if (presetNames.at(i).compare("Empty Preset", Qt::CaseInsensitive) == 0) {
            item->setForeground(QColor("#888888"));
            QFont f = item->font();
            f.setItalic(true);
            item->setFont(f);
        }
    }

    // Sélectionne et centre sur le preset actif
    if (currentPreset >= 0 && currentPreset < ui->presetListWidget->count()) {
        ui->presetListWidget->setCurrentRow(currentPreset);
        ui->presetListWidget->scrollToItem(ui->presetListWidget->currentItem(), QAbstractItemView::PositionAtCenter);
    }

    // Filtrage instantané lors de la frappe
    connect(ui->searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int i = 0; i < ui->presetListWidget->count(); ++i) {
            QListWidgetItem *item = ui->presetListWidget->item(i);
            item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
        }
    });

    // Double-clic valide immédiatement le choix
    connect(ui->presetListWidget, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
}

PresetSelectorDialog::~PresetSelectorDialog()
{
    delete ui;
}

int PresetSelectorDialog::selectedPresetIndex() const
{
    if (auto *item = ui->presetListWidget->currentItem()) {
        return item->data(Qt::UserRole).toInt();
    }
    return -1;
}
