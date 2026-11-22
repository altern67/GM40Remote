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

    // Pré-écoute en direct sur l'ampli
    connect(ui->btnAudition, &QPushButton::clicked, this, [this]() {
        int row = ui->listLeft->currentRow();
        if (row >= 0 && row < m_leftPresets.size()) {
            emit auditionPresetRequested(m_leftPresets[row]);
        }
    });
}

LibraryEditorDialog::~LibraryEditorDialog()
{
    delete ui;
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

void LibraryEditorDialog::refreshLeftList()
{
    int prevRow = ui->listLeft->currentRow();
    ui->listLeft->clear();

    for (int i = 0; i < m_leftPresets.size(); ++i) {
        m_leftPresets[i].number = i + 1;
        auto *item = new QListWidgetItem(formatPresetRow(i, m_leftPresets[i]), ui->listLeft);
        item->setData(Qt::UserRole, i);
    }

    if (prevRow >= 0 && prevRow < ui->listLeft->count()) {
        ui->listLeft->setCurrentRow(prevRow);
    }
    // Réapplique le filtre actif si présent
    if (!ui->searchLeft->text().isEmpty()) {
        emit ui->searchLeft->textChanged(ui->searchLeft->text());
    }
}

void LibraryEditorDialog::refreshRightList()
{
    ui->listRight->clear();
    for (int i = 0; i < m_rightPresets.size(); ++i) {
        auto *item = new QListWidgetItem(formatPresetRow(i, m_rightPresets[i]), ui->listRight);
        item->setData(Qt::UserRole, i);
    }
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
    int srcRow  = ui->listRight->currentRow();
    int destRow = ui->listLeft->currentRow();
    if (srcRow < 0 || srcRow >= m_rightPresets.size() || destRow < 0 || destRow >= m_leftPresets.size()) return;

    PresetData imported = m_rightPresets[srcRow];
    imported.number = destRow + 1;
    m_leftPresets[destRow] = imported;

    refreshLeftList();
    ui->listLeft->setCurrentRow(destRow);
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
    int row = ui->listLeft->currentRow();
    if (row < 0 || row >= m_leftPresets.size()) return;

    auto reply = QMessageBox::question(this,
                                       "Reset Preset",
                                       QString("Reset slot #%1 (%2) to a clean default template?")
                                           .arg(row + 1)
                                           .arg(m_leftPresets[row].name),
                                       QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    PresetData cleanDefault;
    cleanDefault.number        = row + 1;
    cleanDefault.name          = QString("New Clean %1").arg(row + 1);
    cleanDefault.gain          = 80;
    cleanDefault.volume        = 128;
    cleanDefault.bass          = 128;
    cleanDefault.mid           = 128;
    cleanDefault.treble        = 128;
    cleanDefault.resonance     = 128;
    cleanDefault.presence      = 128;
    cleanDefault.channelType   = 0; // Clean
    cleanDefault.powerSoak     = 32; // 1W
    m_leftPresets[row] = cleanDefault;
    refreshLeftList();
    ui->listLeft->setCurrentRow(row);
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