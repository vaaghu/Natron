#include <HttpServer.h>

#include <QDateTime>
#include <QDebug>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonParseError>

HttpServer::HttpServer(quint16 port, QObject* parent)
    : QObject(parent)
    , _server(new QTcpServer(this))
    , _port(port)
{
    connect(
        _server,
        &QTcpServer::newConnection,
        this,
        &HttpServer::onNewConnection);
}

HttpServer::~HttpServer()
{
    stop();
}

bool
HttpServer::start()
{
    if (_server->isListening()) {
        qDebug() << "HTTP server is already running on port"
                 << _server->serverPort();

        return true;
    }

    const bool success = _server->listen(
        QHostAddress::LocalHost,
        _port);

    if (!success) {
        qWarning() << "Failed to start HTTP server:"
                   << _server->errorString();

        return false;
    }

    qDebug() << "HTTP server listening on"
             << "127.0.0.1:" << _server->serverPort();

    return true;
}

void
HttpServer::stop()
{
    if (!_server->isListening()) {
        return;
    }

    _server->close();

    qDebug() << "HTTP server stopped";
}

bool
HttpServer::isRunning() const
{
    return _server->isListening();
}

quint16
HttpServer::port() const
{
    return _server->serverPort();
}

void
HttpServer::onNewConnection()
{
    while (_server->hasPendingConnections()) {

        QTcpSocket* socket = _server->nextPendingConnection();

        if (!socket) {
            continue;
        }

        connect(
            socket,
            &QTcpSocket::readyRead,
            this,
            [this, socket]() {
                handleSocket(socket);
            });

        connect(
            socket,
            &QTcpSocket::disconnected,
            socket,
            &QTcpSocket::deleteLater);
    }
}

void
HttpServer::handleSocket(QTcpSocket* socket)
{
    if (!socket) {
        return;
    }

    const QByteArray request = socket->readAll();

    if (request.isEmpty()) {
        return;
    }

    handleRequest(socket, request);
}

void
HttpServer::handleRequest(
    QTcpSocket* socket,
    const QByteArray& request)
{
    /*
     * Example request:
     *
     * POST /api/test HTTP/1.1
     * Host: localhost:3000
     * Content-Type: application/json
     * Content-Length: 20
     *
     * {"hello":"world"}
     */

    const int headerEnd = request.indexOf("\r\n\r\n");

    if (headerEnd == -1) {
        sendResponse(
            socket,
            400,
            "Bad Request",
            R"({"error":"Invalid HTTP request"})");

        return;
    }

    const QByteArray headerData = request.left(headerEnd);

    const QByteArray body = request.mid(headerEnd + 4);

    const QList<QByteArray> lines = headerData.split('\n');

    if (lines.isEmpty()) {
        sendResponse(
            socket,
            400,
            "Bad Request",
            R"({"error":"Missing HTTP request line"})");

        return;
    }

    /*
     * Parse:
     *
     * POST /api/test HTTP/1.1
     */
    const QByteArray requestLine = lines.first().trimmed();

    const QList<QByteArray> requestParts = requestLine.split(' ');

    if (requestParts.size() != 3) {
        sendResponse(
            socket,
            400,
            "Bad Request",
            R"({"error":"Invalid HTTP request line"})");

        return;
    }

    const QString method = QString::fromUtf8(requestParts.at(0));

    const QString path = QString::fromUtf8(requestParts.at(1));

    const QString httpVersion = QString::fromUtf8(requestParts.at(2));

    if (httpVersion != QStringLiteral("HTTP/1.0") && httpVersion != QStringLiteral("HTTP/1.1")) {

        sendResponse(
            socket,
            400,
            "Bad Request",
            R"({"error":"Unsupported HTTP version"})");

        return;
    }

    /*
     * Parse JSON body if present.
     */
    QJsonObject jsonBody;

    if (!body.trimmed().isEmpty()) {

        bool jsonOk = false;

        jsonBody = parseJsonBody(
            body,
            &jsonOk);

        if (!jsonOk) {

            sendResponse(
                socket,
                400,
                "Bad Request",
                R"({"error":"Invalid JSON body"})");

            return;
        }
    }

    qDebug()
        << "HTTP"
        << method
        << path;

    /*
     * Simple routing.
     */

    if (method == QStringLiteral("GET") && path == QStringLiteral("/")) {

        QJsonObject response;

        response["status"] = "ok";
        response["service"] = "Natron HTTP Server";

        sendJsonResponse(
            socket,
            200,
            response);

        return;
    }

    if (method == QStringLiteral("GET") && path == QStringLiteral("/health")) {

        QJsonObject response;

        response["status"] = "ok";

        sendJsonResponse(
            socket,
            200,
            response);

        return;
    }

    if (method == QStringLiteral("POST") && path == QStringLiteral("/api/test")) {

        QJsonObject response;

        response["status"] = "ok";
        response["message"] = "Request received";
        response["method"] = method;
        response["path"] = path;
        response["body"] = jsonBody;

        /*
         * Notify Natron/application code.
         */
        emit requestReceived(
            method,
            path,
            jsonBody);

        sendJsonResponse(
            socket,
            200,
            response);

        return;
    }

    /*
     * Route wasn't found.
     */
    QJsonObject response;

    response["error"] = "Not Found";
    response["path"] = path;

    sendJsonResponse(
        socket,
        404,
        response);
}

QJsonObject
HttpServer::parseJsonBody(
    const QByteArray& body,
    bool* ok) const
{
    if (ok) {
        *ok = false;
    }

    QJsonParseError error;

    const QJsonDocument document = QJsonDocument::fromJson(
        body,
        &error);

    if (error.error != QJsonParseError::NoError) {

        qWarning()
            << "JSON parse error:"
            << error.errorString();

        return QJsonObject();
    }

    if (!document.isObject()) {

        qWarning()
            << "JSON body is not an object";

        return QJsonObject();
    }

    if (ok) {
        *ok = true;
    }

    return document.object();
}

void
HttpServer::sendJsonResponse(
    QTcpSocket* socket,
    int statusCode,
    const QJsonObject& json)
{
    const QJsonDocument document(json);

    const QByteArray body = document.toJson(QJsonDocument::Compact);

    QByteArray statusText;

    switch (statusCode) {

    case 200:
        statusText = "OK";
        break;

    case 201:
        statusText = "Created";
        break;

    case 400:
        statusText = "Bad Request";
        break;

    case 404:
        statusText = "Not Found";
        break;

    case 500:
        statusText = "Internal Server Error";
        break;

    default:
        statusText = "Unknown";
        break;
    }

    sendResponse(
        socket,
        statusCode,
        statusText,
        body,
        "application/json");
}

void
HttpServer::sendResponse(
    QTcpSocket* socket,
    int statusCode,
    const QByteArray& statusText,
    const QByteArray& body,
    const QByteArray& contentType)
{
    if (!socket) {
        return;
    }

    QByteArray response;

    response += "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText + "\r\n";

    response += "Content-Type: " + contentType + "\r\n";

    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";

    response += "Connection: close\r\n";

    response += "Access-Control-Allow-Origin: *\r\n";

    response += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";

    response += "Access-Control-Allow-Headers: Content-Type\r\n";

    response += "\r\n";

    response += body;

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}