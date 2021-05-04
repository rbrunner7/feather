// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2021, The Monero Project.

#include <QObject>

#include "nodes.h"
#include "utils/utils.h"
#include "utils/WebsocketClient.h"
#include "appcontext.h"


Nodes::Nodes(AppContext *ctx, QObject *parent)
    : QObject(parent)
    , m_ctx(ctx)
    , m_connection(FeatherNode())
    , modelWebsocket(new NodeModel(NodeSource::websocket, this))
    , modelCustom(new NodeModel(NodeSource::custom, this))
{
    this->loadConfig();
    connect(m_ctx, &AppContext::walletRefreshed, this, &Nodes::onWalletRefreshed);
}

void Nodes::loadConfig() {
    auto configNodes = config()->get(Config::nodes).toByteArray();
    auto key = QString::number(m_ctx->networkType);
    if (!Utils::validateJSON(configNodes)) {
        m_configJson[key] = QJsonObject();
        qCritical() << "Fixed malformed config key \"nodes\"";
    }

    QJsonDocument doc = QJsonDocument::fromJson(configNodes);
    m_configJson = doc.object();

    if (!m_configJson.contains(key))
        m_configJson[key] = QJsonObject();

    auto obj = m_configJson.value(key).toObject();
    if (!obj.contains("custom"))
        obj["custom"] = QJsonArray();
    if (!obj.contains("ws"))
        obj["ws"] = QJsonArray();

    // load custom nodes
    auto nodes = obj.value("custom").toArray();
    for (auto value: nodes) {
        auto customNode = FeatherNode(value.toString());
        customNode.custom = true;

        if(m_connection == customNode) {
            if(m_connection.isActive)
                customNode.isActive = true;
            else if(m_connection.isConnecting)
                customNode.isConnecting = true;
        }

        m_customNodes.append(customNode);
    }

    // load cached websocket nodes
    auto ws = obj.value("ws").toArray();
    for (auto value: ws) {
        auto wsNode = FeatherNode(value.toString());
        wsNode.custom = false;
        wsNode.online = true;  // assume online

        if (m_connection == wsNode) {
            if (m_connection.isActive)
                wsNode.isActive = true;
            else if (m_connection.isConnecting)
                wsNode.isConnecting = true;
        }

        m_websocketNodes.append(wsNode);
    }

    if (m_websocketNodes.count() > 0) {
        qDebug() << QString("Loaded %1 cached websocket nodes from config").arg(m_websocketNodes.count());
    }

    if (m_customNodes.count() > 0) {
        qDebug() << QString("Loaded %1 custom nodes from config").arg(m_customNodes.count());
    }

    // No nodes cached, fallback to hardcorded list
    if (m_websocketNodes.count() == 0) {
        QByteArray file = Utils::fileOpenQRC(":/assets/nodes.json");
        QJsonDocument nodes_json = QJsonDocument::fromJson(file);
        QJsonObject nodes_obj = nodes_json.object();

        QString netKey;
        if (m_ctx->networkType == NetworkType::MAINNET) {
            netKey = "mainnet";
        } else if (m_ctx->networkType == NetworkType::STAGENET) {
            netKey = "stagenet";
        }

        if (nodes_obj.contains(netKey)) {
            QJsonArray nodes_list;
            nodes_list = nodes_json[netKey].toObject()["tor"].toArray();
            nodes_list.append(nodes_list = nodes_json[netKey].toObject()["clearnet"].toArray());
            for (auto node: nodes_list) {
                auto wsNode = FeatherNode(node.toString());
                wsNode.custom = false;
                wsNode.online = true;
                m_websocketNodes.append(wsNode);
            }
        }

        qDebug() << QString("Loaded %1 nodes from hardcoded list").arg(m_websocketNodes.count());
    }

    m_configJson[key] = obj;
    this->writeConfig();
    this->updateModels();
}

void Nodes::writeConfig() {
    QJsonDocument doc(m_configJson);
    QString output(doc.toJson(QJsonDocument::Compact));
    config()->set(Config::nodes, output);

    qDebug() << "Saved node config.";
}

void Nodes::connectToNode() {
    // auto connect
    m_wsExhaustedWarningEmitted = false;
    m_customExhaustedWarningEmitted = false;
    this->autoConnect(true);
}

void Nodes::connectToNode(const FeatherNode &node) {
    if (!node.isValid())
        return;

    emit updateStatus(QString("Connecting to %1").arg(node.toAddress()));
    qInfo() << QString("Attempting to connect to %1 (%2)").arg(node.toAddress()).arg(node.custom ? "custom" : "ws");

    if (!node.url.userName().isEmpty() && !node.url.password().isEmpty())
        m_ctx->currentWallet->setDaemonLogin(node.url.userName(), node.url.password());

    // Don't use SSL over Tor
    m_ctx->currentWallet->setUseSSL(!node.isOnion());

    QString proxyAddress;
    if (useTorProxy(node)) {
        if (!torManager()->isLocalTor()) {
            proxyAddress = QString("%1:%2").arg(torManager()->featherTorHost, QString::number(torManager()->featherTorPort));
        } else {
            proxyAddress = QString("%1:%2").arg(config()->get(Config::socks5Host).toString(),
                                                config()->get(Config::socks5Port).toString());
        }
    }

    m_ctx->currentWallet->initAsync(node.toAddress(), true, 0, false, false, 0, proxyAddress);

    m_connection = node;
    m_connection.isActive = false;
    m_connection.isConnecting = true;

    this->resetLocalState();
    this->updateModels();
}

void Nodes::autoConnect(bool forceReconnect) {
    // this function is responsible for automatically connecting to a daemon.
    if (m_ctx->currentWallet == nullptr || !m_enableAutoconnect) {
        return;
    }

    Wallet::ConnectionStatus status = m_ctx->currentWallet->connectionStatus();
    bool wsMode = (this->source() == NodeSource::websocket);

    if (wsMode && !m_wsNodesReceived && websocketNodes().count() == 0) {
        // this situation should rarely occur due to the usage of the websocket node cache on startup.
        qInfo() << "Feather is in websocket connection mode but was not able to receive any nodes (yet).";
        return;
    }

    if (status == Wallet::ConnectionStatus_Disconnected || forceReconnect) {
        if (m_connection.isValid() && !forceReconnect) {
            m_recentFailures << m_connection.toAddress();
        }

        // try a connect
        auto node = this->pickEligibleNode();
        this->connectToNode(node);
        return;
    }
    else if ((status == Wallet::ConnectionStatus_Synchronizing || status == Wallet::ConnectionStatus_Synchronized) && m_connection.isConnecting) {
        qInfo() << QString("Node connected to %1").arg(m_connection.toAddress());

        // set current connection object
        m_connection.isConnecting = false;
        m_connection.isActive = true;

        // reset node exhaustion state
        m_wsExhaustedWarningEmitted = false;
        m_customExhaustedWarningEmitted = false;
        m_recentFailures.clear();
    }

    this->resetLocalState();
    this->updateModels();
}

FeatherNode Nodes::pickEligibleNode() {
    // Pick a node at random to connect to
    auto rtn = FeatherNode();
    auto wsMode = (this->source() == NodeSource::websocket);
    auto nodes = wsMode ? websocketNodes() : m_customNodes;

    if (nodes.count() == 0) {
        if (wsMode)
            this->exhausted();
        return rtn;
    }

    QVector<int> node_indeces;
    int i = 0;
    for (const auto &node: nodes) {
        node_indeces.push_back(i);
        i++;
    }
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::shuffle(node_indeces.begin(), node_indeces.end(), std::default_random_engine(seed));

    // Pick random eligible node
    int mode_height = this->modeHeight(nodes);
    for (int index : node_indeces) {
        const FeatherNode &node = nodes.at(index);

        // This may fail to detect bad nodes if cached nodes are used
        // Todo: wait on websocket before connecting, only use cache if websocket is unavailable
        if (wsMode && m_wsNodesReceived) {
            // Ignore offline nodes
            if (!node.online)
                continue;

            // Ignore nodes that are more than 25 blocks behind mode
            if (node.height < (mode_height - 25))
                continue;

            // Ignore nodes that say they aren't synchronized
            if (node.target_height > node.height)
                continue;
        }

        // Don't connect to nodes that failed to connect recently
        if (m_recentFailures.contains(node.toAddress())) {
            continue;
        }

        return node;
    }

    // All nodes tried, and none eligible
    // Don't show node exhaustion warning if single custom node is used
    if (wsMode || node_indeces.size() > 1) {
        this->exhausted();
    }
    return rtn;
}

void Nodes::onWSNodesReceived(QList<FeatherNode> &nodes) {
    m_websocketNodes.clear();

    m_wsNodesReceived = true;

    for (auto &node: nodes) {
        if (m_connection == node) {
            if (m_connection.isActive)
                node.isActive = true;
            else if (m_connection.isConnecting)
                node.isConnecting = true;
        }
        m_websocketNodes.push_back(node);
    }

    // cache into config
    auto key = QString::number(m_ctx->networkType);
    auto obj = m_configJson.value(key).toObject();
    auto ws = QJsonArray();
    for (auto const &node: m_websocketNodes)
        ws.push_back(node.toAddress());

    obj["ws"] = ws;
    m_configJson[key] = obj;
    this->writeConfig();
    this->resetLocalState();
    this->updateModels();
}

void Nodes::onNodeSourceChanged(NodeSource nodeSource) {
    this->resetLocalState();
    this->updateModels();
    this->connectToNode();
}

void Nodes::setCustomNodes(const QList<FeatherNode> &nodes) {
    m_customNodes.clear();
    auto key = QString::number(m_ctx->networkType);
    auto obj = m_configJson.value(key).toObject();

    QStringList nodesList;
    for (auto const &node: nodes) {
        if (nodesList.contains(node.toAddress())) continue;
        nodesList.append(node.toAddress());
        m_customNodes.append(node);
    }

    auto arr = QJsonArray::fromStringList(nodesList);
    obj["custom"] = arr;
    m_configJson[key] = obj;
    this->writeConfig();
    this->resetLocalState();
    this->updateModels();
}

void Nodes::onWalletRefreshed() {
    if (config()->get(Config::torPrivacyLevel).toInt() == Config::allTorExceptInitSync) {
        if (m_connection.isLocal())
            return;
        if (TailsOS::detect() || WhonixOS::detect())
            return;
        this->autoConnect(true);
    }
}

bool Nodes::useOnionNodes() {
    if (TailsOS::detect() || WhonixOS::detect()) {
        return true;
    }
    auto privacyLevel = config()->get(Config::torPrivacyLevel).toInt();
    if (privacyLevel == Config::allTor || (privacyLevel == Config::allTorExceptInitSync && m_ctx->refreshed)) {
        return true;
    }
    return false;
}

bool Nodes::useTorProxy(const FeatherNode &node) {
    if (node.isLocal()) {
        return false;
    }

    if (Utils::isTorsocks()) {
        return false;
    }

    return this->useOnionNodes();
}

void Nodes::updateModels() {
    this->modelCustom->updateNodes(m_customNodes);

    this->modelWebsocket->updateNodes(this->websocketNodes());
}

void Nodes::resetLocalState() {
    auto resetState = [this](QList<FeatherNode> &model){
        for (auto &node: model) {
            node.isConnecting = false;
            node.isActive = false;

            if (node == m_connection) {
                node.isActive = m_connection.isActive;
                node.isConnecting = m_connection.isConnecting;
            }
        }
    };

    resetState(m_customNodes);
    resetState(m_websocketNodes);
}

void Nodes::exhausted() {
    if (this->source() == NodeSource::websocket)
        this->WSNodeExhaustedWarning();
    else
        this->nodeExhaustedWarning();
}

void Nodes::nodeExhaustedWarning(){
    if (m_customExhaustedWarningEmitted)
        return;

    emit nodeExhausted();
    qWarning() << "Could not find an eligible custom node to connect to.";
    m_customExhaustedWarningEmitted = true;
}

void Nodes::WSNodeExhaustedWarning() {
    if (m_wsExhaustedWarningEmitted)
        return;

    emit WSNodeExhausted();
    qWarning() << "Could not find an eligible websocket node to connect to.";
    m_wsExhaustedWarningEmitted = true;
}

QList<FeatherNode> Nodes::customNodes() {
    return m_customNodes;
}

QList<FeatherNode> Nodes::websocketNodes() {
    bool onionNode = this->useOnionNodes();

    QList<FeatherNode> nodes;
    for (const auto &node : m_websocketNodes) {
        if (onionNode && !node.isOnion()) {
            continue;
        }

        if (!onionNode && node.isOnion()) {
            continue;
        }

        nodes.push_back(node);
    }

    return nodes;
}

void Nodes::onTorSettingsChanged() {
    this->autoConnect(true);
}

FeatherNode Nodes::connection() {
    return m_connection;
}

NodeSource Nodes::source() {
    return static_cast<NodeSource>(config()->get(Config::nodeSource).toInt());
}

int Nodes::modeHeight(const QList<FeatherNode> &nodes) {
    QVector<int> heights;
    for (const auto &node: nodes) {
        heights.push_back(node.height);
    }

    std::sort(heights.begin(), heights.end());

    int max_count = 1, mode_height = heights[0], count = 1;
    for (int i = 1; i < heights.count(); i++) {
        if (heights[i] == 0) { // Don't consider 0 height nodes
            continue;
        }

        if (heights[i] == heights[i - 1])
            count++;
        else {
            if (count > max_count) {
                max_count = count;
                mode_height = heights[i - 1];
            }
            count = 1;
        }
    }
    if (count > max_count)
    {
        mode_height = heights[heights.count() - 1];
    }

    return mode_height;
}