#ifndef SERIALHANDLER_H
#define SERIALHANDLER_H

#include <QObject>
#include <QSerialPort>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QFile>
#include <QTextStream>
#include <QtQml/qqml.h>

class SerialHandler : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SerialHandler)
    Q_PROPERTY(QStringList availablePorts READ availablePorts NOTIFY portsChanged)
    Q_PROPERTY(bool isLogging READ isLogging WRITE setIsLogging NOTIFY isLoggingChanged)

public:
    explicit SerialHandler(QObject *parent = nullptr);
    ~SerialHandler();

    Q_INVOKABLE void connectToRadio(const QString &portName);
    Q_INVOKABLE void disconnectRadio();
    Q_INVOKABLE void refreshPorts();
    Q_INVOKABLE void toggleLogging(bool enabled);

    QStringList availablePorts() const { return m_availablePorts; }
    bool isLogging() const { return m_isLogging; }
    void setIsLogging(bool logging);

signals:
    void radioDataReceived(int rssi, float ber, float snr, float plr);
    void constellationDataReceived(float i, float q);
    void connectionStatusChanged(bool isConnected, const QString &message);
    void portsChanged();
    void isLoggingChanged();

private slots:
    void readData();

private:
    QSerialPort *m_serialPort;
    QByteArray m_serialBuffer;
    QStringList m_availablePorts;
    
    // Logging logic
    bool m_isLogging = false;
    QFile m_logFile;
    QTextStream m_logStream;
    
    // Packet Loss Tracking
    int m_lastSequence = -1;
    int m_packetsLost = 0;
    int m_totalPackets = 0;

    void updateAvailablePorts();
    void logData(const QString &data);
};

#endif // SERIALHANDLER_H
