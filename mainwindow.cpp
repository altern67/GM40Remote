#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "settingsdialog.h"
#include <QCoreApplication>
#include "presetselectordialog.h"
#include "shortcutsdialog.h"
#include "libraryeditordialog.h"



MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle(QString("Hughes & Kettner Remote — GM40 Linux v%1").arg(qApp->applicationVersion())); // <-- AJOUTER ICI
    setMinimumSize(1104, 1104);
    createMenus();

    // Initialisation des options des sélecteurs rotatifs
    ui->modSelector->setOptions({"Chorus","Flanger", "Phaser", "Tremolo"}, "Modulation");
    ui->channelSelector->setOptions({"Clean", "Crunch", "Lead", "Ultra"}, "Channel");

    // Émission SysEx pour les sélecteurs rotatifs (valeurs officielles H&K : 0, 42, 84, 126)
    ui->channelSelector->onSelectorChanged = [this](int index) {
        int channelValues[] = {0, 42, 84, 126};
        sendMidiSysExParam(31, channelValues[std::clamp(index, 0, 3)]);
        markPresetAsModified();
    };

    // Émission SysEx pour tous les interrupteurs On/Off
    connect(ui->btnBoost,        &QToolButton::toggled, this, [this](bool checked) { sendMidiSysExParam(64, checked ? 127 : 0); markPresetAsModified(); });
    connect(ui->btnReverbSwitch, &QToolButton::toggled, this, [this](bool checked) { sendMidiSysExParam(54, checked ? 127 : 0); markPresetAsModified(); });
    connect(ui->btnFxLoop,       &QToolButton::toggled, this, [this](bool checked) { sendMidiSysExParam(55, checked ? 127 : 0); markPresetAsModified(); });
    connect(ui->btnNoiseGate,    &QToolButton::toggled, this, [this](bool checked) { sendMidiSysExParam(63, checked ? 127 : 0); markPresetAsModified(); });
    connect(ui->btnMute,         &QToolButton::toggled, this, [this](bool checked) { sendMidiSysExParam(9,  checked ? 127 : 0); });

    // Interrupteurs Modulation / Delay
    if (auto *btnMod = ui->remotePanel->findChild<QToolButton*>("btnModSwitch")) {
        connect(btnMod, &QToolButton::toggled, this, [this](bool checked) { sendMidiSysExParam(52, checked ? 127 : 0); markPresetAsModified(); });
    }
    if (auto *btnDel = ui->remotePanel->findChild<QToolButton*>("btnDelaySwitch")) {
        connect(btnDel, &QToolButton::toggled, this, [this](bool checked) { sendMidiSysExParam(53, checked ? 127 : 0); markPresetAsModified(); });
    }

    // Groupe exclusif du Power Soak (Paramètre SysEx 30)
    m_powerSoakGroup = new QButtonGroup(this);
    m_powerSoakGroup->setExclusive(true);
    m_powerSoakGroup->addButton(ui->btnSoakNoSp, 0);
    m_powerSoakGroup->addButton(ui->btnSoak1W,   1);
    m_powerSoakGroup->addButton(ui->btnSoak5W,   2);
    m_powerSoakGroup->addButton(ui->btnSoak20W,  3);
    m_powerSoakGroup->addButton(ui->btnSoak40W,  4);
    connect(m_powerSoakGroup, &QButtonGroup::idClicked, this, [this](int id) {
        int soakValues[] = {0, 32, 64, 96, 127};
        sendMidiSysExParam(30, soakValues[std::clamp(id, 0, 4)]);
        QSettings("HughesKettner", "GM40Remote").setValue("global/lastSoakId", id);
        if (!m_globalPowerSoak) {
            markPresetAsModified();
        }
    });   

    // Création automatique de ~/.config/HughesKettner/GM40Remote.conf au premier lancement
    QSettings settings("HughesKettner", "GM40Remote");
    QString configDir = QFileInfo(settings.fileName()).absolutePath();
    QDir().mkpath(configDir);

    if (!QFile::exists(settings.fileName())) {
        // Valeurs par défaut MIDI et globales
        settings.setValue("midi/channel", 0);
        settings.setValue("global/powerSoak", false);
        settings.setValue("global/powerEq", false);
        settings.setValue("global/lastSoakId", 1);

        // Raccourcis clavier par défaut
        settings.setValue("shortcuts/boost", "Space");
        settings.setValue("shortcuts/mute", "M");
        settings.setValue("shortcuts/nextPreset", "Right");
        settings.setValue("shortcuts/prevPreset", "Left");
        settings.setValue("shortcuts/tap", "T");
        settings.setValue("shortcuts/presetList", "L");

        settings.sync(); // Force l'écriture immédiate sur le disque
    }

    // Vérifie à chaque démarrage que factory.gm40 est présent dans ~/.config/HughesKettner/
    QString diskFactoryPath = configDir + "/factory.gm40";
    if (!QFile::exists(diskFactoryPath)) {
        if (QFile::copy(":/factory.gm40", diskFactoryPath)) {
            QFile::setPermissions(diskFactoryPath,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                      QFileDevice::ReadGroup | QFileDevice::ReadOther);
        }
    }

    // Lecture des préférences sauvegardées AVANT de charger les presets
    m_midiChannel     = settings.value("midi/channel", 0).toInt();
    m_globalPowerSoak = settings.value("global/powerSoak", false).toBool();
    m_globalPowerEq   = settings.value("global/powerEq", false).toBool();

    // 1. Chargement des presets : dernier fichier ouvert > backup local > banque d'usine
    initDefaultPresets();

    QString lastFile   = settings.value("paths/lastLibraryFile").toString();
    QString backupPath = configDir + "/backup_gm40.gm40";

    if (!lastFile.isEmpty() && QFile::exists(lastFile)) {
        loadGm40File(lastFile);
    } else if (QFile::exists(backupPath)) {
        loadGm40File(backupPath);
    } else {
        loadGm40File(diskFactoryPath);
    }


    if (m_globalPowerSoak) {
        int lastSoak = settings.value("global/lastSoakId", 1).toInt(); // 1 = 1W par défaut
        if (auto *btn = m_powerSoakGroup->button(lastSoak)) btn->setChecked(true);
    }

    // 2. Édition directe du nom du preset dans la barre d'affichage
    ui->presetDisplay->setReadOnly(false);
    connect(ui->presetDisplay, &QLineEdit::editingFinished, this, [this]() {
        QString newName = ui->presetDisplay->text().trimmed();
        if (!newName.isEmpty()) {
            // Ne marque comme modifié que si le nom est différent de l'original
            if (newName != m_workingPresets[m_currentPreset].name) {
                m_workingPresets[m_currentPreset].name = newName;
                markPresetAsModified();
            }
        } else {
            updatePresetDisplay();
        }
    });

    //SECTION PRESET (HEADER)
    // Bouton "+" (Preset suivant)
    connect(ui->plusButton, &QPushButton::clicked, this, [this]() {
        if (m_currentPreset < 127) {
            switchToPreset(m_currentPreset + 1);
        }
    });
    // bouton PRESET
    connect(ui->btnPresetList, &QPushButton::clicked, this, &MainWindow::openPresetSelectorDialog);
    // Bouton "-" (Preset précédent)
    connect(ui->minusButton, &QPushButton::clicked, this, [this]() {
        if (m_currentPreset > 0) {
            switchToPreset(m_currentPreset - 1);
        }
    });




    //SECTION CURRENT PRESET
    // Bouton "Restore Orig." : annule les modifications du preset actif et recharge l'original du fichier
    connect(ui->btnRestoreOrig, &QPushButton::clicked, this, [this]() {
        if (m_currentPreset < 0 || m_currentPreset >= m_originalPresets.size()) return;
        // 1. Remplace le preset de travail par sa copie intacte d'origine
        m_workingPresets[m_currentPreset] = m_originalPresets[m_currentPreset];
        m_isComparingOriginal = false;
        ui->btnCompare->setStyleSheet("");
        // 2. Met à jour tous les potentiomètres, boutons et le nom à l'écran (et renvoie à l'ampli)
        applyPresetToUi(m_workingPresets[m_currentPreset], true);
        updatePresetDisplay();
        // 3. Remet l'indicateur visuel sur "Original"
        ui->lblCompStatus->setText("Original ●   Modified ○");
        ui->midiLog->appendHtml(QString("<font color='#45e28a'>↩️ Preset %1 (%2) restored to original values.</font>")
                                    .arg(m_currentPreset + 1)
                                    .arg(m_workingPresets[m_currentPreset].name));
    });
    // Bouton "Compare to HD" : bascule entre la version modifiée et la version d'origine du fichier
    connect(ui->btnCompare, &QPushButton::clicked, this, [this]() {
        if (m_currentPreset < 0 || m_currentPreset >= m_originalPresets.size()) return;

        if (!m_isComparingOriginal) {
            // 1. Sauvegarde nos retouches en cours avant d'écouter l'original
            m_workingPresets[m_currentPreset] = captureCurrentUiState();
            m_isComparingOriginal = true;

            // 2. Affiche et envoie la version originale à l'ampli
            applyPresetToUi(m_originalPresets[m_currentPreset], true);
            updatePresetDisplay();
            ui->lblCompStatus->setText("Original ●   Modified ○");
            ui->btnCompare->setStyleSheet("background-color: #8c6239; color: #ffff00; font-weight: bold;");
            ui->midiLog->appendHtml("<font color='#ffb52c'>⚖️ [COMPARE] Listening to original preset.</font>");
        } else {
            // 3. Revient à nos réglages modifiés
            m_isComparingOriginal = false;
            applyPresetToUi(m_workingPresets[m_currentPreset], true);
            updatePresetDisplay();
            ui->lblCompStatus->setText("Original ○   Modified ●");
            ui->btnCompare->setStyleSheet("");
            ui->midiLog->appendHtml("<font color='#45e28a'>⚖️ [COMPARE] Back to modified settings.</font>");
        }
    });




    // SECTION ACTIVE PRESET
    // --- BOUTON "Save HD" : met à jour le preset actif dans le fichier de backup ---
    connect(ui->btnSave, &QPushButton::clicked, this, [this]() {
        // 1. Capture l'état actuel de l'écran et le place dans le preset actif en mémoire
        m_workingPresets[m_currentPreset] = captureCurrentUiState();
        m_isComparingOriginal = false;
        ui->btnCompare->setStyleSheet("");
        // 2. Récupère le chemin exact du fichier de backup (~/.config/HughesKettner/backup_gm40.gm40)
        QSettings settings("HughesKettner", "GM40Remote");
        QString configDir  = QFileInfo(settings.fileName()).absolutePath();
        QDir().mkpath(configDir);
        QString backupPath = configDir + "/backup_gm40.gm40";
        // 3. Écrit toute la liste (avec ce preset mis à jour) dans le fichier de backup
        saveGm40File(backupPath);
    });
    // --- BOUTON "Save to Amp" : écrit le preset actif dans la mémoire interne de l'ampli ---
    connect(ui->btnSendAmp, &QPushButton::clicked, this, &MainWindow::sendCurrentPresetToAmp);
    // --- BOUTON "Save to HD number" : enregistre dans le backup et propose l'écriture dans l'ampli ---
    connect(ui->btnSaveAsHD, &QPushButton::clicked, this, [this]() {
        PresetData currentPresetData = captureCurrentUiState();

        QStringList names;
        names.reserve(m_workingPresets.size());
        for (const auto &p : m_workingPresets) {
            names.append(p.name);
        }

        PresetSelectorDialog dlg(names, m_currentPreset, this);
        dlg.setWindowTitle(QString("Save '%1' to which slot ?").arg(currentPresetData.name));

        if (dlg.exec() == QDialog::Accepted) {
            int destIdx = dlg.selectedPresetIndex();
            if (destIdx >= 0 && destIdx < 128) {
                currentPresetData.number = destIdx + 1;
                m_workingPresets[destIdx] = currentPresetData;

                m_currentPreset = destIdx;
                m_isComparingOriginal = false;
                ui->btnCompare->setStyleSheet("");
                // 1. Sauvegarde dans ~/.config/HughesKettner/backup_gm40.gm40
                QSettings settings("HughesKettner", "GM40Remote");
                QString configDir  = QFileInfo(settings.fileName()).absolutePath();
                QDir().mkpath(configDir);
                saveGm40File(configDir + "/backup_gm40.gm40");

                updatePresetDisplay();
                applyPresetToUi(m_workingPresets[m_currentPreset], false);

                ui->midiLog->appendHtml(QString("<font color='#45e28a'>💾 Preset '%1' saved to slot %2 on disk.</font>")
                                            .arg(currentPresetData.name)
                                            .arg(destIdx + 1));
                // 2. Demande si on copie également dans la mémoire interne de l'ampli
                auto reply = QMessageBox::question(this,
                                                   "Send to Amplifier",
                                                   QString("Do you also want to copy preset '%1' into internal amp memory slot #%2 ?")
                                                       .arg(currentPresetData.name)
                                                       .arg(destIdx + 1),
                                                   QMessageBox::Yes | QMessageBox::No);

                if (reply == QMessageBox::Yes) {
                    if (!seq) {
                        QMessageBox::warning(this, "MIDI Disconnected", "The amplifier is not connected via MIDI.");
                    } else {
                        sendPresetToAmp(destIdx, currentPresetData);
                        sendMidiProgramChange(destIdx);
                        ui->midiLog->appendHtml(QString("<font color='#ff4d4d'>📤 Preset %1 written to internal amp memory.</font>")
                                                    .arg(destIdx + 1));
                    }
                } else {
                    sendMidiProgramChange(m_currentPreset);
                }
            }
        }
    });
    // --- BOUTON "Save to Amp number" : écrit dans l'ampli et propose l'enregistrement dans le backup ---
    connect(ui->btnSaveAsAmp, &QPushButton::clicked, this, [this]() {
        if (!seq) {
            QMessageBox::warning(this, "MIDI Disconnected", "Please connect to the amplifier first.");
            return;
        }

        PresetData currentPresetData = captureCurrentUiState();

        QStringList names;
        names.reserve(m_workingPresets.size());
        for (const auto &p : m_workingPresets) {
            names.append(p.name);
        }

        PresetSelectorDialog dlg(names, m_currentPreset, this);
        dlg.setWindowTitle(QString("Write '%1' to which amp slot ?").arg(currentPresetData.name));

        if (dlg.exec() == QDialog::Accepted) {
            int destIdx = dlg.selectedPresetIndex();
            if (destIdx >= 0 && destIdx < 128) {
                currentPresetData.number = destIdx + 1;
                m_workingPresets[destIdx] = currentPresetData;
                m_currentPreset = destIdx;
                m_isComparingOriginal = false;
                ui->btnCompare->setStyleSheet("");
                // 1. Envoie la trame SysEx dans la mémoire interne de l'ampli
                sendPresetToAmp(destIdx, currentPresetData);
                sendMidiProgramChange(destIdx);

                updatePresetDisplay();
                applyPresetToUi(m_workingPresets[m_currentPreset], false);
                ui->lblCompStatus->setText("Original ○   Modified ●");

                ui->midiLog->appendHtml(QString("<font color='#ff4d4d'>💾 [SENT] Preset '%1' written to amp slot #%2.</font>")
                                            .arg(currentPresetData.name)
                                            .arg(destIdx + 1));
                // 2. Demande si on sauvegarde également au même numéro dans backup_gm40.gm40
                auto reply = QMessageBox::question(this,
                                                   "Save to Backup",
                                                   QString("Do you also want to save preset '%1' to slot #%2 in your backup file ?")
                                                       .arg(currentPresetData.name)
                                                       .arg(destIdx + 1),
                                                   QMessageBox::Yes | QMessageBox::No);

                if (reply == QMessageBox::Yes) {
                    QSettings settings("HughesKettner", "GM40Remote");
                    QString configDir  = QFileInfo(settings.fileName()).absolutePath();
                    QDir().mkpath(configDir);
                    saveGm40File(configDir + "/backup_gm40.gm40");
                }
            }
        }
    });



    //SECTION LIBRARY
    // Bouton "LOAD library file"
    connect(ui->btnLoad, &QPushButton::clicked, this, [this]() {
        QSettings settings("HughesKettner", "GM40Remote");
        QString configDir = QFileInfo(settings.fileName()).absolutePath();
        QString file = QFileDialog::getOpenFileName(this,
                                                    "Load GM40 file",
                                                    configDir,
                                                    "GM40 File (*.gm40)");
        if (!file.isEmpty()) {
            loadGm40File(file);
        }
    });
    // --- BOUTON "Factory Reset" : réinitialise la bibliothèque active sur le disque dur ---
    connect(ui->btnFactoryReset, &QPushButton::clicked, this, [this]() {
        // 1. Détermine le fichier cible sur le disque dur (fichier actif ou backup par défaut)
        QSettings settings("HughesKettner", "GM40Remote");
        QString configDir  = QFileInfo(settings.fileName()).absolutePath();
        QDir().mkpath(configDir);
        QString defaultBackup = configDir + "/backup_gm40.gm40";

        QString targetDiskFile = (!m_currentLibraryPath.isEmpty() && !m_currentLibraryPath.startsWith(":/"))
                                     ? m_currentLibraryPath
                                     : defaultBackup;

        auto reply = QMessageBox::question(this,
                                           "Factory Reset",
                                           QString("Warning: this action will overwrite all 128 presets in:\n%1\n\n"
                                                   "with the factory settings.\n\nDo you want to continue ?")
                                               .arg(targetDiskFile),
                                           QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return;

        // 2. Charge les 128 presets d'usine en mémoire vive
        loadGm40File(":/factory.gm40");
        ui->btnCompare->setStyleSheet("");

        // 3. Écrase immédiatement le fichier actif sur le disque dur avec ces données d'usine
        saveGm40File(targetDiskFile);

        ui->midiLog->appendHtml(QString("<font color='#45e28a'>🏭 Factory Reset complete: '%1' reset to factory presets.</font>")
                                    .arg(targetDiskFile));

        // 4. Propose de réécrire également les 128 presets dans l'amplificateur s'il est connecté
        if (seq) {
            auto ampReply = QMessageBox::question(this,
                                                  "Send to Amplifier",
                                                  "Do you also want to send these 128 factory presets to the amplifier's internal memory ?",
                                                  QMessageBox::Yes | QMessageBox::No);
            if (ampReply == QMessageBox::Yes) {
                startFullRestore();
            }
        }
    });
    // Bouton "Library editor"
    connect(ui->btnLibEditor, &QPushButton::clicked, this, [this]() {
        // Capture les éventuelles modifications en cours avant d'ouvrir l'éditeur
        if (!m_isComparingOriginal) {
            m_workingPresets[m_currentPreset] = captureCurrentUiState();
        }

        LibraryEditorDialog dlg(m_workingPresets, m_currentLibraryPath, this);

        // Permet de pré-écouter en direct n'importe quel preset sélectionné dans l'éditeur
        connect(&dlg, &LibraryEditorDialog::auditionPresetRequested, this, [this](const PresetData &p) {
            applyPresetToUi(p, true);
            ui->midiLog->appendHtml(QString("<font color='#ffb52c'>🔊 [AUDITION] Previewing '%1' on amplifier.</font>")
                                        .arg(p.name));
        });

        if (dlg.exec() == QDialog::Accepted) {
            m_workingPresets = dlg.modifiedPresets();
            m_currentLibraryPath = dlg.currentLibraryPath();
            m_isComparingOriginal = false;
            ui->btnCompare->setStyleSheet("");

            // Sauvegarde automatiquement dans le fichier actif sur le disque dur
            if (!m_currentLibraryPath.isEmpty() && !m_currentLibraryPath.startsWith(":/")) {
                saveGm40File(m_currentLibraryPath);
            }

            updatePresetDisplay();
            applyPresetToUi(m_workingPresets[m_currentPreset], false);
            ui->midiLog->appendHtml("<font color='#45e28a'>📚 Library Editor changes applied.</font>");
        } else {
            // Si l'utilisateur clique sur Cancel, on rétablit l'affichage du preset actif
            applyPresetToUi(m_workingPresets[m_currentPreset], false);
            updatePresetDisplay();
        }
    });

    // Bouton "Backup Orig. as HD" : sauvegarde les presets de l'ampli
    connect(ui->btnBackup, &QPushButton::clicked, this, [this]() {
        startFullBackup(true);
    });




    // Bouton Tap Tempo et du BPM SpinBox (44 à 1176 BPM)
    QPushButton *tapBtn = ui->remotePanel->findChild<QPushButton*>("btnTap");
    QSpinBox *bpmSpin = ui->remotePanel->findChild<QSpinBox*>("bpmDisplay");

    if (tapBtn) {
        connect(tapBtn, &QPushButton::clicked, this, [this, bpmSpin]() {
            if (!tapTimer.isValid()) {
                tapTimer.start();
                return;
            }

            qint64 interval = tapTimer.restart();
            if (interval >= 51 && interval <= 1360) {
                int graphVal = delayMsToGraph(static_cast<int>(interval));
                int realMs = graphToDelayMs(graphVal);
                int bpm = std::clamp(static_cast<int>(std::round(60000.0 / realMs)), 44, 1176);

                sendMidiSysExParam(4, graphVal); markPresetAsModified();

                if (bpmSpin) {
                    bpmSpin->blockSignals(true);
                    bpmSpin->setValue(bpm);
                    bpmSpin->blockSignals(false);
                }

                ui->knobDelTime->blockSignals(true);
                ui->knobDelTime->setValue(graphVal);
                if (ui->valDelTime) {
                    ui->valDelTime->setTextFormat(Qt::RichText);
                    ui->valDelTime->setText(QString("%1<br>%2 ms").arg(graphVal).arg(realMs));
                }
                ui->knobDelTime->blockSignals(false);
            }
        });
    }
    if (bpmSpin) {
        connect(bpmSpin, &QSpinBox::valueChanged, this, [this](int bpm) {
            if (bpm <= 0) return;

            int targetMs = static_cast<int>(std::round(60000.0 / bpm));
            int graphVal = delayMsToGraph(targetMs);
            int realMs = graphToDelayMs(graphVal);

            sendMidiSysExParam(4, graphVal); markPresetAsModified();

            ui->knobDelTime->blockSignals(true);
            ui->knobDelTime->setValue(graphVal);
            if (ui->valDelTime) {
                ui->valDelTime->setTextFormat(Qt::RichText);
                ui->valDelTime->setText(QString("%1<br>%2 ms").arg(graphVal).arg(realMs));
            }
            ui->knobDelTime->blockSignals(false);
        });
    }




    // Initialisation des potentiomètres
    setupKnobLabels();
    if (!m_workingPresets.isEmpty()) {
        applyPresetToUi(m_workingPresets[0], false);
    }


    // Connexion de la barre système MIDI
    connect(ui->refreshButton,    &QPushButton::clicked, this, [this] { refreshPorts(); });
    connect(ui->connectButton,    &QPushButton::clicked, this, [this] { connectMidi(); });
    connect(ui->disconnectButton, &QPushButton::clicked, this, [this] { disconnectMidi(); });

    ui->disconnectButton->setEnabled(false);
    connect(&midiTimer, &QTimer::timeout, this, [this] { readMidi(); });

    // Initialisation de l'affichage du Delay Time au démarrage
    if (ui->valDelTime && ui->knobDelTime) {
        ui->valDelTime->setTextFormat(Qt::RichText);
        int initialGraphVal = ui->knobDelTime->value();
        int initialMs = graphToDelayMs(initialGraphVal);
        ui->valDelTime->setText(QString("%1<br>%2 ms").arg(initialGraphVal).arg(initialMs));
    }

    m_currentPreset = 0;
    updatePresetDisplay();
    refreshPorts();
    setupShortcuts();
}






MainWindow::~MainWindow()
{
    disconnectMidi();
    delete ui;
}




void MainWindow::setupKnobLabels()
{
    if (ui->valDelTime) ui->valDelTime->setTextFormat(Qt::RichText);

    ui->knobRevLevel->onValueChanged     = [this](int v){ ui->valRevLevel->setText(QString::number(v));     sendMidiSysExParam(29, v); markPresetAsModified(); };
    ui->knobDelLevel->onValueChanged     = [this](int v){ ui->valDelLevel->setText(QString::number(v));     sendMidiSysExParam(28, v); markPresetAsModified(); };
    ui->knobDelFeedback->onValueChanged  = [this](int v){ ui->valDelFeedback->setText(QString::number(v));  sendMidiSysExParam(27, v); markPresetAsModified(); };

    ui->knobModRate->onValueChanged = [this](int v){
        int rate = std::clamp(v / 4, 0, 63); // Convertit 0..255 en 0..63 sur tout le tour
        ui->valModRate->setText(QString::number(rate));
        int type = std::clamp(ui->modSelector->value(), 0, 3);
        int combined = (type * 64) + rate;
        if (type == 3 && rate == 63) combined = 255;
        sendMidiSysExParam(12, combined);
        markPresetAsModified();
    };

    ui->knobModIntensity->onValueChanged = [this](int v){ ui->valModIntensity->setText(QString::number(v)); sendMidiSysExParam(1, v); markPresetAsModified(); };

    ui->knobDelTime->onValueChanged = [this](int v){
        int delayMs = graphToDelayMs(v);
        ui->valDelTime->setText(QString("%1<br>%2 ms").arg(v).arg(delayMs));
        sendMidiSysExParam(4, v);
        markPresetAsModified();

        QSpinBox *bpmSpin = ui->remotePanel->findChild<QSpinBox*>("bpmDisplay");
        if (bpmSpin && delayMs > 0) {
            bpmSpin->blockSignals(true);
            bpmSpin->setValue(std::clamp(static_cast<int>(std::round(60000.0 / delayMs)), 44, 1176));
            bpmSpin->blockSignals(false);
        }
    };

    auto sendModulationSysEx = [this]() {
        int type = std::clamp(ui->modSelector->value(), 0, 3);
        int rate = std::clamp(ui->knobModRate->value() / 4, 0, 63);
        int combined = (type * 64) + rate;
        if (type == 3 && rate == 63) combined = 255;
        sendMidiSysExParam(12, combined);
        markPresetAsModified();
    };

    ui->modSelector->onSelectorChanged = [sendModulationSysEx](int) {
        sendModulationSysEx();
    };

    ui->knobGain->onValueChanged      = [this](int v){ ui->valGain->setText(QString::number(v));      sendMidiSysExParam(56, v); markPresetAsModified(); };
    ui->knobBass->onValueChanged      = [this](int v){ ui->valBass->setText(QString::number(v));      sendMidiSysExParam(21, v); markPresetAsModified(); };
    ui->knobMid->onValueChanged       = [this](int v){ ui->valMid->setText(QString::number(v));       sendMidiSysExParam(22, v); markPresetAsModified(); };
    ui->knobTreble->onValueChanged    = [this](int v){ ui->valTreble->setText(QString::number(v));    sendMidiSysExParam(23, v); markPresetAsModified(); };
    ui->knobResonance->onValueChanged = [this](int v){ ui->valResonance->setText(QString::number(v)); sendMidiSysExParam(24, v); if (!m_globalPowerEq) markPresetAsModified(); };
    ui->knobPresence->onValueChanged  = [this](int v){ ui->valPresence->setText(QString::number(v));  sendMidiSysExParam(25, v); if (!m_globalPowerEq) markPresetAsModified(); };
    ui->knobVolume->onValueChanged    = [this](int v){ ui->valVolume->setText(QString::number(v));    sendMidiSysExParam(57, v); markPresetAsModified(); };
}







void MainWindow::createMenus()
{
    // Menu File -> Quit (créé dans mainwindow.ui)
    if (ui->actionQuit) {
        connect(ui->actionQuit, &QAction::triggered, this, &QWidget::close);
    }

    // Menu Settings -> MIDI Channel... (créé dans mainwindow.ui)
    if (ui->actionMIDI_Channel) {
        connect(ui->actionMIDI_Channel, &QAction::triggered, this, &MainWindow::openSettingsDialog);
    }

    // Menu Help -> About (créé dans mainwindow.ui)
    // Menu Help -> About
    if (ui->actionAbout) {
        connect(ui->actionAbout, &QAction::triggered, this, [this]() {
            QMessageBox::about(this, "About GM40 Remote",
                               QString("<h3>Hughes &amp; Kettner GM40 Remote — Linux v%1</h3>"
                                       "<p>ALSA MIDI &amp; SysEx control interface for GrandMeister Deluxe 40.</p>"
                                       "<p><b>Author:</b> altern67<br>"
                                       "<b>Contact:</b> <a href='mailto:altern67@yahoo.com'>altern67@yahoo.com</a><br>"
                                       "<b>GitHub:</b> <a href='https://github.com'>://github.com</a><br>"
                                       "<b>License:</b> GNU GPL v3</p>")
                                   .arg(qApp->applicationVersion()));
        });
    }

    if (ui->actionShortcuts) {
        connect(ui->actionShortcuts, &QAction::triggered, this, &MainWindow::openShortcutsDialog);
    }
}




void MainWindow::openSettingsDialog()
{
    SettingsDialog dlg(m_midiChannel, m_globalPowerSoak, m_globalPowerEq, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_midiChannel     = dlg.selectedChannel();
        m_globalPowerSoak = dlg.isGlobalSoak();
        m_globalPowerEq   = dlg.isGlobalPowerEq();

        // Sauvegarde dans ~/.config/HughesKettner/GM40Remote.conf
        QSettings settings("HughesKettner", "GM40Remote");
        settings.setValue("midi/channel", m_midiChannel);
        settings.setValue("global/powerSoak", m_globalPowerSoak);
        settings.setValue("global/powerEq", m_globalPowerEq);

        sendSystemConfig();

        QString chText = (m_midiChannel == -1) ? "OMNI" : QString::number(m_midiChannel + 1);
        ui->midiLog->appendHtml(QString("<font color='#45e28a'>⚙️ Settings saved (MIDI: %1 | Global Soak: %2 | Global EQ: %3)</font>")
                                    .arg(chText)
                                    .arg(m_globalPowerSoak ? "ON" : "OFF")
                                    .arg(m_globalPowerEq ? "ON" : "OFF"));
    }
}






void MainWindow::refreshPorts()
{
    ui->inputCombo->clear();
    ui->outputCombo->clear();

    snd_seq_t *scanner = nullptr;
    if (snd_seq_open(&scanner, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
        ui->midiLog->appendHtml("<font color='#ff4d4d'>❌ Error: unable to open ALSA.</font>");
        return;
    }

    snd_seq_client_info_t *clientInfo;
    snd_seq_port_info_t *portInfo;
    snd_seq_client_info_alloca(&clientInfo);
    snd_seq_port_info_alloca(&portInfo);
    snd_seq_client_info_set_client(clientInfo, -1);

    while (snd_seq_query_next_client(scanner, clientInfo) >= 0) {
        int client = snd_seq_client_info_get_client(clientInfo);
        snd_seq_port_info_set_client(portInfo, client);
        snd_seq_port_info_set_port(portInfo, -1);

        while (snd_seq_query_next_port(scanner, portInfo) >= 0) {
            int port = snd_seq_port_info_get_port(portInfo);
            unsigned int caps = snd_seq_port_info_get_capability(portInfo);
            QString name = QString("%1:%2 — %3").arg(client).arg(port).arg(snd_seq_port_info_get_name(portInfo));

            if ((caps & SND_SEQ_PORT_CAP_READ) && !(caps & SND_SEQ_PORT_CAP_SUBS_READ && client == snd_seq_client_id(scanner))) {
                ui->inputCombo->addItem(name, QString("%1:%2").arg(client).arg(port));
            }
            if ((caps & SND_SEQ_PORT_CAP_WRITE) && !(caps & SND_SEQ_PORT_CAP_SUBS_WRITE && client == snd_seq_client_id(scanner))) {
                ui->outputCombo->addItem(name, QString("%1:%2").arg(client).arg(port));
            }
        }
    }
    snd_seq_close(scanner);

    // Restauration des derniers ports MIDI utilisés
    QSettings settings("HughesKettner", "GM40Remote");
    QString savedIn  = settings.value("midi/inputPortName").toString();
    QString savedOut = settings.value("midi/outputPortName").toString();

    auto restoreCombo = [](QComboBox *combo, const QString &savedText) {
        if (savedText.isEmpty()) return;
        // 1. Recherche exacte (ex. "20:0 — UM-ONE MIDI 1")
        int idx = combo->findText(savedText);
        // 2. Si le numéro ALSA a changé (ex. 24:0 au lieu de 20:0), recherche par le nom après "—"
        if (idx < 0 && savedText.contains("—")) {
            QString deviceName = savedText.section("—", 1).trimmed();
            for (int i = 0; i < combo->count(); ++i) {
                if (combo->itemText(i).contains(deviceName)) {
                    idx = i;
                    break;
                }
            }
        }
        if (idx >= 0) combo->setCurrentIndex(idx);
    };

    restoreCombo(ui->inputCombo, savedIn);
    restoreCombo(ui->outputCombo, savedOut);

    ui->midiLog->appendHtml("<font color='#45e28a'>🔄 Physical MIDI ports refreshed.</font>");
}






bool MainWindow::parsePort(const QString &value, int &client, int &port)
{
    if (value.isEmpty()) return false;
    const QStringList parts = value.split(':');
    if (parts.size() != 2) return false;
    bool ok1 = false, ok2 = false;
    client = parts.at(0).toInt(&ok1);
    port = parts.at(1).toInt(&ok2);
    return ok1 && ok2;
}






void MainWindow::connectMidi()
{
    if (seq) return;

    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) {
        QMessageBox::critical(this, "ALSA", "Unable to open ALSA MIDI subsystem.");
        seq = nullptr;
        return;
    }

    snd_seq_set_client_name(seq, "GM40 Remote");

    localPort = snd_seq_create_simple_port(seq, "GM40 MIDI",
                                           SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE |
                                               SND_SEQ_PORT_CAP_READ  | SND_SEQ_PORT_CAP_SUBS_READ,
                                           SND_SEQ_PORT_TYPE_APPLICATION);

    if (localPort < 0) {
        snd_seq_close(seq);
        seq = nullptr;
        QMessageBox::critical(this, "ALSA", "Unable to create virtual MIDI port.");
        return;
    }

    QString inData = ui->inputCombo->currentData().toString();
    QString outData = ui->outputCombo->currentData().toString();

    int inputClient, inputPort, outputClient, outputPort;
    if (!parsePort(inData, inputClient, inputPort) || !parsePort(outData, outputClient, outputPort)) {
        disconnectMidi();
        QMessageBox::warning(this, "MIDI", "Please select a valid physical interface port.");
        return;
    }

    int resultIn = snd_seq_connect_from(seq, localPort, inputClient, inputPort);
    int resultOut = snd_seq_connect_to(seq, localPort, outputClient, outputPort);

    if (resultIn < 0 || resultOut < 0) {
        ui->midiLog->appendHtml("<font color='#ff4d4d'>❌ Hardware routing error: please check cables.</font>");
        disconnectMidi();
        return;
    }

    // Sauvegarde des ports MIDI sélectionnés dans GM40Remote.conf
    QSettings settings("HughesKettner", "GM40Remote");
    settings.setValue("midi/inputPortName", ui->inputCombo->currentText());
    settings.setValue("midi/outputPortName", ui->outputCombo->currentText());

    ui->statusLabel->setText("Connected");
    ui->statusLabel->setStyleSheet("color: #45e28a; font-weight: bold;");
    ui->disconnectButton->setEnabled(true);
    ui->midiLog->appendHtml("<font color='#45e28a'>🟢 MIDI connection established with hardware interface.</font>");

    m_initialSyncDone = false;
    midiTimer.start(10);

    // Demande automatique de l'état de l'ampli 100 ms après la connexion
    QTimer::singleShot(100, this, [this]() {
        requestAmpState();

        // Vérifie si le fichier de sauvegarde existe dans ~/.config/HughesKettner/
        QSettings settings("HughesKettner", "GM40Remote");
        QString configDir  = QFileInfo(settings.fileName()).absolutePath();
        QString backupPath = configDir + "/backup_gm40.gm40";

        if (!QFile::exists(backupPath)) {
            ui->midiLog->appendHtml("<font color='#ffb52c'>ℹ️ No backup file found: starting automatic backup...</font>");
            // Attend 400 ms que la synchro initiale (Config + Edit Buffer) se termine avant de lire les 128 presets
            QTimer::singleShot(400, this, [this]() {
                startFullBackup(false);
            });
        }
    });
}




void MainWindow::disconnectMidi()
{
    midiTimer.stop();
    if (seq) {
        snd_seq_close(seq);
        seq = nullptr;
    }
    localPort = -1;
    if (ui->statusLabel) {
        ui->statusLabel->setText("Disconnected");
        ui->statusLabel->setStyleSheet("color: #ffb52c; font-weight: bold;");
    }
    if (ui->disconnectButton) ui->disconnectButton->setEnabled(false);
    if (ui->midiLog) ui->midiLog->appendHtml("<font color='#ffb52c'>🟡 MIDI disconnected.</font>");
}






void MainWindow::readMidi()
{
    if (!seq) return;

    snd_seq_event_t *event = nullptr;
    bool presetChanged = false;
    bool receivedFullDump = false;

    while (snd_seq_event_input(seq, &event) >= 0) {
        if (!event) break;

        // 1. Changement de Preset (Program Change) filtré sur m_midiChannel
        if (event->type == SND_SEQ_EVENT_PGMCHANGE) {
            if (m_midiChannel != -1 && event->data.control.channel != m_midiChannel) {
                snd_seq_free_event(event);
                continue;
            }

            int pgm = std::clamp(static_cast<int>(event->data.control.value), 0, 127);

            if (pgm != m_currentPreset) {
                if (!m_isComparingOriginal) {
                    m_workingPresets[m_currentPreset] = captureCurrentUiState();
                }
                m_isComparingOriginal = false;
                m_currentPreset = pgm;
                presetChanged = true;
            }

            QString presetName = (pgm < m_workingPresets.size())
                                     ? m_workingPresets[pgm].name
                                     : QString("Preset %1").arg(pgm + 1);

            ui->midiLog->appendHtml(QString("<font color='#45e28a'>📥 [RX Ch.%1] Program Change -> %2 (%3)</font>")
                                        .arg(m_midiChannel + 1)
                                        .arg(pgm + 1)
                                        .arg(presetName));
        }
        // 2. Décodage du protocole SysEx GrandMeister 40 (Trame Courte et Longue)
        else if (event->type == SND_SEQ_EVENT_SYSEX || (event->flags & SND_SEQ_EVENT_LENGTH_MASK) == SND_SEQ_EVENT_LENGTH_VARIABLE) {
            const auto *data = static_cast<const unsigned char *>(event->data.ext.ptr);
            unsigned int len = event->data.ext.len;

            if (data && len >= 14 && data[0] == 0xF0 && data[1] == 0x00 && data[2] == 0x20 && data[3] == 0x44) {

                // Neutralisation temporaire des callbacks pour éviter le renvoi en boucle vers l'ampli
                auto backupGain         = ui->knobGain->onValueChanged;         ui->knobGain->onValueChanged = nullptr;
                auto backupVolume       = ui->knobVolume->onValueChanged;       ui->knobVolume->onValueChanged = nullptr;
                auto backupBass         = ui->knobBass->onValueChanged;         ui->knobBass->onValueChanged = nullptr;
                auto backupMid          = ui->knobMid->onValueChanged;          ui->knobMid->onValueChanged = nullptr;
                auto backupTreble       = ui->knobTreble->onValueChanged;       ui->knobTreble->onValueChanged = nullptr;
                auto backupPresence     = ui->knobPresence->onValueChanged;     ui->knobPresence->onValueChanged = nullptr;
                auto backupResonance    = ui->knobResonance->onValueChanged;    ui->knobResonance->onValueChanged = nullptr;
                auto backupRevLevel     = ui->knobRevLevel->onValueChanged;     ui->knobRevLevel->onValueChanged = nullptr;
                auto backupDelLevel     = ui->knobDelLevel->onValueChanged;     ui->knobDelLevel->onValueChanged = nullptr;
                auto backupDelFeedback  = ui->knobDelFeedback->onValueChanged;  ui->knobDelFeedback->onValueChanged = nullptr;
                auto backupDelTime      = ui->knobDelTime->onValueChanged;      ui->knobDelTime->onValueChanged = nullptr;
                auto backupModRate      = ui->knobModRate->onValueChanged;      ui->knobModRate->onValueChanged = nullptr;
                auto backupModIntensity = ui->knobModIntensity->onValueChanged; ui->knobModIntensity->onValueChanged = nullptr;

                ui->btnBoost->blockSignals(true);
                ui->btnReverbSwitch->blockSignals(true);
                ui->btnFxLoop->blockSignals(true);
                ui->btnNoiseGate->blockSignals(true);
                ui->btnMute->blockSignals(true);
                ui->channelSelector->blockSignals(true);
                ui->modSelector->blockSignals(true);

                QToolButton *btnMod = ui->remotePanel->findChild<QToolButton*>("btnModSwitch");
                QToolButton *btnDel = ui->remotePanel->findChild<QToolButton*>("btnDelaySwitch");
                if (btnMod) btnMod->blockSignals(true);
                if (btnDel) btnDel->blockSignals(true);

                // --- TRAME COURTE (15 octets) : Modification d'un paramètre unique ---
                if (len == 15) {
                    int paramID = *(data + 10);
                    int graphVal = (*(data + 11) * 128) + *(data + 12);
                    bool isButtonOn = (graphVal != 0);

                    // Affichage simplifié dans le journal MIDI
                    ui->midiLog->appendHtml(QString("<font color='#45e28a'>📥 [RX] %1</font>")
                                                .arg(formatSysExLog(paramID, graphVal)));

                    switch (paramID) {
                    case 56: ui->knobGain->setValue(graphVal);         ui->valGain->setText(QString::number(graphVal)); break;
                    case 57: ui->knobVolume->setValue(graphVal);       ui->valVolume->setText(QString::number(graphVal)); break;
                    case 21: ui->knobBass->setValue(graphVal);         ui->valBass->setText(QString::number(graphVal)); break;
                    case 22: ui->knobMid->setValue(graphVal);          ui->valMid->setText(QString::number(graphVal)); break;
                    case 23: ui->knobTreble->setValue(graphVal);       ui->valTreble->setText(QString::number(graphVal)); break;
                    case 25: ui->knobPresence->setValue(graphVal);     ui->valPresence->setText(QString::number(graphVal)); break;
                    case 24: ui->knobResonance->setValue(graphVal);    ui->valResonance->setText(QString::number(graphVal)); break;
                    case 29: ui->knobRevLevel->setValue(graphVal);     ui->valRevLevel->setText(QString::number(graphVal)); break;
                    case 28: ui->knobDelLevel->setValue(graphVal);     ui->valDelLevel->setText(QString::number(graphVal)); break;
                    case 27: ui->knobDelFeedback->setValue(graphVal);  ui->valDelFeedback->setText(QString::number(graphVal)); break;
                    case 1:  ui->knobModIntensity->setValue(graphVal); ui->valModIntensity->setText(QString::number(graphVal)); break;

                    case 12: {
                        int modType = std::clamp(graphVal / 64, 0, 3);
                        int modRate = (graphVal == 255) ? 63 : (graphVal % 64);
                        ui->modSelector->setValue(modType);
                        ui->knobModRate->setValue(modRate * 4);
                        ui->valModRate->setText(QString::number(modRate));
                        break;
                    }

                    case 30: {
                        int soakId = std::clamp(static_cast<int>(std::round(graphVal / 31.0)), 0, 4);
                        if (QAbstractButton *targetBtn = m_powerSoakGroup->button(soakId)) {
                            m_powerSoakGroup->blockSignals(true);
                            targetBtn->setChecked(true);
                            m_powerSoakGroup->blockSignals(false);
                        }
                        break;
                    }

                    case 31: ui->channelSelector->setValue(std::clamp(static_cast<int>(std::round(graphVal / 42.0)), 0, 3)); break;

                    case 4: {
                        int delayMs = graphToDelayMs(graphVal);
                        ui->knobDelTime->setValue(graphVal);
                        if (ui->valDelTime) {
                            ui->valDelTime->setTextFormat(Qt::RichText);
                            ui->valDelTime->setText(QString("%1<br>%2 ms").arg(graphVal).arg(delayMs));
                        }
                        QSpinBox *bpmSpin = ui->remotePanel->findChild<QSpinBox*>("bpmDisplay");
                        if (bpmSpin && delayMs > 0) {
                            bpmSpin->blockSignals(true);
                            bpmSpin->setValue(std::clamp(static_cast<int>(std::round(60000.0 / delayMs)), 44, 1176));
                            bpmSpin->blockSignals(false);
                        }
                        break;
                    }
                    case 63: ui->btnNoiseGate->setChecked(isButtonOn); break;
                    case 64: ui->btnBoost->setChecked(isButtonOn); break;
                    case 54: ui->btnReverbSwitch->setChecked(isButtonOn); break;
                    case 55: ui->btnFxLoop->setChecked(isButtonOn); break;
                    case 9:  ui->btnMute->setChecked(isButtonOn); break;
                    case 52: if (btnMod) btnMod->setChecked(isButtonOn); break;
                    case 53: if (btnDel) btnDel->setChecked(isButtonOn); break;
                    default: break;
                    }
                }

                // --- TRAME CONFIG SYSTÈME (commandType == 0x50) ---
                else if (len >= 15 && *(data + 9) == 0x50) {
                    ui->midiLog->appendHtml("<font color='#45e28a'>📥 [RX] Amplifier system configuration synchronized.</font>");

                    int globalFlags = *(data + 10);
                    // Bit à 0 = Mode Global activé
                    m_globalPowerSoak = (globalFlags & 0x01) == 0;
                    m_globalPowerEq   = (globalFlags & 0x02) == 0;

                    m_lastMutesByte = *(data + 11);
                    bool globalMute = (m_lastMutesByte & 0x01) != 0; // Bit 0 : Global Mute
                    bool delayMute  = (m_lastMutesByte & 0x02) != 0; // Bit 1 : Mute Delay
                    bool reverbMute = (m_lastMutesByte & 0x04) != 0; // Bit 2 : Mute Reverb
                    bool fxMute     = (m_lastMutesByte & 0x08) != 0; // Bit 3 : Mute Modulation / FX

                    ui->btnMute->setChecked(globalMute);
                    ui->btnReverbSwitch->setChecked(!reverbMute);
                    if (btnMod) btnMod->setChecked(!fxMute);
                    if (btnDel) btnDel->setChecked(!delayMute);
                }




                // --- TRAME LONGUE (47 octets) : Dump de Preset complet ---
                else if (len == 47) {
                    auto read14Bit = [&](int msbIdx) -> int {
                        return std::clamp((*(data + msbIdx) * 128) + *(data + msbIdx + 1), 0, 255);
                    };

                    // MODE BACKUP COMPLET : enregistrement silencieux dans m_workingPresets[0..127]
                    if (m_isBackingUp) {
                        m_backupReceivedCount++;
                        int byte11 = static_cast<int>(*(data + 11));
                        int byte12 = static_cast<int>(*(data + 12));
                        int slotIdx = std::clamp((byte11 > 0) ? byte11 : byte12, 0, 127);

                        PresetData &p = m_workingPresets[slotIdx];
                        p.number        = slotIdx + 1;
                        p.gain          = read14Bit(13);
                        p.bass          = read14Bit(15);
                        p.mid           = read14Bit(17);
                        p.volume        = read14Bit(19);
                        p.treble        = read14Bit(21);
                        p.resonance     = read14Bit(23);
                        p.presence      = read14Bit(25);
                        p.reverb        = read14Bit(27);
                        p.delayLevel    = read14Bit(29);
                        p.delayTime     = read14Bit(31);
                        p.delayFeedback = read14Bit(33);
                        p.modIntensity  = read14Bit(35);
                        p.modType       = read14Bit(37);

                        int qqByte      = *(data + 43);
                        int rrByte      = *(data + 44);
                        p.noiseGate     = ((qqByte % 2) != 0) ? 127 : 0;
                        p.channelType   = std::clamp(rrByte % 4, 0, 3) * 42;
                        p.channelBoost  = (((rrByte / 4) % 2) != 0) ? 127 : 0;
                        p.fxLoop        = (((rrByte / 8) % 2) != 0) ? 127 : 0;

                        int soakValues[] = {0, 32, 64, 96, 124};
                        int soakId      = std::clamp((rrByte / 16) % 8, 0, 4);
                        p.powerSoak     = soakValues[soakId];
                    }
                    // MODE NORMAL : synchronisation du preset actif à l'écran
                    else {
                        bool isFirstSync = !m_initialSyncDone;
                        bool isEditBuffer = (*(data + 10) == 0x7F && *(data + 11) == 0x7F);
                        if (!isEditBuffer || isFirstSync) {
                            m_currentPreset = std::clamp(static_cast<int>(*(data + 12)), 0, 127);
                            presetChanged = true;
                            m_initialSyncDone = true;
                        }
                        receivedFullDump = true;

                        QString presetName = (m_currentPreset < m_workingPresets.size())
                                                 ? m_workingPresets[m_currentPreset].name
                                                 : QString("Preset %1").arg(m_currentPreset + 1);

                        ui->midiLog->appendHtml(QString("<font color='#45e28a'>📥 [RX] Full parameters for Preset %1 (%2) loaded.</font>")
                                                    .arg(m_currentPreset + 1)
                                                    .arg(presetName));

                        // 2. Lecture des potentiomètres (startIndex = 13)
                        int valGain      = read14Bit(13);
                        int valBass      = read14Bit(15);
                        int valMid       = read14Bit(17);
                        int valVolume    = read14Bit(19);
                        int valTreble    = read14Bit(21);
                        int valResonance = read14Bit(23);
                        int valPresence  = read14Bit(25);
                        int valRevLevel  = read14Bit(27);
                        int valDelLevel  = read14Bit(29);
                        int valDelTime   = read14Bit(31);
                        int valDelFb     = read14Bit(33);
                        int valModInt    = read14Bit(35);
                        int valModType   = read14Bit(37);

                        ui->knobGain->setValue(valGain);              ui->valGain->setText(QString::number(valGain));
                        ui->knobBass->setValue(valBass);              ui->valBass->setText(QString::number(valBass));
                        ui->knobMid->setValue(valMid);                ui->valMid->setText(QString::number(valMid));
                        ui->knobVolume->setValue(valVolume);          ui->valVolume->setText(QString::number(valVolume));
                        ui->knobTreble->setValue(valTreble);          ui->valTreble->setText(QString::number(valTreble));

                        if (!m_globalPowerEq || isFirstSync) {
                            ui->knobResonance->setValue(valResonance);    ui->valResonance->setText(QString::number(valResonance));
                            ui->knobPresence->setValue(valPresence);      ui->valPresence->setText(QString::number(valPresence));
                        }

                        ui->knobRevLevel->setValue(valRevLevel);      ui->valRevLevel->setText(QString::number(valRevLevel));
                        ui->knobDelLevel->setValue(valDelLevel);      ui->valDelLevel->setText(QString::number(valDelLevel));
                        ui->knobDelFeedback->setValue(valDelFb);      ui->valDelFeedback->setText(QString::number(valDelFb));
                        ui->knobModIntensity->setValue(valModInt);    ui->valModIntensity->setText(QString::number(valModInt));

                        int modType = std::clamp(valModType / 64, 0, 3);
                        int modRate = (valModType == 255) ? 63 : (valModType % 64);
                        ui->modSelector->setValue(modType);
                        ui->knobModRate->setValue(modRate * 4);
                        ui->valModRate->setText(QString::number(modRate));

                        int delayMs = graphToDelayMs(valDelTime);
                        ui->knobDelTime->setValue(valDelTime);
                        if (ui->valDelTime) {
                            ui->valDelTime->setTextFormat(Qt::RichText);
                            ui->valDelTime->setText(QString("%1<br>%2 ms").arg(valDelTime).arg(delayMs));
                        }
                        QSpinBox *bpmSpin = ui->remotePanel->findChild<QSpinBox*>("bpmDisplay");
                        if (bpmSpin && delayMs > 0) {
                            bpmSpin->blockSignals(true);
                            bpmSpin->setValue(std::clamp(static_cast<int>(std::round(60000.0 / delayMs)), 44, 1176));
                            bpmSpin->blockSignals(false);
                        }

                        // 3. Lecture des octets qqByte (index 43) et rrByte (index 44)
                        int qqByte = *(data + 43);
                        int rrByte = *(data + 44);

                        ui->btnNoiseGate->setChecked((qqByte % 2) != 0);
                        ui->channelSelector->setValue(std::clamp(rrByte % 4, 0, 3));
                        ui->btnBoost->setChecked(((rrByte / 4) % 2) != 0);
                        ui->btnFxLoop->setChecked(((rrByte / 8) % 2) != 0);

                        if (!m_globalPowerSoak || isFirstSync) {
                            int soakId = std::clamp((rrByte / 16) % 8, 0, 4);
                            if (QAbstractButton *targetBtn = m_powerSoakGroup->button(soakId)) {
                                m_powerSoakGroup->blockSignals(true);
                                targetBtn->setChecked(true);
                                m_powerSoakGroup->blockSignals(false);
                            }
                        }
                    }
                }

                // Restauration des liaisons
                ui->knobGain->onValueChanged         = backupGain;
                ui->knobVolume->onValueChanged       = backupVolume;
                ui->knobBass->onValueChanged         = backupBass;
                ui->knobMid->onValueChanged          = backupMid;
                ui->knobTreble->onValueChanged       = backupTreble;
                ui->knobPresence->onValueChanged     = backupPresence;
                ui->knobResonance->onValueChanged    = backupResonance;
                ui->knobRevLevel->onValueChanged     = backupRevLevel;
                ui->knobDelLevel->onValueChanged     = backupDelLevel;
                ui->knobDelFeedback->onValueChanged  = backupDelFeedback;
                ui->knobDelTime->onValueChanged      = backupDelTime;
                ui->knobModRate->onValueChanged      = backupModRate;
                ui->knobModIntensity->onValueChanged = backupModIntensity;

                ui->btnBoost->blockSignals(false);
                ui->btnReverbSwitch->blockSignals(false);
                ui->btnFxLoop->blockSignals(false);
                ui->btnNoiseGate->blockSignals(false);
                ui->btnMute->blockSignals(false);
                ui->channelSelector->blockSignals(false);
                ui->modSelector->blockSignals(false);
                if (btnMod) btnMod->blockSignals(false);
                if (btnDel) btnDel->blockSignals(false);
            }
        }
        snd_seq_free_event(event);
    }

    if (presetChanged) {
        ui->remotePanel->blockSignals(true);
        updatePresetDisplay();
        if (!receivedFullDump && m_currentPreset < m_workingPresets.size()) {
            applyPresetToUi(m_workingPresets[m_currentPreset], false);
        }
        ui->lblCompStatus->setText("Original ●   Modified ○");
        ui->remotePanel->blockSignals(false);
    }
}







void MainWindow::sendMidiSysExParam(int paramID, int value255)
{
    if (!seq) return;

    int clamped = std::clamp(value255, 0, 255);
    unsigned char msb = static_cast<unsigned char>((clamped >> 7) & 0x7F);
    unsigned char lsb = static_cast<unsigned char>(clamped & 0x7F);

    unsigned char sysex[15] = {
        0xF0, 0x00, 0x20, 0x44,
        0x00, 0x10, 0x00, 0x09, 0x00, 0x04,
        static_cast<unsigned char>(paramID & 0x7F),
        msb,
        lsb,
        0x00,
        0xF7
    };

    // Checksum XOR Hughes & Kettner GM40 (octets 4 à 12 ^ 0x14)
    unsigned char xorSum = 0x14;
    for (int i = 4; i <= 12; ++i) {
        xorSum ^= sysex[i];
    }
    sysex[13] = xorSum & 0x7F;

    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, localPort);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);
    snd_seq_ev_set_sysex(&ev, sizeof(sysex), sysex);
    snd_seq_event_output(seq, &ev);
    snd_seq_drain_output(seq);

    QString hexStr;
    for (int i = 0; i < 15; ++i) {
        hexStr += QString("%1 ").arg(sysex[i], 2, 16, QLatin1Char('0')).toUpper();
    }
    // Remplacez l'affichage hexadécimal à la fin de sendMidiSysExParam() par :
    ui->midiLog->appendHtml(QString("<font color='#ff4d4d'>📤 [TX] %1</font>")
                                .arg(formatSysExLog(paramID, clamped)));

}




void MainWindow::updatePresetDisplay()
{
    int bank = (m_currentPreset / 4) + 1;
    int slotIdx = m_currentPreset % 4;
    const QString slotsStr = "ABCD";

    ui->presetNumber->setText(QString::number(m_currentPreset + 1));
    ui->presetBank->setText(QString("%1-%2").arg(bank).arg(slotsStr.at(slotIdx)));

    const auto &list = m_isComparingOriginal ? m_originalPresets : m_workingPresets;
    if (m_currentPreset >= 0 && m_currentPreset < list.size()) {
        ui->presetDisplay->setText(list.at(m_currentPreset).name);
    } else {
        ui->presetDisplay->setText(QString("Preset %1").arg(m_currentPreset + 1));
    }
}







void MainWindow::sendMidiProgramChange(int program)
{
    if (!seq) return;
    int outChannel = (m_midiChannel == -1) ? 0 : m_midiChannel;

    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, localPort);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);
    snd_seq_ev_set_pgmchange(&ev, outChannel, program);
    snd_seq_event_output(seq, &ev);
    snd_seq_drain_output(seq);

    QString chLog = (m_midiChannel == -1) ? "OMNI (Ch.1)" : QString("Ch.%1").arg(m_midiChannel + 1);
    ui->midiLog->appendHtml(QString("<font color='#ff4d4d'>📤 [TX %1] Program Change -> %2</font>")
                                .arg(chLog)
                                .arg(program + 1));
}






void MainWindow::requestAmpState()
{
    if (!seq) return;

    auto sendRawSysEx = [this](const unsigned char *bytes, size_t size) {
        snd_seq_event_t ev;
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_source(&ev, localPort);
        snd_seq_ev_set_subs(&ev);
        snd_seq_ev_set_direct(&ev);
        snd_seq_ev_set_sysex(&ev, size, const_cast<unsigned char*>(bytes));
        snd_seq_event_output(seq, &ev);
        snd_seq_drain_output(seq);
    };

    // 1. Demande de la configuration système GM40 (commande 0x10 -> réponse 0x50)
    const unsigned char reqConfig[] = {
        0xF0, 0x00, 0x20, 0x44, 0x00, 0x10, 0x00, 0x09, 0x00, 0x10, 0x1D, 0xF7
    };
    sendRawSysEx(reqConfig, sizeof(reqConfig));

    // 2. Demande de l'Edit Buffer GM40 (commande 0x00 7F 7F 00 -> réponse 0x40 de 47 octets)
    QTimer::singleShot(150, this, [this, sendRawSysEx]() {
        if (!seq) return;
        const unsigned char reqEditBuffer[] = {
            0xF0, 0x00, 0x20, 0x44, 0x00, 0x10, 0x00, 0x09, 0x00, 0x00, 0x7F, 0x7F, 0x00, 0x0D, 0xF7
        };
        sendRawSysEx(reqEditBuffer, sizeof(reqEditBuffer));
    });

    ui->midiLog->appendHtml("<font color='#45e28a'>🔄 Sync request sent (Config 0x10 + Edit Buffer 0x00)...</font>");
}







void MainWindow::initDefaultPresets()
{
    m_originalPresets.resize(128);
    for (int i = 0; i < 128; ++i) {
        m_originalPresets[i].number = i + 1;
        m_originalPresets[i].name = QString("Preset %1").arg(i + 1);
    }
    m_workingPresets = m_originalPresets;
    ui->lblPath->setText("Current library file used : no file loaded");
}






void MainWindow::loadGm40File(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&file);
    PresetData current;
    int currentIdx = -1;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        if (line == "[preset]") {
            if (currentIdx >= 0 && currentIdx < 128) {
                m_originalPresets[currentIdx] = current;
            }
            current = PresetData();
        } else if (line.contains('=')) {
            int eqPos = line.indexOf('=');
            QString key = line.left(eqPos).trimmed();
            QString val = line.mid(eqPos + 1).trimmed();

            if (key == "number") {
                current.number = val.toInt();
                currentIdx = current.number - 1;
            }
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
    if (currentIdx >= 0 && currentIdx < 128) {
        m_originalPresets[currentIdx] = current;
    }

    m_workingPresets = m_originalPresets;
    m_currentLibraryPath = filePath;
    m_isComparingOriginal = false;

    ui->lblPath->setText(QString("Current library file used : %1").arg(filePath));
    ui->lblCompStatus->setText("Original ●   Modified ○");
    applyPresetToUi(m_workingPresets[m_currentPreset], false);
    updatePresetDisplay();
    ui->midiLog->appendHtml(QString("<font color='#45e28a'>📂 Library loaded: %1</font>").arg(filePath));

    // Mémorise le fichier pour le recharger automatiquement au prochain démarrage
    if (!filePath.startsWith(":/")) {
        QSettings("HughesKettner", "GM40Remote").setValue("paths/lastLibraryFile", filePath);
    }
}






void MainWindow::saveGm40File(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&file);
    for (int i = 0; i < 128; ++i) {
        const PresetData &p = m_workingPresets[i];
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

    m_currentLibraryPath = filePath;
    ui->lblPath->setText(QString("Current library file used : %1").arg(filePath));
    ui->midiLog->appendHtml(QString("<font color='#45e28a'>💾 File saved: %1</font>").arg(filePath));

    m_originalPresets = m_workingPresets;
    ui->lblCompStatus->setText("Original ●   Modified ○");

    // Mémorise le fichier pour le recharger automatiquement au prochain démarrage
    if (!filePath.startsWith(":/")) {
        QSettings("HughesKettner", "GM40Remote").setValue("paths/lastLibraryFile", filePath);
    }
}






PresetData MainWindow::captureCurrentUiState() const
{
    PresetData p;
    const PresetData &existing = m_workingPresets[m_currentPreset];

    p.number        = m_currentPreset + 1;
    p.name          = ui->presetDisplay->text().trimmed();
    p.modIntensity  = ui->knobModIntensity->value();

    // Si Global Power Soak est actif, on conserve la valeur propre du preset
    if (m_globalPowerSoak) {
        p.powerSoak = existing.powerSoak;
    } else {
        int soakValues[] = {0, 32, 64, 96, 124};
        int soakId = std::clamp(m_powerSoakGroup->checkedId(), 0, 4);
        p.powerSoak = soakValues[soakId];
    }

    p.delayTime     = ui->knobDelTime->value();
    p.channelType   = std::clamp(ui->channelSelector->value(), 0, 3) * 42;
    p.noiseGate     = ui->btnNoiseGate->isChecked() ? 127 : 0;
    p.channelBoost  = ui->btnBoost->isChecked() ? 127 : 0;
    p.bass          = ui->knobBass->value();
    p.mid           = ui->knobMid->value();
    p.treble        = ui->knobTreble->value();

    int mType       = std::clamp(ui->modSelector->value(), 0, 3);
    int mRate       = std::clamp(ui->knobModRate->value() / 4, 0, 63);
    p.modType       = (mType == 3 && mRate == 63) ? 255 : (mType * 64 + mRate);

    // Si Global Power Amp EQ est actif, on conserve Presence et Resonance propres au preset
    if (m_globalPowerEq) {
        p.resonance = existing.resonance;
        p.presence  = existing.presence;
    } else {
        p.resonance = ui->knobResonance->value();
        p.presence  = ui->knobPresence->value();
    }

    p.delayFeedback = ui->knobDelFeedback->value();
    p.volume        = ui->knobVolume->value();
    p.delayLevel    = ui->knobDelLevel->value();
    p.gain          = ui->knobGain->value();
    p.reverb        = ui->knobRevLevel->value();
    p.fxLoop        = ui->btnFxLoop->isChecked() ? 127 : 0;
    return p;
}







void MainWindow::applyPresetToUi(const PresetData &p, bool sendToAmp)
{
    m_isApplyingPreset = true; // Empêche markPresetAsModified() pendant le chargement

    auto setAllSignalsBlocked = [this](bool block) {
        ui->knobGain->blockSignals(block);
        ui->knobVolume->blockSignals(block);
        ui->knobBass->blockSignals(block);
        ui->knobMid->blockSignals(block);
        ui->knobTreble->blockSignals(block);
        ui->knobResonance->blockSignals(block);
        ui->knobPresence->blockSignals(block);
        ui->knobRevLevel->blockSignals(block);
        ui->knobDelLevel->blockSignals(block);
        ui->knobDelFeedback->blockSignals(block);
        ui->knobDelTime->blockSignals(block);
        ui->knobModRate->blockSignals(block);
        ui->knobModIntensity->blockSignals(block);

        ui->channelSelector->blockSignals(block);
        ui->modSelector->blockSignals(block);

        ui->btnBoost->blockSignals(block);
        ui->btnNoiseGate->blockSignals(block);
        ui->btnFxLoop->blockSignals(block);
        ui->btnReverbSwitch->blockSignals(block);
        ui->btnDelaySwitch->blockSignals(block);
        ui->btnModSwitch->blockSignals(block);
        m_powerSoakGroup->blockSignals(block);
    };

    if (!sendToAmp) {
        setAllSignalsBlocked(true);
    }

    // 1. Mise à jour du nom et du canal
    ui->presetDisplay->setText(p.name);
    ui->channelSelector->setValue(std::clamp(static_cast<int>(std::round(p.channelType / 42.0)), 0, 3));

    // 2. Potentiomètres Préampli et Effets
    ui->knobGain->setValue(p.gain);
    ui->knobVolume->setValue(p.volume);
    ui->knobBass->setValue(p.bass);
    ui->knobMid->setValue(p.mid);
    ui->knobTreble->setValue(p.treble);
    ui->knobRevLevel->setValue(p.reverb);
    ui->knobDelLevel->setValue(p.delayLevel);
    ui->knobDelFeedback->setValue(p.delayFeedback);
    ui->knobDelTime->setValue(p.delayTime);
    ui->knobModIntensity->setValue(p.modIntensity);

    ui->valGain->setText(QString::number(p.gain));
    ui->valVolume->setText(QString::number(p.volume));
    ui->valBass->setText(QString::number(p.bass));
    ui->valMid->setText(QString::number(p.mid));
    ui->valTreble->setText(QString::number(p.treble));
    ui->valRevLevel->setText(QString::number(p.reverb));
    ui->valDelLevel->setText(QString::number(p.delayLevel));
    ui->valDelFeedback->setText(QString::number(p.delayFeedback));
    ui->valModIntensity->setText(QString::number(p.modIntensity));

    // 3. Modulation (Type + Rate)
    int mType = std::clamp(p.modType / 64, 0, 3);
    int mRate = (p.modType == 255) ? 63 : (p.modType % 64);
    ui->modSelector->setValue(mType);
    ui->knobModRate->setValue(mRate * 4);
    ui->valModRate->setText(QString::number(mRate));

    // 4. Power Amp EQ (Presence & Resonance) — uniquement si le mode Global est désactivé
    if (!m_globalPowerEq) {
        ui->knobResonance->setValue(p.resonance);
        ui->knobPresence->setValue(p.presence);
        ui->valResonance->setText(QString::number(p.resonance));
        ui->valPresence->setText(QString::number(p.presence));
    }

    // 5. Power Soak — uniquement si le mode Global est désactivé
    if (!m_globalPowerSoak) {
        int soakId = std::clamp(static_cast<int>(std::round(p.powerSoak / 31.0)), 0, 4);
        if (auto *btn = m_powerSoakGroup->button(soakId)) {
            btn->setChecked(true);
        }
    }

    // 6. Interrupteurs On/Off
    ui->btnBoost->setChecked(p.channelBoost != 0);
    ui->btnNoiseGate->setChecked(p.noiseGate != 0);
    ui->btnFxLoop->setChecked(p.fxLoop != 0);
    ui->btnReverbSwitch->setChecked(p.reverb > 0);
    ui->btnDelaySwitch->setChecked(p.delayLevel > 0);
    ui->btnModSwitch->setChecked(p.modIntensity > 0);

    // 7. Affichage du Delay Time en ms et synchronisation du BPM
    int delayMs = graphToDelayMs(p.delayTime);
    ui->valDelTime->setText(QString("%1<br>%2 ms").arg(p.delayTime).arg(delayMs));

    if (delayMs > 0) {
        ui->bpmDisplay->blockSignals(true);
        ui->bpmDisplay->setValue(std::clamp(static_cast<int>(std::round(60000.0 / delayMs)), 44, 1176));
        ui->bpmDisplay->blockSignals(false);
    }

    if (!sendToAmp) {
        setAllSignalsBlocked(false);
    }
    m_isApplyingPreset = false; // Réactive la détection des retouches manuelles
}






QString MainWindow::formatSysExLog(int paramID, int value255) const
{
    const QStringList channels = {"Clean", "Crunch", "Lead", "Ultra"};
    const QStringList modTypes = {"Chorus", "Flanger", "Phaser", "Tremolo"};
    const QStringList soakLevels = {"No Speaker", "1W", "5W", "20W", "40W"};

    switch (paramID) {
    case 56: return QString("Gain -> %1").arg(value255);
    case 57: return QString("Volume -> %1").arg(value255);
    case 21: return QString("Bass -> %1").arg(value255);
    case 22: return QString("Mid -> %1").arg(value255);
    case 23: return QString("Treble -> %1").arg(value255);
    case 24: return QString("Resonance -> %1").arg(value255);
    case 25: return QString("Presence -> %1").arg(value255);

    case 29: return QString("Reverb Level -> %1").arg(value255);
    case 28: return QString("Delay Level -> %1").arg(value255);
    case 27: return QString("Delay Feedback -> %1").arg(value255);
    case 4:  return QString("Delay Time -> %1 (%2 ms)").arg(value255).arg(graphToDelayMs(value255));

    case 1:  return QString("Modulation Intensity -> %1").arg(value255);
    case 12: {
        int type = std::clamp(value255 / 64, 0, 3);
        int rate = (value255 == 255) ? 63 : (value255 % 64);
        return QString("Modulation -> %1 (Rate: %2)").arg(modTypes.at(type)).arg(rate);
    }

    case 31: {
        int chIdx = std::clamp(static_cast<int>(std::round(value255 / 42.0)), 0, 3);
        return QString("Channel -> %1").arg(channels.at(chIdx));
    }
    case 30: {
        int soakIdx = std::clamp(static_cast<int>(std::round(value255 / 31.0)), 0, 4);
        return QString("Power Soak -> %1").arg(soakLevels.at(soakIdx));
    }

    case 64: return QString("Boost -> %1").arg(value255 != 0 ? "ON" : "OFF");
    case 63: return QString("Noise Gate -> %1").arg(value255 != 0 ? "ON" : "OFF");
    case 54: return QString("Reverb -> %1").arg(value255 != 0 ? "ON" : "OFF");
    case 55: return QString("FX Loop -> %1").arg(value255 != 0 ? "ON" : "OFF");
    case 52: return QString("Modulation -> %1").arg(value255 != 0 ? "ON" : "OFF");
    case 53: return QString("Delay -> %1").arg(value255 != 0 ? "ON" : "OFF");
    case 9:  return QString("Global Mute -> %1").arg(value255 != 0 ? "ON" : "OFF");

    default: return QString("Parameter #%1 -> %2").arg(paramID).arg(value255);
    }
}




void MainWindow::openPresetSelectorDialog()
{
    QStringList names;
    names.reserve(m_workingPresets.size());
    for (const auto &p : m_workingPresets) {
        names.append(p.name);
    }

    PresetSelectorDialog dlg(names, m_currentPreset, this);
    if (dlg.exec() == QDialog::Accepted) {
        int chosenPreset = dlg.selectedPresetIndex();
        if (chosenPreset >= 0 && chosenPreset != m_currentPreset) {
            switchToPreset(chosenPreset);
        }
    }
}






void MainWindow::setupShortcuts()
{
    QSettings settings("HughesKettner", "GM40Remote");
    m_midiChannel     = settings.value("midi/channel", 0).toInt();
    m_globalPowerSoak = settings.value("global/powerSoak", false).toBool();
    m_globalPowerEq   = settings.value("global/powerEq", false).toBool();

    m_shortcuts["boost"]      = settings.value("shortcuts/boost", "Space").toString();
    m_shortcuts["mute"]       = settings.value("shortcuts/mute", "M").toString();
    m_shortcuts["nextPreset"] = settings.value("shortcuts/nextPreset", "Right").toString();
    m_shortcuts["prevPreset"] = settings.value("shortcuts/prevPreset", "Left").toString();
    m_shortcuts["tap"]        = settings.value("shortcuts/tap", "T").toString();
    m_shortcuts["presetList"] = settings.value("shortcuts/presetList", "L").toString();

    auto createBind = [this](const QString &key, auto callback) {
        auto *sc = new QShortcut(this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, this, callback);
        m_shortcutObjects[key] = sc;
    };

    createBind("boost",      [this]() { if (!ui->presetDisplay->hasFocus()) ui->btnBoost->toggle(); });
    createBind("mute",       [this]() { if (!ui->presetDisplay->hasFocus()) ui->btnMute->toggle(); });
    createBind("nextPreset", [this]() { if (!ui->presetDisplay->hasFocus()) ui->plusButton->click(); });
    createBind("prevPreset", [this]() { if (!ui->presetDisplay->hasFocus()) ui->minusButton->click(); });
    createBind("tap",        [this]() { if (!ui->presetDisplay->hasFocus()) ui->btnTap->click(); });
    createBind("presetList", [this]() { if (!ui->presetDisplay->hasFocus()) ui->btnPresetList->click(); });

    applyShortcuts();
}

void MainWindow::applyShortcuts()
{
    for (auto it = m_shortcutObjects.begin(); it != m_shortcutObjects.end(); ++it) {
        QString raw = m_shortcuts.value(it.key());
        QList<QKeySequence> keyList;

        const QStringList parts = raw.split(',', Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            QKeySequence seq = QKeySequence::fromString(part.trimmed(), QKeySequence::PortableText);
            if (!seq.isEmpty()) {
                keyList.append(seq);
            }
        }
        it.value()->setKeys(keyList);
    }
}


void MainWindow::openShortcutsDialog()
{
    ShortcutsDialog dlg(m_shortcuts, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_shortcuts = dlg.shortcuts();
        applyShortcuts();

        QSettings settings("HughesKettner", "GM40Remote");
        for (auto it = m_shortcuts.begin(); it != m_shortcuts.end(); ++it) {
            settings.setValue("shortcuts/" + it.key(), it.value());
        }
        ui->midiLog->appendHtml("<font color='#45e28a'>⌨️ Keyboard shortcuts updated.</font>");
    }
}






void MainWindow::sendSystemConfig()
{
    if (!seq) return;

    // Logique matérielle GM40 : Bit = 1 signifie "Par Preset", Bit = 0 signifie "Global"
    unsigned char globalFlags = 0;
    if (!m_globalPowerSoak) globalFlags |= 0x01; // 1 = par preset, 0 = Global
    if (!m_globalPowerEq)   globalFlags |= 0x02; // 1 = par preset, 0 = Global

    unsigned char sysex[14] = {
        0xF0, 0x00, 0x20, 0x44,
        0x00, 0x10, 0x00, 0x09, 0x00, 0x50,
        globalFlags,
        static_cast<unsigned char>(m_lastMutesByte & 0x7F),
        0x00,
        0xF7
    };

    // Calcul du checksum XOR (octets 4 à 11 ^ 0x14)
    unsigned char xorSum = 0x14;
    for (int i = 4; i <= 11; ++i) {
        xorSum ^= sysex[i];
    }
    sysex[12] = xorSum & 0x7F;

    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, localPort);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);
    snd_seq_ev_set_sysex(&ev, sizeof(sysex), sysex);
    snd_seq_event_output(seq, &ev);
    snd_seq_drain_output(seq);
}



void MainWindow::startFullBackup(bool askFilePath)
{
    if (!seq) {
        QMessageBox::warning(this, "MIDI", "Please connect to the amplifier first.");
        return;
    }

    m_isBackingUp = true;
    m_backupReceivedCount = 0;
    ui->btnBackup->setEnabled(false);
    ui->midiLog->appendHtml("<font color='#ffb52c'>⏳ Fetching all 128 presets from amplifier...</font>");

    // Envoie une requête toutes les 35 ms (durée totale : ~4,5 secondes)
    for (int i = 0; i < 128; ++i) {
        QTimer::singleShot(i * 35, this, [this, i, askFilePath]() {
            if (!seq) return;

            // Trame officielle de 15 octets (commande 0x00 + 3 octets : 0x00, index, 0x00)
            unsigned char sysex[15] = {
                0xF0, 0x00, 0x20, 0x44,
                0x00, 0x10, 0x00, 0x09, 0x00, 0x00,
                0x00,
                static_cast<unsigned char>(i & 0x7F),
                0x00,
                0x00,
                0xF7
            };

            // Checksum XOR sur les octets 4 à 12 ^ 0x14
            unsigned char xorSum = 0x14;
            for (int b = 4; b <= 12; ++b) {
                xorSum ^= sysex[b];
            }
            sysex[13] = xorSum & 0x7F;

            snd_seq_event_t ev;
            snd_seq_ev_clear(&ev);
            snd_seq_ev_set_source(&ev, localPort);
            snd_seq_ev_set_subs(&ev);
            snd_seq_ev_set_direct(&ev);
            snd_seq_ev_set_sysex(&ev, sizeof(sysex), sysex);
            snd_seq_event_output(seq, &ev);
            snd_seq_drain_output(seq);

            // Fin de la séquence après le 128e preset
            if (i == 127) {
                QTimer::singleShot(300, this, [this, askFilePath]() {
                    m_isBackingUp = false;
                    ui->btnBackup->setEnabled(true);

                    // Sécurité : si l'ampli n'a pas répondu, on annule l'enregistrement
                    if (m_backupReceivedCount == 0) {
                        ui->midiLog->appendHtml("<font color='#ff4d4d'>❌ Backup failed: no response from amplifier (check power and cables).</font>");
                        if (askFilePath) {
                            QMessageBox::warning(this, "Backup Failed",
                                                       "The amplifier did not respond.\n"
                                                       "Please ensure it is powered on and both MIDI IN and OUT cables are connected.");
                        }
                        return;
                    }

                    m_originalPresets = m_workingPresets;
                    applyPresetToUi(m_workingPresets[m_currentPreset], false);
                    ui->midiLog->appendHtml(QString("<font color='#45e28a'>✅ Backup complete: %1/128 presets retrieved from amplifier !</font>")
                                                .arg(m_backupReceivedCount));

                    if (askFilePath) {
                        // Backup manuel : ouvre la boîte de dialogue pour choisir le fichier
                        QString file = QFileDialog::getSaveFileName(this, "Save Amplifier Backup",
                                                                    "backup_gm40.gm40", "GM40 Files (*.gm40)");
                        if (!file.isEmpty()) {
                            if (!file.endsWith(".gm40", Qt::CaseInsensitive)) file += ".gm40";
                            saveGm40File(file);
                        }
                    } else {
                        // Backup automatique initial : sauvegarde dans ~/.config/HughesKettner/
                        QSettings settings("HughesKettner", "GM40Remote");
                        QString configDir = QFileInfo(settings.fileName()).absolutePath();
                        QDir().mkpath(configDir);
                        saveGm40File(configDir + "/backup_gm40.gm40");
                    }
                });
            }
        });
    }
}





void MainWindow::sendPresetToAmp(int slotIndex, const PresetData &p)
{
    if (!seq) return;

    int targetSlot = std::clamp(slotIndex, 0, 127);

    // Récupère la référence disque du slot pour préserver ses valeurs si un mode Global est actif
    const PresetData &refPreset = (targetSlot < m_originalPresets.size()) ? m_originalPresets[targetSlot] : p;
    int finalResonance = m_globalPowerEq   ? refPreset.resonance : p.resonance;
    int finalPresence  = m_globalPowerEq   ? refPreset.presence  : p.presence;
    int finalPowerSoak = m_globalPowerSoak ? refPreset.powerSoak : p.powerSoak;

    // Préparation de la trame officielle de 47 octets (commande 0x40)
    unsigned char sysex[47] = {0};
    sysex[0] = 0xF0; sysex[1] = 0x00; sysex[2] = 0x20; sysex[3] = 0x44;
    sysex[4] = 0x00; sysex[5] = 0x10; sysex[6] = 0x00; sysex[7] = 0x09;
    sysex[8] = 0x00; sysex[9] = 0x40;

    // Numéro du preset cible (0..127)
    sysex[10] = 0x00;
    sysex[11] = static_cast<unsigned char>(targetSlot);
    sysex[12] = static_cast<unsigned char>(targetSlot);

    // Helper pour encoder une valeur 0..255 sur 2 octets 7-bit (MSB, LSB)
    auto write14Bit = [&](int msbIdx, int val) {
        int clamped = std::clamp(val, 0, 255);
        sysex[msbIdx]     = static_cast<unsigned char>((clamped >> 7) & 0x7F);
        sysex[msbIdx + 1] = static_cast<unsigned char>(clamped & 0x7F);
    };

    // Encodage des 13 potentiomètres dans l'ordre exact du protocole GM40
    write14Bit(13, p.gain);
    write14Bit(15, p.bass);
    write14Bit(17, p.mid);
    write14Bit(19, p.volume);
    write14Bit(21, p.treble);
    write14Bit(23, finalResonance);
    write14Bit(25, finalPresence);
    write14Bit(27, p.reverb);
    write14Bit(29, p.delayLevel);
    write14Bit(31, p.delayTime);
    write14Bit(33, p.delayFeedback);
    write14Bit(35, p.modIntensity);
    write14Bit(37, p.modType);

    // Octet 43 (qqByte) : Noise Gate
    sysex[43] = (p.noiseGate != 0) ? 0x01 : 0x00;

    // Octet 44 (rrByte) : Channel + Boost + FX Loop + Power Soak
    int chIdx   = std::clamp(static_cast<int>(std::round(p.channelType / 42.0)), 0, 3);
    int boost   = (p.channelBoost != 0) ? 4 : 0;
    int fxLoop  = (p.fxLoop != 0) ? 8 : 0;
    int soakId  = std::clamp(static_cast<int>(std::round(finalPowerSoak / 31.0)), 0, 4);
    sysex[44]   = static_cast<unsigned char>(chIdx + boost + fxLoop + (soakId * 16));

    // Calcul du Checksum XOR (octets 4 à 44 ^ 0x14)
    unsigned char xorSum = 0x14;
    for (int i = 4; i <= 44; ++i) {
        xorSum ^= sysex[i];
    }
    sysex[45] = xorSum & 0x7F;
    sysex[46] = 0xF7;

    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, localPort);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);
    snd_seq_ev_set_sysex(&ev, sizeof(sysex), sysex);
    snd_seq_event_output(seq, &ev);
    snd_seq_drain_output(seq);
}






void MainWindow::sendCurrentPresetToAmp()
{
    if (!seq) {
        QMessageBox::warning(this, "MIDI", "Please connect to the amplifier first.");
        return;
    }

    // Capture l'état actuel de l'écran et le sauvegarde en mémoire
    PresetData current = captureCurrentUiState();
    m_workingPresets[m_currentPreset] = current;

    sendPresetToAmp(m_currentPreset, current);

    ui->midiLog->appendHtml(QString("<font color='#ff4d4d'>💾 [TX] Preset %1 (%2) saved to internal amp memory !</font>")
                                .arg(m_currentPreset + 1)
                                .arg(current.name));
}





void MainWindow::startFullRestore()
{
    if (!seq) {
        QMessageBox::warning(this, "MIDI", "Please connect to the amplifier first.");
        return;
    }

    QString sourceName = m_currentLibraryPath.startsWith(":/")
                             ? "Factory Bank"
                             : QFileInfo(m_currentLibraryPath).fileName();

    auto reply = QMessageBox::question(this, "Restore all 128 presets",
                                       QString("Disk source: %1\n\n"
                                               "Warning: this action will overwrite all 128 internal presets of your amplifier with the original file data.\n\n"
                                               "Do you want to continue?").arg(sourceName),
                                       QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    ui->btnRestore->setEnabled(false);
    ui->btnBackup->setEnabled(false);
    ui->midiLog->appendHtml(QString("<font color='#ffb52c'>⏳ Restoring '%1' to amplifier (~6 seconds)...</font>")
                                .arg(sourceName));

    // Envoie strictement la copie originale du disque (m_originalPresets)
    for (int i = 0; i < 128; ++i) {
        QTimer::singleShot(i * 45, this, [this, i]() {
            if (!seq) return;
            sendPresetToAmp(i, m_originalPresets[i]);

            if (i == 127) {
                QTimer::singleShot(250, this, [this]() {
                    // Réaligne également la mémoire de travail et l'écran sur le fichier restauré
                    m_workingPresets = m_originalPresets;
                    m_isComparingOriginal = false;
                    applyPresetToUi(m_workingPresets[m_currentPreset], false);
                    updatePresetDisplay();
                    ui->lblCompStatus->setText("Original ●   Modified ○");

                    ui->btnRestore->setEnabled(true);
                    ui->btnBackup->setEnabled(true);
                    sendMidiProgramChange(m_currentPreset);
                    ui->midiLog->appendHtml("<font color='#45e28a'>✅ Restore complete: all 128 presets written to amplifier !</font>");
                });
            }
        });
    }
}




void MainWindow::markPresetAsModified()
{
    if (m_isApplyingPreset) return;

    if (m_isComparingOriginal) {
        m_isComparingOriginal = false;
        ui->btnCompare->setStyleSheet("");
    }
    ui->lblCompStatus->setText("Original ○   Modified ●");
}




void MainWindow::switchToPreset(int targetPreset)
{
    if (targetPreset < 0 || targetPreset > 127 || targetPreset == m_currentPreset) {
        return;
    }

    // Vérifie si le preset actuel a été modifié (indicateur "Modified ●" actif)
    bool isModified = ui->lblCompStatus->text().contains("Modified ●");

    if (isModified) {
        PresetData modifiedData = m_isComparingOriginal
                                      ? m_workingPresets[m_currentPreset]
                                      : captureCurrentUiState();

        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Modified preset");
        msgBox.setIcon(QMessageBox::Question);
        msgBox.setText(QString("Preset #%1 (%2) has been modified.")
                           .arg(m_currentPreset + 1)
                           .arg(modifiedData.name));
        msgBox.setInformativeText("Do you want to save your changes before switching presets ?");

        QPushButton *btnBoth   = msgBox.addButton("Both (HD + Amp)", QMessageBox::AcceptRole);
        QPushButton *btnHd     = msgBox.addButton("hard drive (HD)", QMessageBox::ActionRole);
        QPushButton *btnAmp    = msgBox.addButton("Amplifier (Amp)", QMessageBox::ActionRole);
        QPushButton *btnIgnore = msgBox.addButton("Discard", QMessageBox::DestructiveRole);
        QPushButton *btnCancel = msgBox.addButton("Cancel", QMessageBox::RejectRole);

        msgBox.exec();
        QAbstractButton *clicked = msgBox.clickedButton();

        // Si l'utilisateur clique sur Annuler (ou ferme la croix), on reste sur le preset actuel
        if (clicked == btnCancel || clicked == nullptr) {
            return;
        }

        m_workingPresets[m_currentPreset] = modifiedData;

        // 1. Sauvegarde sur le disque dur (backup_gm40.gm40)
        if (clicked == btnHd || clicked == btnBoth) {
            QSettings settings("HughesKettner", "GM40Remote");
            QString configDir = QFileInfo(settings.fileName()).absolutePath();
            QDir().mkpath(configDir);
            saveGm40File(configDir + "/backup_gm40.gm40");
        }

        // 2. Sauvegarde dans la mémoire flash de l'amplificateur
        if (clicked == btnAmp || clicked == btnBoth) {
            if (!seq) {
                QMessageBox::warning(this, "MIDI Disconnected", "Cannot write to amplifier: MIDI cable not connected");
            } else {
                sendPresetToAmp(m_currentPreset, modifiedData);
                ui->midiLog->appendHtml(QString("<font color='#ff4d4d'>💾 Preset %1 written to internal amp memory.</font>")
                                            .arg(m_currentPreset + 1));
            }
        }

        // 3. Si Ignorer : on annule les modifications en mémoire vive en restaurant l'original
        if (clicked == btnIgnore) {
            m_workingPresets[m_currentPreset] = m_originalPresets[m_currentPreset];
        }
    }

    // Passage propre au nouveau preset demandé
    m_isComparingOriginal = false;
    ui->btnCompare->setStyleSheet("");
    m_currentPreset = targetPreset;

    updatePresetDisplay();
    applyPresetToUi(m_workingPresets[m_currentPreset], false);
    ui->lblCompStatus->setText("Original ●   Modified ○");

    sendMidiProgramChange(m_currentPreset);
}
