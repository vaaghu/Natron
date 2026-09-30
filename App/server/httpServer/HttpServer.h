#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

class HttpServer : public QObject {
    Q_OBJECT

public:
    explicit HttpServer(quint16 port = 3000, QObject* parent = nullptr);
    ~HttpServer() override;

    bool start();
    void stop();

    bool isRunning() const;
    quint16 port() const;

signals:
    void requestReceived(
        const QString& method,
        const QString& path,
        const QJsonObject& body);

private slots:
    void onNewConnection();

private:
    void handleSocket(QTcpSocket* socket);

    void handleRequest(
        QTcpSocket* socket,
        const QByteArray& request);

    void sendResponse(
        QTcpSocket* socket,
        int statusCode,
        const QByteArray& statusText,
        const QByteArray& body,
        const QByteArray& contentType = "application/json");

    void sendJsonResponse(
        QTcpSocket* socket,
        int statusCode,
        const QJsonObject& json);

    QJsonObject parseJsonBody(
        const QByteArray& body,
        bool* ok) const;

    QTcpServer* _server;
    quint16 _port;
};