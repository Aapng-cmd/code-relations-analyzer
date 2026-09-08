#pragma once

#include <QDialog>

class QRadioButton;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    void retranslate();

private:
    void buildUi();
    void syncFromConfig();

    QRadioButton *m_light = nullptr;
    QRadioButton *m_dark = nullptr;
    QRadioButton *m_en = nullptr;
    QRadioButton *m_ru = nullptr;
    class QGroupBox *m_themeBox = nullptr;
    class QGroupBox *m_langBox = nullptr;
    class QPushButton *m_close = nullptr;
};
