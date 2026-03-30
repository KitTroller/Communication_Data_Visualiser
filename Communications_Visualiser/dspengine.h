#ifndef DSPENGINE_H
#define DSPENGINE_H

#include <QObject>
#include <QTimer>
#include <QString>
#include <QThread>
#include <QtQml/qqml.h>
#include <liquid/liquid.h>

// The Worker handles the heavy math in a background thread
class DspWorker : public QObject
{
    Q_OBJECT
public:
    explicit DspWorker(QObject *parent = nullptr);
    ~DspWorker();

public slots:
    void process();
    void updateParameters(float snr, int modType, bool useRrc, float rollOff);

signals:
    void newConstellationData(float i, float q, bool isError, bool isPeak);
    void simulatedTelemetry(float rssi, float ber, float snr, float plr);
    void packetReceived(QString modName, int speed, int timeMs, QString tx, QString rx, bool hasError);


private:
    void rebuildModem();
    unsigned int countSetBits(unsigned int n);

    float m_snr = 30.0f;
    int m_modType = 1;
    bool m_useRrc = false;

    modemcf m_modem = nullptr;
    firinterp_crcf m_interp = nullptr;
    unsigned int m_mSize = 16;
    unsigned int m_bitsPerSymbol = 4;
    QString m_modName = "16-QAM";

    long m_totalBits = 0;
    long m_errorBits = 0;
    int m_frameCounter = 0;
    int m_packetTimer = 0;

    float m_rollOff = 0.3f;
};

class DspEngine : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DspEngine)
    Q_PROPERTY(float snr READ snr WRITE setSnr NOTIFY snrChanged)
    Q_PROPERTY(int modType READ modType WRITE setModType NOTIFY modTypeChanged)
    Q_PROPERTY(float gridOpacity READ gridOpacity WRITE setGridOpacity NOTIFY gridOpacityChanged)
    Q_PROPERTY(bool useRrc READ useRrc WRITE setUseRrc NOTIFY useRrcChanged)

    Q_PROPERTY(float rollOff READ rollOff WRITE setRollOff NOTIFY rollOffChanged)
    Q_PROPERTY(int timerInterval READ timerInterval WRITE setTimerInterval NOTIFY timerIntervalChanged)

public:
    explicit DspEngine(QObject *parent = nullptr);
    ~DspEngine();

    float snr() const { return m_snr; }
    void setSnr(float snr);

    int modType() const { return m_modType; }
    void setModType(int type);

    float gridOpacity() const { return m_gridOpacity; }
    void setGridOpacity(float opacity);

    bool useRrc() const { return m_useRrc; }
    void setUseRrc(bool rrc);

    Q_INVOKABLE void startSimulation();
    Q_INVOKABLE void stopSimulation();

    float rollOff() const { return m_rollOff; }
    void setRollOff(float ro);

    int timerInterval() const { return m_timerInterval; }
    void setTimerInterval(int ti);

signals:
    void snrChanged();
    void modTypeChanged();
    void gridOpacityChanged();
    void useRrcChanged();

    // Proxied signals from Worker
    void newConstellationData(float i, float q, bool isError, bool isPeak);
    void simulatedTelemetry(float rssi, float ber, float snr, float plr);
    void packetReceived(QString modName, int speed, int timeMs, QString tx, QString rx, bool hasError);

    void rollOffChanged();
    void timerIntervalChanged();

private:
    float m_snr = 30.0f;
    int m_modType = 1;
    float m_gridOpacity = 0.15f;
    bool m_useRrc = false;

    QThread m_workerThread;
    DspWorker *m_worker;
    QTimer *m_timer;

    void syncParameters();

    float m_rollOff = 0.3f;
    int m_timerInterval = 15;
};

#endif // DSPENGINE_H
