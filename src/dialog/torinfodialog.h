// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2021, The Monero Project.

#ifndef FEATHER_TORINFODIALOG_H
#define FEATHER_TORINFODIALOG_H

#include <QDialog>
#include <QAbstractButton>

#include "appcontext.h"

namespace Ui {
    class TorInfoDialog;
}

class TorInfoDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TorInfoDialog(QWidget *parent, AppContext *ctx);
    ~TorInfoDialog() override;

public slots:
    void onLogsUpdated();

private slots:
    void onConnectionStatusChanged(bool connected);
    void onApplySettings();
    void onSettingsChanged();
    void onStopTor();

signals:
    void torSettingsChanged();

private:
    void initConnectionSettings();
    void initPrivacyLevel();

    Ui::TorInfoDialog *ui;
    AppContext *m_ctx;
};


#endif //FEATHER_TORINFODIALOG_H
