#ifndef DSPENGINE_H
#define DSPENGINE_H

#include <QObject>
#include <QTimer>
#include <QString>
#include <QtQml/qqml.h>
#include <liquid/liquid.h>

class DspEngine : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DspEngine)
    Q_PROPERTY(float snr READ snr WRITE setSnr NOTIFY snrChanged)
    Q_PROPERTY(int modType READ modType WRITE setModType NOTIFY modTypeChanged)

public:
    explicit DspEngine(QObject *parent = nullptr);
    ~DspEngine();

    float snr() const { return m_snr; }
    void setSnr(float snr);

    int modType() const { return m_modType; }
    void setModType(int type);

    Q_INVOKABLE void startSimulation();
    Q_INVOKABLE void stopSimulation();

signals:
    void snrChanged();
    void modTypeChanged();
    void newConstellationData(float i, float q, bool isError);
    // New Signals for the Full System Simulation
    void simulatedTelemetry(float rssi, float ber, float snr, float plr);
    void packetReceived(QString modName, int speed, int timeMs, QString tx, QString rx, bool hasError);

private slots:
    void processDspMath();

private:
    void rebuildModem();
    unsigned int countSetBits(unsigned int n);

    float m_snr = 30.0f;
    int m_modType = 1;
    unsigned int m_mSize = 16;
    unsigned int m_bitsPerSymbol = 4;

    QTimer *m_timer;
    modemcf m_modem = nullptr;

    // Simulation Tracking
    QString m_targetString = "ARISTURTLE GROUND STATION ACTIVE - TELEMETRY LINK ESTABLISHED *** ";
    int m_stringIndex = 0;
    QString m_corruptedBuffer = "";
    QString m_originalBuffer = "";

    // BER Math
    long m_totalBits = 0;
    long m_errorBits = 0;
    int m_frameCounter = 0;
    QString m_modName = "16-QAM";
    int m_packetTimer = 0;
};
#endif // DSPENGINE_H