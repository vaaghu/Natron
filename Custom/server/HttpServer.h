#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

class HttpServer : public QObject
{
  Q_OBJECT

public:
  explicit HttpServer(unsigned short port, QObject *parent = nullptr);
  ~HttpServer();

  void start();

private Q_SLOTS:
  void onNewConnection();
  void onReadyRead();
  void onDisconnected();

private:
  QTcpServer m_server;
  unsigned short m_port;
};
