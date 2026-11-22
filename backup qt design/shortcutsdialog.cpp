#include "shortcutsdialog.h"
#include "ui_shortcutsdialog.h"
#include <QPushButton>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTimer>
#include <QMessageBox>

ShortcutsDialog::ShortcutsDialog(const QMap<QString, QString> &currentShortcuts, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ShortcutsDialog)
{
    ui->setupUi(this);

    auto initField = [this](QKeySequenceEdit *edit, const QString &seqStr) {
        m_fieldTexts[edit] = seqStr;
        edit->installEventFilter(this);
        // Installe aussi le filtre sur le QLineEdit interne de QKeySequenceEdit
        if (auto *le = edit->findChild<QLineEdit*>()) {
            le->setReadOnly(true);
            le->installEventFilter(this);
            m_fieldTexts[le] = seqStr;
        }
        updateFieldDisplay(edit);
    };

    initField(ui->keyBoost,      currentShortcuts.value("boost"));
    initField(ui->keyMute,       currentShortcuts.value("mute"));
    initField(ui->keyNextPreset, currentShortcuts.value("nextPreset"));
    initField(ui->keyPrevPreset, currentShortcuts.value("prevPreset"));
    initField(ui->keyTap,        currentShortcuts.value("tap"));
    initField(ui->keyPresetList, currentShortcuts.value("presetList"));

    if (auto *resetBtn = ui->buttonBox->button(QDialogButtonBox::RestoreDefaults)) {
        connect(resetBtn, &QPushButton::clicked, this, &ShortcutsDialog::setDefaults);
    }
}

ShortcutsDialog::~ShortcutsDialog()
{
    delete ui;
}

void ShortcutsDialog::updateFieldDisplay(QWidget *widget)
{
    QString text = m_fieldTexts.value(widget);
    if (auto *le = widget->findChild<QLineEdit*>()) {
        le->setText(text);
    } else if (auto *leDirect = qobject_cast<QLineEdit*>(widget)) {
        leDirect->setText(text);
    }
}

bool ShortcutsDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (!m_fieldTexts.contains(obj)) {
        return QDialog::eventFilter(obj, event);
    }

    // Retrouve le widget parent QKeySequenceEdit
    QKeySequenceEdit *parentEdit = qobject_cast<QKeySequenceEdit*>(obj);
    if (!parentEdit) {
        parentEdit = qobject_cast<QKeySequenceEdit*>(obj->parent());
    }
    if (!parentEdit) return true;

    // 1. Neutralise le comportement interne de QKeySequenceEdit lors du clic / perte de focus
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
        if (auto *le = parentEdit->findChild<QLineEdit*>()) {
            le->setPlaceholderText("None");
        }
        // Réapplique immédiatement le texte mémorisé après l'événement de focus
        QTimer::singleShot(0, this, [this, parentEdit]() {
            updateFieldDisplay(parentEdit);
        });
        return false; // Laisse le focus visuel (contour bleu) s'afficher normalement
    }

    // 2. Capture des touches clavier
    if (event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent*>(event);
        int key = keyEvent->key();

        // Backspace ou Delete : efface tout
        if ((key == Qt::Key_Backspace || key == Qt::Key_Delete) &&
            keyEvent->modifiers() == Qt::NoModifier) {
            m_fieldTexts[parentEdit].clear();
            if (auto *le = parentEdit->findChild<QLineEdit*>()) m_fieldTexts[le].clear();
            updateFieldDisplay(parentEdit);
            return true;
        }

        // Ignore les touches modificatrices seules
        if (key == Qt::Key_Control || key == Qt::Key_Shift ||
            key == Qt::Key_Alt || key == Qt::Key_Meta || key == Qt::Key_AltGr) {
            return true;
        }

        // Ajoute la nouvelle touche à la liste
        QKeySequence newKey(QKeyCombination(keyEvent->modifiers(), static_cast<Qt::Key>(key)));
        QString keyStr = newKey.toString(QKeySequence::PortableText);

        // Vérifie si cette touche est déjà utilisée dans un AUTRE champ
        const QList<QKeySequenceEdit*> allEdits = {
            ui->keyBoost, ui->keyMute, ui->keyNextPreset,
            ui->keyPrevPreset, ui->keyTap, ui->keyPresetList
        };

        for (QKeySequenceEdit *otherEdit : allEdits) {
            if (otherEdit == parentEdit) continue;

            QStringList otherList = m_fieldTexts[otherEdit].split(", ", Qt::SkipEmptyParts);
            if (otherList.contains(keyStr)) {
                QMessageBox::warning(this,
                                     "Duplicate Shortcut",
                                     QString("The key '%1' is already assigned to another action.\n"
                                             "Please remove it there first or choose another key.")
                                         .arg(keyStr));
                return true; // Bloque l'ajout en doublon
            }
        }

        // Vérifie aussi qu'elle n'est pas déjà dans le champ actuel
        QStringList list = m_fieldTexts[parentEdit].split(", ", Qt::SkipEmptyParts);
        if (!list.contains(keyStr)) {
            list.append(keyStr);
        }

        QString combined = list.join(", ");
        m_fieldTexts[parentEdit] = combined;
        if (auto *le = parentEdit->findChild<QLineEdit*>()) m_fieldTexts[le] = combined;
        updateFieldDisplay(parentEdit);

        return true;
    }
    return QDialog::eventFilter(obj, event);
}

void ShortcutsDialog::setDefaults()
{
    auto resetField = [this](QKeySequenceEdit *edit, const QString &def) {
        m_fieldTexts[edit] = def;
        if (auto *le = edit->findChild<QLineEdit*>()) m_fieldTexts[le] = def;
        updateFieldDisplay(edit);
    };

    resetField(ui->keyBoost,      "Space");
    resetField(ui->keyMute,       "M");
    resetField(ui->keyNextPreset, "Right");
    resetField(ui->keyPrevPreset, "Left");
    resetField(ui->keyTap,        "T");
    resetField(ui->keyPresetList, "L");
}

QMap<QString, QString> ShortcutsDialog::shortcuts() const
{
    QMap<QString, QString> map;
    map["boost"]      = m_fieldTexts.value(ui->keyBoost);
    map["mute"]       = m_fieldTexts.value(ui->keyMute);
    map["nextPreset"] = m_fieldTexts.value(ui->keyNextPreset);
    map["prevPreset"] = m_fieldTexts.value(ui->keyPrevPreset);
    map["tap"]        = m_fieldTexts.value(ui->keyTap);
    map["presetList"] = m_fieldTexts.value(ui->keyPresetList);
    return map;
}
