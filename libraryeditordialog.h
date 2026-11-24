#ifndef LIBRARYEDITORDIALOG_H
#define LIBRARYEDITORDIALOG_H

#include <QDialog>
#include <QVector>
#include <QCheckBox>
#include "mainwindow.h"
#include <QLabel>


namespace Ui {
class LibraryEditorDialog;
}

class LibraryEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LibraryEditorDialog(const QVector<PresetData> &activePresets,
                                 const QString &activeLibraryPath,
                                 QWidget *parent = nullptr);
    ~LibraryEditorDialog();

    QVector<PresetData> modifiedPresets() const { return m_leftPresets; }
    QString currentLibraryPath() const { return m_leftPath; }

signals:
    void auditionPresetRequested(const PresetData &preset);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onLoadRightFile();
    void onCopyRightToLeft();
    void onCopyLeftInternal();
    void onSwapLeftInternal();
    void onMoveUp();
    void onMoveDown();
    void onRenameLeft();
    void onInitLeftPreset();
    void onSaveLeft();
    void onSaveAsLeft();

private:
    Ui::LibraryEditorDialog *ui;
    QVector<PresetData> m_leftPresets;
    QVector<PresetData> m_rightPresets;
    QString m_leftPath;
    QString m_rightPath;
    QCheckBox *m_chkAutoAudition = nullptr;
    QLabel *m_lblPreview = nullptr;
    void updatePreview(const PresetData &p, const QString &sourceTag);
    void refreshLeftList();
    void refreshRightList();
    QString formatPresetRow(int index, const PresetData &p) const;
    bool loadGm40FromFile(const QString &filePath, QVector<PresetData> &targetList);
    bool saveGm40ToFile(const QString &filePath, const QVector<PresetData> &sourceList);
};

#endif // LIBRARYEDITORDIALOG_H
