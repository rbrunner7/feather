// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: 2020-2023 The Monero Project

#ifndef FEATHER_SUBADDRESSROW_H
#define FEATHER_SUBADDRESSROW_H

#include <QObject>

class SubaddressRow : public QObject 
{
    Q_OBJECT
    
public:
    SubaddressRow(QObject *parent, qsizetype row, const QString& address, const QString &label, bool used, bool hidden, bool pinned)
        : QObject(parent)
        , m_row(row)
        , m_address(address)
        , m_label(label)
        , m_used(used) 
        , m_hidden(hidden)
        , m_pinned(pinned) {}
    
    qsizetype getRow() const;
    const QString& getAddress() const;
    const QString& getLabel() const;
    bool isUsed() const;
    bool isHidden() const;
    bool isPinned() const;
    
private:
    qsizetype m_row;
    QString m_address;
    QString m_label;
    bool m_used = false;
    bool m_hidden = false;
    bool m_pinned = false;
};


#endif //FEATHER_SUBADDRESSROW_H
