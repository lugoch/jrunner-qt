#pragma once

#include <QDialog>

#include "app/app_store.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;

namespace jrunner {

// 【设置对话框】对应 macOS 版 SettingsSheet.swift:外观、扫描目录、JDK 列表。

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(AppStore *store, QWidget *parent = nullptr);

private:
    void rebuildJdkList();

    AppStore *m_store;
    QComboBox *m_appearancePicker = nullptr;
    QPlainTextEdit *m_rootsEdit = nullptr;
    QCheckBox *m_mcpCheck = nullptr;
    QWidget *m_jdkListContainer = nullptr;
    QLabel *m_jdkCountLabel = nullptr;
};

} // namespace jrunner
