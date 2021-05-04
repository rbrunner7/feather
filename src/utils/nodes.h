// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2021, The Monero Project.

#ifndef FEATHER_NODES_H
#define FEATHER_NODES_H

#include <QTimer>
#include <QRegExp>
#include <QApplication>
#include <QtNetwork>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include "model/NodeModel.h"
#include "utils/utils.h"
#include "utils/config.h"

enum NodeSource {
    websocket = 0,
    custom
};

struct FeatherNode {
    explicit FeatherNode(QString address = "", int height = 0, int target_height = 0, bool online = false)
            : height(height)
            , target_height(target_height)
            , online(online)
    {
        if (address.isEmpty())
            return;

        address.remove("https://"); // todo: regex
        if (!address.startsWith("http://"))
            address.prepend("http://");

        url = QUrl(address);

        if (!url.isValid())
            return;

        if (url.port() == -1)
            url.setPort(18081);
    };

    int height;
    int target_height;
    bool online = false;
    bool cached = false;
    bool custom = false;
    bool isConnecting = false;
    bool isActive = false;
    QUrl url;

    bool isValid() const {
        return url.isValid();
    }

    bool isLocal() const {
        return (url.host() == "127.0.0.1" || url.host() == "localhost");
    }

    bool isOnion() const {
        return url.host().endsWith(".onion");
    }

    QString toAddress() const {
        return QString("%1:%2").arg(url.host(), QString::number(url.port()));
    }

    QString toFullAddress() const {
        if (!url.userName().isEmpty() && !url.password().isEmpty())
            return QString("%1:%2@%3:%4").arg(url.userName(), url.password(), url.host(), QString::number(url.port()));

        return toAddress();
    }

    QString toURL() const {
        QUrl withScheme(url);
        withScheme.setScheme("http");

        return withScheme.toString(QUrl::RemoveUserInfo | QUrl::RemovePath);
    }

    bool operator == (const FeatherNode &other) const {
        return this->url == other.url;
    }
};

class Nodes : public QObject {
    Q_OBJECT

public:
    explicit Nodes(AppContext *ctx, QObject *parent = nullptr);
    void loadConfig();
    void writeConfig();

    NodeSource source();
    FeatherNode connection();

    QList<FeatherNode> customNodes();
    QList<FeatherNode> websocketNodes();

    NodeModel *modelWebsocket;
    NodeModel *modelCustom;

public slots:
    void connectToNode();
    void connectToNode(const FeatherNode &node);
    void onWSNodesReceived(QList<FeatherNode>& nodes);
    void onNodeSourceChanged(NodeSource nodeSource);
    void setCustomNodes(const QList<FeatherNode>& nodes);
    void autoConnect(bool forceReconnect = false);

    void onTorSettingsChanged();

signals:
    void WSNodeExhausted();
    void nodeExhausted();
    void updateStatus(const QString &msg);

private slots:
    void onWalletRefreshed();

private:
    AppContext *m_ctx = nullptr;
    QJsonObject m_configJson;

    QStringList m_recentFailures;

    QList<FeatherNode> m_customNodes;
    QList<FeatherNode> m_websocketNodes;

    FeatherNode m_connection;  // current active connection, if any

    bool m_wsNodesReceived = false;
    bool m_wsExhaustedWarningEmitted = true;
    bool m_customExhaustedWarningEmitted = true;
    bool m_enableAutoconnect = true;

    FeatherNode pickEligibleNode();

    bool useOnionNodes();
    bool useTorProxy(const FeatherNode &node);

    void updateModels();
    void resetLocalState();
    void exhausted();
    void WSNodeExhaustedWarning();
    void nodeExhaustedWarning();
    int modeHeight(const QList<FeatherNode> &nodes);
};

#endif //FEATHER_NODES_H
