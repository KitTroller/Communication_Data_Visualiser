#include "serialhandler.h"
#include <QDebug>
#include <QSerialPortInfo>
#include <QDateTime>
#include <QDir>

SerialHandler::SerialHandler(QObject *parent) 
    : QObject(parent), m_serialPort(new QSerialPort) {
    connect(m_serialPort, &QSerialPort::readyRead, this, &SerialHandler::readData);
    updateAvailablePorts();
}

SerialHandler::~SerialHandler() {
    if (m_serialPort->isOpen())
        m_serialPort->close();
    if (m_logFile.isOpen())
        m_logFile.close();
}

void SerialHandler::updateAvailablePorts() {
    m_availablePorts.clear();
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        m_availablePorts.append(info.portName());
    }
    emit portsChanged();
}

void SerialHandler::refreshPorts() {
    updateAvailablePorts();
}

void SerialHandler::connectToRadio(const QString &portName) {
    if (m_serialPort->isOpen())
        m_serialPort->close();

    m_serialPort->setPortName(portName);
    m_serialPort->setBaudRate(QSerialPort::Baud115200);
    m_serialPort->setDataBits(QSerialPort::Data8);
    m_serialPort->setParity(QSerialPort::NoParity);
    m_serialPort->setStopBits(QSerialPort::OneStop);
    m_serialPort->setFlowControl(QSerialPort::NoFlowControl);

    if (m_serialPort->open(QIODevice::ReadOnly)) {
        emit connectionStatusChanged(true, "Connected to " + portName);
    } else {
        emit connectionStatusChanged(false, "Failed: " + m_serialPort->errorString());
    }
}

void SerialHandler::disconnectRadio() {
    if (m_serialPort->isOpen()) {
        m_serialPort->close();
        emit connectionStatusChanged(false, "Disconnected");
    }
}

void SerialHandler::toggleLogging(bool enabled) {
    if (enabled) {
        QString fileName = QString("telemetry_log_%1.csv")
                            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
        m_logFile.setFileName(fileName);
        if (m_logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            m_logStream.setDevice(&m_logFile);
            m_logStream << "Timestamp,RSSI,BER,SNR,PLR,I,Q\n";
            m_isLogging = true;
        }
    } else {
        m_isLogging = false;
        if (m_logFile.isOpen()) m_logFile.close();
    }
    emit isLoggingChanged();
}

void SerialHandler::setIsLogging(bool logging) {
    if (m_isLogging != logging) {
        toggleLogging(logging);
    }
}

void SerialHandler::readData() {
    m_serialBuffer.append(m_serialPort->readAll());

    while (m_serialBuffer.contains('\n')) {
        int newlineIndex = m_serialBuffer.indexOf('\n');
        QByteArray rawLine = m_serialBuffer.left(newlineIndex);
        m_serialBuffer.remove(0, newlineIndex + 1);

        QString cleanLine = QString::fromUtf8(rawLine).trimmed();
        if (cleanLine.isEmpty()) continue;

        // Log raw data if enabled
        if (m_isLogging) logData(cleanLine);

        // Advanced Parsing Engine
        // Format: "RSSI:-85,BER:0.02,SNR:15.5,SEQ:102,I:0.707,Q:-0.707"
        int rssi = 0;
        float ber = 0.0f, snr = 0.0f, plr = 0.0f, iVal = 0.0f, qVal = 0.0f;
        int seq = -1;
        bool hasTelemetry = false, hasIQ = false;

        const QList<QString> parts = cleanLine.split(',');
        for (const QString &part : parts) {
            QString key = part.section(':', 0, 0);
            QString val = part.section(':', 1, 1);

            if (key == "RSSI") { rssi = val.toInt(); hasTelemetry = true; }
            else if (key == "BER") { ber = val.toFloat(); hasTelemetry = true; }
            else if (key == "SNR") { snr = val.toFloat(); hasTelemetry = true; }
            else if (key == "I") { iVal = val.toFloat(); hasIQ = true; }
            else if (key == "Q") { qVal = val.toFloat(); hasIQ = true; }
            else if (key == "SEQ") {
                seq = val.toInt();
                m_totalPackets++;
                if (m_lastSequence != -1 && seq != (m_lastSequence + 1)) {
                    m_packetsLost += (seq > m_lastSequence) ? (seq - m_lastSequence - 1) : 0;
                }
                m_lastSequence = seq;
                plr = (float)m_packetsLost / m_totalPackets * 100.0f;
                hasTelemetry = true;
            }
        }

        if (hasTelemetry) emit radioDataReceived(rssi, ber, snr, plr);
        if (hasIQ) emit constellationDataReceived(iVal, qVal);
    }
}

void SerialHandler::logData(const QString &data) {
    if (m_logStream.device()) {
        m_logStream << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << "," << data << "\n";
    }
}