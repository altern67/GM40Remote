#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QWheelEvent>
#include <QComboBox>
#include <QPushButton>
#include <QToolButton>
#include <QPlainTextEdit>
#include <QMessageBox>
#include <QTimer>
#include <QMenuBar>
#include <QMenu>
#include <QPainter>
#include <QMouseEvent>
#include <QLinearGradient>
#include <QGroupBox>
#include <QSlider>
#include <QSpinBox>
#include <QSizePolicy>
#include <QStringList>
#include <functional>
#include <algorithm>
#include <cmath>
#include <QElapsedTimer>
#include <QStyleOption>
#include <QPainter>
#include <QButtonGroup>
#include <QQuickWidget>
#include <QQuickItem>
#include <QQmlContext>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QVector>
#include <QShortcut>
#include <QSettings>
#include <QMap>

#include <alsa/asoundlib.h>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE


// --- Widgets Personnalisés promus de manière transparente dans Qt Designer ---
class RemoteKnob : public QQuickWidget
{
    Q_OBJECT
public:
    explicit RemoteKnob(QWidget *parent = nullptr) : QQuickWidget(parent) {
        setFixedSize(84, 84);
        setResizeMode(QQuickWidget::SizeRootObjectToView);
        setClearColor(Qt::transparent);

        // Expose directement cet objet C++ à QML sous le nom "knobBridge"
        rootContext()->setContextProperty("knobBridge", this);
        setSource(QUrl("qrc:/MetalKnob.qml"));
    }

    Q_INVOKABLE void notifyValueFromQml(int newValue) {
        if (!signalsBlocked() && onValueChanged) {
            onValueChanged(newValue);
        }
    }

    void setMaximum(int maxVal) {
        m_pendingMax = maxVal;
        if (!rootObject()) return;
        rootObject()->setProperty("maxValue", maxVal);
        if (value() > maxVal) {
            setValue(maxVal / 2);
        }
    }

    int maximum() const {
        return rootObject() ? rootObject()->property("maxValue").toInt() : m_pendingMax;
    }

    int value() const {
        return rootObject() ? rootObject()->property("value").toInt() : 128;
    }

    void setValue(int value) {
        if (!rootObject()) return;
        int clamped = std::clamp(value, 0, maximum());
        if (this->value() == clamped) return;

        rootObject()->setProperty("value", clamped);
        if (!signalsBlocked() && onValueChanged) {
            onValueChanged(clamped);
        }
    }

    std::function<void(int)> onValueChanged = nullptr;

private:
    int m_pendingMax = 255;
};

// Remplace l'ancienne classe AmpRotarySelector
class AmpRotarySelector : public QQuickWidget
{
    Q_OBJECT
public:
    explicit AmpRotarySelector(QWidget *parent = nullptr) : QQuickWidget(parent) {
        setFixedSize(140, 160);
        setResizeMode(QQuickWidget::SizeRootObjectToView);
        setClearColor(Qt::transparent);
        setSource(QUrl("qrc:/AmpRotaryKnob.qml"));

        if (rootObject()) {
            connect(rootObject(), SIGNAL(selectorChanged(int)), this, SLOT(handleQmlChanged(int)));
        }
    }

    int value() const {
        return rootObject() ? rootObject()->property("value").toInt() : 0;
    }

    void setValue(int value) {
        if (!rootObject()) return;
        int clamped = std::clamp(value, 0, 3);
        rootObject()->setProperty("value", clamped);
        if (!signalsBlocked() && onSelectorChanged) {
            onSelectorChanged(clamped);
        }
    }

    void setOptions(const QStringList &opts, const QString &title) {
        if (!rootObject()) return;
        rootObject()->setProperty("options", QVariant::fromValue(opts));
        rootObject()->setProperty("title", title);
    }

    std::function<void(int)> onSelectorChanged = nullptr;

private slots:
    void handleQmlChanged(int index) {
        if (!signalsBlocked() && onSelectorChanged) {
            onSelectorChanged(index);
        }
    }
};

// Structure représentant un preset complet (.gm40)
struct PresetData {
    int number = 1;
    QString name = "Empty preset";
    int modIntensity = 0;
    int powerSoak = 124;
    int delayTime = 0;
    int channelType = 0;
    int noiseGate = 0;
    int channelBoost = 0;
    int bass = 127;
    int mid = 127;
    int treble = 127;
    int modType = 0;
    int resonance = 127;
    int presence = 127;
    int delayFeedback = 0;
    int volume = 127;
    int delayLevel = 0;
    int gain = 127;
    int reverb = 0;
    int fxLoop = 0;
};





// --- Classe principale MainWindow ---
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    snd_seq_t *seq = nullptr;
    int localPort = -1;
    QTimer midiTimer;
    QButtonGroup *m_powerSoakGroup = nullptr;
    int m_currentPreset = 0;
    int m_midiChannel = 0; // 0 = Canal 1, jusqu'à 15 = Canal 16
    bool m_initialSyncDone = false;
    QElapsedTimer tapTimer;
    QString formatSysExLog(int paramID, int value255) const;

    int m_backupReceivedCount = 0;

    // Gestion de la bibliothèque .gm40 (copie originale + copie de travail)
    QVector<PresetData> m_originalPresets;
    QVector<PresetData> m_workingPresets;
    QString m_currentLibraryPath;
    bool m_isComparingOriginal = false;

    void setupKnobLabels();
    void createMenus();
    void openSettingsDialog();
    void openPresetSelectorDialog();
    void refreshPorts();
    bool parsePort(const QString &value, int &client, int &port);
    void connectMidi();
    void disconnectMidi();
    void readMidi();
    void requestAmpState();
    void markPresetAsModified();
    bool m_isApplyingPreset = false;
    void switchToPreset(int targetPreset);

    //backup
    bool m_isBackingUp = false;
    void startFullBackup(bool askFilePath = true);

    // gestion des raccourcis claviers
    QMap<QString, QString> m_shortcuts;
    QMap<QString, QShortcut*> m_shortcutObjects;
    void setupShortcuts();
    void applyShortcuts();
    void openShortcutsDialog();

    // Gestion des presets .gm40
    void initDefaultPresets();
    void loadGm40File(const QString &filePath);
    void saveGm40File(const QString &filePath);
    PresetData captureCurrentUiState() const;
    void applyPresetToUi(const PresetData &p, bool sendToAmp);

    // Envoi SysEx natif GrandMeister 40 et Program Change
    void sendMidiSysExParam(int paramID, int value255);
    void sendMidiProgramChange(int program);
    void updatePresetDisplay();

    // global settings
    bool m_globalPowerSoak = false;
    bool m_globalPowerEq = false;
    int m_lastMutesByte = 0; // Mémorise l'octet 11 (mutes) reçu de l'ampli

    void sendSystemConfig();

    void sendPresetToAmp(int slotIndex, const PresetData &p);
    void sendCurrentPresetToAmp();
    void startFullRestore();


    // Conversion exacte Delay Time (128 niveaux MIDI de 51 ms à 1360 ms)
    int graphToDelayMs(int v) const {
        int midiStep = std::clamp(v / 2, 0, 127);
        return static_cast<int>(std::round(51.0 + (midiStep * 1309.0) / 127.0));
    }

    int delayMsToGraph(int ms) const {
        int clampedMs = std::clamp(ms, 51, 1360);
        int midiStep = static_cast<int>(std::round(((clampedMs - 51) * 127.0) / 1309.0));
        return std::clamp(midiStep * 2, 0, 255);
    }
};


#endif // MAINWINDOW_H
