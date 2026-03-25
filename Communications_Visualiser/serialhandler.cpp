#include "serialhandler.h"
#include <QDebug>
#include <QList>

SerialHandler::SerialHandler(QObject *parent) : QObject(parent), m_serialPort(new QSerialPort) {
    connect(m_serialPort, &QSerialPort::readyRead, this, &SerialHandler::readData);
}
SerialHandler::~SerialHandler(){
    if(m_serialPort->isOpen())
        m_serialPort->close();
}

void SerialHandler::connectToRadio(const QString &portName){
    if (m_serialPort->isOpen())
        m_serialPort->close();

    // Configure the exact hardware physics to match the STM32's USART2 settings
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
void SerialHandler::disconnectRadio(){
    if(m_serialPort->isOpen()){
        m_serialPort->close();
        emit connectionStatusChanged(false, "Disconnected");
    }
}

void SerialHandler::readData(){
    m_serialBuffer.append(m_serialPort->readAll());

    while (m_serialBuffer.contains('\n')){
        // Find where the newline is, extract that chunk, and remove it from the buffer
        int newlineIndex = m_serialBuffer.indexOf('\n');
        QByteArray rawLine = m_serialBuffer.left(newlineIndex);
        m_serialBuffer.remove(0, newlineIndex + 1);

        // Clean up invisible carriage returns (\r)
        QString cleanLine = QString::fromUtf8(rawLine).trimmed();
        if (cleanLine.isEmpty()) continue;

        // 3. The Parsing Engine
        // Assuming the STM32 sends strings like: "RSSI:-85,BER:0.02"
        int parsedRssi = 0;
        float parsedBer = 0.0f;
        bool validData = false;

        // Split the string by the comma
        const QList<QString> parts = cleanLine.split(',');
        for (const QString &part : parts) {
            if (part.contains("RSSI:")) {
                parsedRssi = part.section(':', 1, 1).toInt();
                validData = true;
            } else if (part.contains("BER:")) {
                parsedBer = part.section(':', 1, 1).toFloat();
                validData = true;
            }
        }

        // 4. Blast the extracted numbers to the QML Graphics Engine
        if (validData) {
            emit radioDataReceived(parsedRssi, parsedBer);
        }
    }
}