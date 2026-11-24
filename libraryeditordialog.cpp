#include "libraryeditordialog.h"
#include "ui_libraryeditordialog.h"
#include "presetselectordialog.h"

#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QDir>
#include <QDropEvent>
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>

LibraryEditorDialog::LibraryEditorDialog(const QVector<PresetData> &activePresets,
                                         const QString &activeLibraryPath,
                                         QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LibraryEditorDialog)
    , m_leftPresets(activePresets)
    , m_leftPath(activeLibraryPath)
{
    ui->setupUi(this);

    // 1. Active la sélection multiple et le glisser-déposer (Droite -> Gauche)
    ui->listLeft->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->listRight->setSelectionMode(QAbstractItemView::ExtendedSelection);

    ui->listRight->setDragEnabled(true);
    ui->listRight->setDragDropMode(QAbstractItemView::DragOnly);

    ui->listLeft->setAcceptDrops(true);
    ui->listLeft->setDropIndicatorShown(true);
    ui->listLeft->setDragDropMode(QAbstractItemView::DropOnly);
    ui->listLeft->viewport()->installEventFilter(this);
    // 2. Ajoute dynamiquement la case à cocher "Auto-Audition" sous le bouton Audition
    m_chkAutoAudition = new QCheckBox("Auto-Audition on click", this);
    m_chkAutoAudition->setToolTip("Automatically send the selected preset to the amplifier when clicked");
    if (auto *actionsLayout = qobject_cast<QVBoxLayout*>(ui->actionsGroup->layout())) {
        int auditionIdx = actionsLayout->indexOf(ui->btnAudition);
        if (auditionIdx >= 0) {
            actionsLayout->insertWidget(auditionIdx + 1, m_chkAutoAudition);
        } else {
            actionsLayout->addWidget(m_chkAutoAudition);
        }
    }
    // 3. Création dynamique du panneau de prévisualisation des réglages en bas de la fenêtre
    m_lblPreview = new QLabel(this);
    m_lblPreview->setWordWrap(true);
    m_lblPreview->setTextFormat(Qt::RichText);
    m_lblPreview->setStyleSheet(
        "QLabel {"
        "  background-color: #1e2229;"
        "  color: #e0e6ed;"
        "  border: 1px solid #3a404c;"
        "  border-radius: 6px;"
        "  padding: 8px 12px;"
        "  font-size: 12px;"
        "}"
        );
    if (auto *mainLay = qobject_cast<QVBoxLayout*>(this->layout())) {
        int btnBoxIdx = mainLay->indexOf(ui->buttonBox);
        if (btnBoxIdx >= 0) {
            mainLay->insertWidget(btnBoxIdx, m_lblPreview);
        } else {
            mainLay->addWidget(m_lblPreview);
        }
    }

    // Affiche le nom du fichier actif à gauche
    QString leftDisplay = m_leftPath.isEmpty() ? "Unsaved Library" : QFileInfo(m_leftPath).fileName();
    ui->lblLeftPath->setText(QString("File: %1").arg(leftDisplay));

    // Charge par défaut la banque d'usine dans le panneau de droite
    m_rightPresets.resize(128);
    for (int i = 0; i < 128; ++i) {
        m_rightPresets[i].number = i + 1;
        m_rightPresets[i].name = QString("Preset %1").arg(i + 1);
    }
    loadGm40FromFile(":/factory.gm40", m_rightPresets);
    m_rightPath = ":/factory.gm40";
    ui->lblRightPath->setText("Source: Factory Bank");

    refreshLeftList();
    refreshRightList();

    if (ui->listLeft->count() > 0) ui->listLeft->setCurrentRow(0);
    if (ui->listRight->count() > 0) ui->listRight->setCurrentRow(0);

    // Filtres de recherche instantanés
    connect(ui->searchLeft, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int i = 0; i < ui->listLeft->count(); ++i) {
            QListWidgetItem *item = ui->listLeft->item(i);
            item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
        }
    });

    connect(ui->searchRight, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int i = 0; i < ui->listRight->count(); ++i) {
            QListWidgetItem *item = ui->listRight->item(i);
            item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
        }
    });

    // Connexion des boutons d'action
    connect(ui->btnOpenRight,       &QPushButton::clicked, this, &LibraryEditorDialog::onLoadRightFile);
    connect(ui->btnImportFromRight, &QPushButton::clicked, this, &LibraryEditorDialog::onCopyRightToLeft);
    connect(ui->btnCopyInternal,    &QPushButton::clicked, this, &LibraryEditorDialog::onCopyLeftInternal);
    connect(ui->btnSwapInternal,    &QPushButton::clicked, this, &LibraryEditorDialog::onSwapLeftInternal);
    connect(ui->btnMoveUp,          &QPushButton::clicked, this, &LibraryEditorDialog::onMoveUp);
    connect(ui->btnMoveDown,        &QPushButton::clicked, this, &LibraryEditorDialog::onMoveDown);
    connect(ui->btnRename,          &QPushButton::clicked, this, &LibraryEditorDialog::onRenameLeft);
    connect(ui->btnInitPreset,      &QPushButton::clicked, this, &LibraryEditorDialog::onInitLeftPreset);
    connect(ui->btnSaveLeft,        &QPushButton::clicked, this, &LibraryEditorDialog::onSaveLeft);
    connect(ui->btnSaveAsLeft,      &QPushButton::clicked, this, &LibraryEditorDialog::onSaveAsLeft);

    // Double-clic à gauche = Renommer
    connect(ui->listLeft, &QListWidget::itemDoubleClicked, this, &LibraryEditorDialog::onRenameLeft);

    // Pré-écoute manuelle sur l'ampli (depuis gauche ou droite)
    connect(ui->btnAudition, &QPushButton::clicked, this, [this]() {
        if (ui->listRight->hasFocus() && ui->listRight->currentRow() >= 0) {
            int rRow = ui->listRight->currentRow();
            if (rRow < m_rightPresets.size()) emit auditionPresetRequested(m_rightPresets[rRow]);
            return;
        }
        int row = ui->listLeft->currentRow();
        if (row >= 0 && row < m_leftPresets.size()) {
            emit auditionPresetRequested(m_leftPresets[row]);
        }
    });

    // Clic / sélection dans la bibliothèque active (gauche)
    connect(ui->listLeft, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < m_leftPresets.size()) {
            updatePreview(m_leftPresets[row], "Active Library");
            if (m_chkAutoAudition && m_chkAutoAudition->isChecked()) {
                emit auditionPresetRequested(m_leftPresets[row]);
            }
        }
    });
    // Clic / sélection dans la bibliothèque source (droite)
    connect(ui->listRight, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < m_rightPresets.size()) {
            updatePreview(m_rightPresets[row], "Source Library");
            if (m_chkAutoAudition && m_chkAutoAudition->isChecked()) {
                emit auditionPresetRequested(m_rightPresets[row]);
            }
        }
    });
    // Initialise l'aperçu sur le premier preset actif dès l'ouverture
    if (!m_leftPresets.isEmpty()) {
        updatePreview(m_leftPresets[0], "Active Library");
    }
}

LibraryEditorDialog::~LibraryEditorDialog()
{
    delete ui;
}

bool LibraryEditorDialog::eventFilter(QObject *obj, QEvent *event)
{
    // Interception du glisser-déposer depuis la colonne de droite vers la colonne de gauche
    if (obj == ui->listLeft->viewport()) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto *dragEvent = static_cast<QDropEvent*>(event);
            if (dragEvent->source() == ui->listRight) {
                dragEvent->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto *dropEvent = static_cast<QDropEvent*>(event);
            if (dropEvent->source() == ui->listRight) {
                QListWidgetItem *targetItem = ui->listLeft->itemAt(dropEvent->position().toPoint());
                int destRow = targetItem ? ui->listLeft->row(targetItem) : ui->listLeft->currentRow();
                if (destRow < 0) destRow = 0;

                ui->listLeft->setCurrentRow(destRow);
                onCopyRightToLeft(); // Importe la sélection (simple ou multiple) à l'endroit déposé
                dropEvent->acceptProposedAction();
                return true;
            }
        }
    }
    return QDialog::eventFilter(obj, event);
}

QString LibraryEditorDialog::formatPresetRow(int index, const PresetData &p) const
{
    const QString slotsStr = "ABCD";
    const QStringList channels = {"Clean", "Crunch", "Lead", "Ultra"};

    int bank = (index / 4) + 1;
    int slotIdx = index % 4;
    int chIdx = std::clamp(static_cast<int>(std::round(p.channelType / 42.0)), 0, 3);

    return QString("%1  [%2-%3]  —  %4   (%5)")
        .arg(index + 1, 3, 10, QChar('0'))
        .arg(bank)
        .arg(slotsStr.at(slotIdx))
        .arg(p.name)
        .arg(channels.at(chIdx));
}

void LibraryEditorDialog::updatePreview(const PresetData &p, const QString &sourceTag)
{
    if (!m_lblPreview) return;

    const QStringList channels   = {"Clean", "Crunch", "Lead", "Ultra"};
    const QStringList modTypes   = {"Chorus", "Flanger", "Phaser", "Tremolo"};
    const QStringList soakLevels = {"No Speaker", "1W", "5W", "20W", "40W"};

    int chIdx   = std::clamp(static_cast<int>(std::round(p.channelType / 42.0)), 0, 3);
    int soakIdx = std::clamp(static_cast<int>(std::round(p.powerSoak / 31.0)), 0, 4);
    int modType = std::clamp(p.modType / 64, 0, 3);
    int modRate = (p.modType == 255) ? 63 : (p.modType % 64);

    // Conversion approximative du Delay Time (0..255) en ms (51 ms à 1360 ms)
    int delayMs = static_cast<int>(std::round(51.0 + (p.delayTime * 5.13333)));

    QString boostStr = (p.channelBoost != 0) ? "<font color='#ff5555'><b>ON</b></font>" : "OFF";
    QString gateStr  = (p.noiseGate != 0)    ? "<font color='#45e28a'><b>ON</b></font>" : "OFF";
    QString loopStr  = (p.fxLoop != 0)       ? "<font color='#45e28a'><b>ON</b></font>" : "OFF";

    QString html = QString(
                       "<b>[%1] #%2 — %3</b> &nbsp;|&nbsp; "
                       "Channel: <font color='#4da6ff'><b>%4</b></font> &nbsp;|&nbsp; "
                       "Power Soak: <b>%5</b> &nbsp;|&nbsp; "
                       "Boost: %6 &nbsp;|&nbsp; Noise Gate: %7 &nbsp;|&nbsp; FX Loop: %8<br>"
                       "<b>Preamp &amp; EQ:</b> Gain: <b>%9</b> &nbsp;•&nbsp; Vol: <b>%10</b> &nbsp;•&nbsp; "
                       "Bass: <b>%11</b> &nbsp;•&nbsp; Mid: <b>%12</b> &nbsp;•&nbsp; Treble: <b>%13</b> &nbsp;•&nbsp; "
                       "Resonance: <b>%14</b> &nbsp;•&nbsp; Presence: <b>%15</b><br>"
                       "<b>Effects:</b> Reverb: <b>%16</b> &nbsp;|&nbsp; "
                       "Delay: <b>%17</b> (%18 ms, FB: %19) &nbsp;|&nbsp; "
                       "Modulation: <b>%20</b> (Int: %21, Rate: %22)"
                       )
                       .arg(sourceTag)
                       .arg(p.number, 3, 10, QChar('0'))
                       .arg(p.name.toHtmlEscaped())
                       .arg(channels.at(chIdx))
                       .arg(soakLevels.at(soakIdx))
                       .arg(boostStr, gateStr, loopStr)
                       .arg(p.gain).arg(p.volume)
                       .arg(p.bass).arg(p.mid).arg(p.treble)
                       .arg(p.resonance).arg(p.presence)
                       .arg(p.reverb)
                       .arg(p.delayLevel).arg(delayMs).arg(p.delayFeedback)
                       .arg(modTypes.at(modType)).arg(p.modIntensity).arg(modRate);

    m_lblPreview->setText(html);
}


void LibraryEditorDialog::refreshLeftList()
{
    int prevRow = ui->listLeft->currentRow();
    ui->listLeft->blockSignals(true);
    ui->listLeft->clear();

    for (int i = 0; i < m_leftPresets.size(); ++i) {
        m_leftPresets[i].number = i + 1;
        auto *item = new QListWidgetItem(formatPresetRow(i, m_leftPresets[i]), ui->listLeft);
        item->setData(Qt::UserRole, i);

        // Style gris et italique pour les emplacements vides
        if (m_leftPresets[i].name.compare("Empty Preset", Qt::CaseInsensitive) == 0) {
            item->setForeground(QColor("#888888"));
            QFont f = item->font();
            f.setItalic(true);
            item->setFont(f);
        }
    }

    if (prevRow >= 0 && prevRow < ui->listLeft->count()) {
        ui->listLeft->setCurrentRow(prevRow);
    }
    ui->listLeft->blockSignals(false);

    if (!ui->searchLeft->text().isEmpty()) {
        emit ui->searchLeft->textChanged(ui->searchLeft->text());
    }
}

void LibraryEditorDialog::refreshRightList()
{
    ui->listRight->blockSignals(true);
    ui->listRight->clear();
    for (int i = 0; i < m_rightPresets.size(); ++i) {
        auto *item = new QListWidgetItem(formatPresetRow(i, m_rightPresets[i]), ui->listRight);
        item->setData(Qt::UserRole, i);

        // Style gris et italique pour les emplacements vides
        if (m_rightPresets[i].name.compare("Empty Preset", Qt::CaseInsensitive) == 0) {
            item->setForeground(QColor("#888888"));
            QFont f = item->font();
            f.setItalic(true);
            item->setFont(f);
        }
    }
    ui->listRight->blockSignals(false);

    if (!ui->searchRight->text().isEmpty()) {
        emit ui->searchRight->textChanged(ui->searchRight->text());
    }
}


void LibraryEditorDialog::onLoadRightFile()
{
    QString file = QFileDialog::getOpenFileName(this,
                                                "Open Source GM40 Library",
                                                QDir::homePath(),
                                                "GM40 Files (*.gm40)");
    if (file.isEmpty()) return;

    if (loadGm40FromFile(file, m_rightPresets)) {
        m_rightPath = file;
        ui->lblRightPath->setText(QString("Source: %1").arg(QFileInfo(file).fileName()));
        refreshRightList();
    } else {
        QMessageBox::warning(this, "Error", "Unable to read the selected .gm40 file.");
    }
}

void LibraryEditorDialog::onCopyRightToLeft()
{
    QList<QListWidgetItem*> selectedRight = ui->listRight->selectedItems();
    int destStartRow = ui->listLeft->currentRow();

    if (selectedRight.isEmpty() || destStartRow < 0 || destStartRow >= m_leftPresets.size()) return;

    // Trie les éléments sélectionnés par ordre croissant d'index
    QList<int> srcIndices;
    for (QListWidgetItem *item : selectedRight) {
        srcIndices.append(ui->listRight->row(item));
    }
    std::sort(srcIndices.begin(), srcIndices.end());

    // Demande confirmation si plusieurs presets vont être écrasés
    if (srcIndices.size() > 1) {
        int endSlot = std::min(128, destStartRow + static_cast<int>(srcIndices.size()));
        auto reply = QMessageBox::question(this,
                                           "Batch Import",
                                           QString("Import %1 presets into slots #%2 to #%3?")
                                               .arg(srcIndices.size())
                                               .arg(destStartRow + 1)
                                               .arg(endSlot),
                                           QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
    }
    int targetRow = destStartRow;
    for (int srcRow : srcIndices) {
        if (targetRow >= m_leftPresets.size()) break;
        if (srcRow >= 0 && srcRow < m_rightPresets.size()) {
            PresetData imported = m_rightPresets[srcRow];
            imported.number = targetRow + 1;
            m_leftPresets[targetRow] = imported;
            targetRow++;
        }
    }
    refreshLeftList();
    ui->listLeft->setCurrentRow(destStartRow);
}
void LibraryEditorDialog::onCopyLeftInternal()
{
    int srcRow = ui->listLeft->currentRow();
    if (srcRow < 0 || srcRow >= m_leftPresets.size()) return;
    QStringList names;
    for (const auto &p : m_leftPresets) names.append(p.name);
    PresetSelectorDialog dlg(names, srcRow, this);
    dlg.setWindowTitle(QString("Copy '%1' to which slot?").arg(m_leftPresets[srcRow].name));
    if (dlg.exec() == QDialog::Accepted) {
        int destRow = dlg.selectedPresetIndex();
        if (destRow >= 0 && destRow < m_leftPresets.size()) {
            PresetData copy = m_leftPresets[srcRow];
            copy.number = destRow + 1;
            m_leftPresets[destRow] = copy;
            refreshLeftList();
            ui->listLeft->setCurrentRow(destRow);
        }
    }
}
void LibraryEditorDialog::onSwapLeftInternal()
{
    int srcRow = ui->listLeft->currentRow();
    if (srcRow < 0 || srcRow >= m_leftPresets.size()) return;
    QStringList names;
    for (const auto &p : m_leftPresets) names.append(p.name);
    PresetSelectorDialog dlg(names, srcRow, this);
    dlg.setWindowTitle(QString("Swap '%1' with which slot?").arg(m_leftPresets[srcRow].name));
    if (dlg.exec() == QDialog::Accepted) {
        int destRow = dlg.selectedPresetIndex();
        if (destRow >= 0 && destRow < m_leftPresets.size() && destRow != srcRow) {
            std::swap(m_leftPresets[srcRow], m_leftPresets[destRow]);
            refreshLeftList();
            ui->listLeft->setCurrentRow(destRow);
        }
    }
}
void LibraryEditorDialog::onMoveUp()
{
    int row = ui->listLeft->currentRow();
    if (row <= 0 || row >= m_leftPresets.size()) return;
    std::swap(m_leftPresets[row], m_leftPresets[row - 1]);
    refreshLeftList();
    ui->listLeft->setCurrentRow(row - 1);
}
void LibraryEditorDialog::onMoveDown()
{
    int row = ui->listLeft->currentRow();
    if (row < 0 || row >= m_leftPresets.size() - 1) return;
    std::swap(m_leftPresets[row], m_leftPresets[row + 1]);
    refreshLeftList();
    ui->listLeft->setCurrentRow(row + 1);
}
void LibraryEditorDialog::onRenameLeft()
{
    int row = ui->listLeft->currentRow();
    if (row < 0 || row >= m_leftPresets.size()) return;
    bool ok = false;
    QString newName = QInputDialog::getText(this,
                                            "Rename Preset",
                                            QString("New name for slot #%1:").arg(row + 1),
                                            QLineEdit::Normal,
                                            m_leftPresets[row].name,
                                            &ok).trimmed();
    if (ok && !newName.isEmpty()) {
        m_leftPresets[row].name = newName;
        refreshLeftList();
        ui->listLeft->setCurrentRow(row);
    }
}
void LibraryEditorDialog::onInitLeftPreset()
{
    QList<QListWidgetItem*> selected = ui->listLeft->selectedItems();
    if (selected.isEmpty()) return;
    QString msg = (selected.size() == 1)
                      ? QString("Reset slot #%1 (%2) to a clean default template?")
                            .arg(ui->listLeft->row(selected.first()) + 1)
                            .arg(m_leftPresets[ui->listLeft->row(selected.first())].name)
                      : QString("Reset %1 selected presets to a clean default template?")
                            .arg(selected.size());
    auto reply = QMessageBox::question(this, "Reset Preset(s)", msg, QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;
    for (QListWidgetItem *item : selected) {
        int row = ui->listLeft->row(item);
        if (row >= 0 && row < m_leftPresets.size()) {
            PresetData cleanDefault;
            cleanDefault.number        = row + 1;
            cleanDefault.name          = "Empty Preset";
            cleanDefault.gain          = 80;
            cleanDefault.volume        = 128;
            cleanDefault.bass          = 128;
            cleanDefault.mid           = 128;
            cleanDefault.treble        = 128;
            cleanDefault.resonance     = 128;
            cleanDefault.presence      = 128;
            cleanDefault.channelType   = 0;  // Clean
            cleanDefault.powerSoak     = 32; // 1W
            m_leftPresets[row] = cleanDefault;
        }
    }
    refreshLeftList();
}
void LibraryEditorDialog::onSaveLeft()
{
    if (m_leftPath.isEmpty() || m_leftPath.startsWith(":/")) {
        onSaveAsLeft();
        return;
    }
    if (saveGm40ToFile(m_leftPath, m_leftPresets)) {
        QMessageBox::information(this, "Saved", QString("Library saved to:\n%1").arg(m_leftPath));
    } else {
        QMessageBox::warning(this, "Error", "Unable to write to file.");
    }
}
void LibraryEditorDialog::onSaveAsLeft()
{
    QString file = QFileDialog::getSaveFileName(this,
                                                "Save Library As",
                                                "custom_library.gm40",
                                                "GM40 Files (*.gm40)");
    if (file.isEmpty()) return;
    if (!file.endsWith(".gm40", Qt::CaseInsensitive)) file += ".gm40";
    if (saveGm40ToFile(file, m_leftPresets)) {
        m_leftPath = file;
        ui->lblLeftPath->setText(QString("File: %1").arg(QFileInfo(file).fileName()));
        QMessageBox::information(this, "Saved", QString("Library exported to:\n%1").arg(file));
    }
}
bool LibraryEditorDialog::loadGm40FromFile(const QString &filePath, QVector<PresetData> &targetList)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    targetList.resize(128);
    QTextStream in(&file);
    PresetData current;
    int currentIdx = -1;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        if (line == "[preset]") {
            if (currentIdx >= 0 && currentIdx < 128) targetList[currentIdx] = current;
            current = PresetData();
        } else if (line.contains('=')) {
            int eqPos = line.indexOf('=');
            QString key = line.left(eqPos).trimmed();
            QString val = line.mid(eqPos + 1).trimmed();
            if (key == "number")             { current.number = val.toInt(); currentIdx = current.number - 1; }
            else if (key == "name")          current.name = val;
            else if (key == "modIntensity")  current.modIntensity = val.toInt();
            else if (key == "powerSoak")     current.powerSoak = val.toInt();
            else if (key == "delayTime")     current.delayTime = val.toInt();
            else if (key == "channelType")   current.channelType = val.toInt();
            else if (key == "noiseGate")     current.noiseGate = val.toInt();
            else if (key == "channelBoost")  current.channelBoost = val.toInt();
            else if (key == "bass")          current.bass = val.toInt();
            else if (key == "mid")           current.mid = val.toInt();
            else if (key == "treble")        current.treble = val.toInt();
            else if (key == "modType")       current.modType = val.toInt();
            else if (key == "resonance")     current.resonance = val.toInt();
            else if (key == "presence")      current.presence = val.toInt();
            else if (key == "delayFeedback") current.delayFeedback = val.toInt();
            else if (key == "volume")        current.volume = val.toInt();
            else if (key == "delayLevel")    current.delayLevel = val.toInt();
            else if (key == "gain")          current.gain = val.toInt();
            else if (key == "reverb")        current.reverb = val.toInt();
            else if (key == "fxLoop")        current.fxLoop = val.toInt();
        }
    }
    if (currentIdx >= 0 && currentIdx < 128) targetList[currentIdx] = current;
    return true;
}
bool LibraryEditorDialog::saveGm40ToFile(const QString &filePath, const QVector<PresetData> &sourceList)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&file);
    for (int i = 0; i < 128; ++i) {
        const PresetData &p = sourceList[i];
        out << "[preset]\n";
        out << "number=" << (i + 1) << "\n";
        out << "name=" << p.name << "\n";
        out << "modIntensity=" << p.modIntensity << "\n";
        out << "powerSoak=" << p.powerSoak << "\n";
        out << "delayTime=" << p.delayTime << "\n";
        out << "channelType=" << p.channelType << "\n";
        out << "noiseGate=" << p.noiseGate << "\n";
        out << "channelBoost=" << p.channelBoost << "\n";
        out << "bass=" << p.bass << "\n";
        out << "mid=" << p.mid << "\n";
        out << "treble=" << p.treble << "\n";
        out << "modType=" << p.modType << "\n";
        out << "resonance=" << p.resonance << "\n";
        out << "presence=" << p.presence << "\n";
        out << "delayFeedback=" << p.delayFeedback << "\n";
        out << "volume=" << p.volume << "\n";
        out << "delayLevel=" << p.delayLevel << "\n";
        out << "gain=" << p.gain << "\n";
        out << "reverb=" << p.reverb << "\n";
        out << "fxLoop=" << p.fxLoop << "\n";
    }
    return true;
}

