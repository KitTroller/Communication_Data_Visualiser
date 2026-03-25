#ifndef SERIALHANDLER_H
#define SERIALHANDLER_H

#include <QObject>
#include <QSerialPort>
#include <QByteArray>
#include <QString>
#include <QtQml/qqml.h>

class SerialHandler : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SerialHandler)
public:

    explicit SerialHandler(QObject *parent = nullptr);
    ~SerialHandler();
    Q_INVOKABLE void connectToRadio(const QString &portName); // invokable allows C++ functions to be called inside QML # brokenfeature
    Q_INVOKABLE void disconnectRadio();

signals:
    void radioDataReceived(int rssi, float ber); //broadcast the parsed data to the frontend
    void connectionStatusChanged(bool isConnected, const QString &message); // lets frontend now when port is opened
private slots:
    void readData();

private:
    QSerialPort *m_serialPort;
    QByteArray m_serialBuffer; // Safely holds raw bytes until a full packet is formed

};

#endif // SERIALHANDLER_H
