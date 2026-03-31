#ifndef DSPENGINE_H
#define DSPENGINE_H

#include <QObject>
#include <QTimer>
#include <QString>
#include <QThread>
#include <QtQml/qqml.h>
#include <liquid/liquid.h>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QStandardPaths>

// The Worker handles the heavy math in a background thread
class DspWorker : public QObject
{
    Q_OBJECT
public:
    explicit DspWorker(QObject *parent = nullptr);
    ~DspWorker();

    // --- THE NEW FIXES ---
    void setMuteSignals(bool mute) { m_muteSignals = mute; }
    long getErrorSymbols() const { return m_errorSymbols; }
    long getTotalSymbols() const { return m_totalSymbols; }
    void resetStats() { m_errorBits = 0; m_totalBits = 0; m_errorSymbols = 0; m_totalSymbols = 0; }
public slots:
    void process();
    void updateParameters(float snr, int modType, bool useRrc, float rollOff, int phaseNoise, int iqImbalance, int interference, int batchSize); // new to support more noise

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

    // Added variables for extra noise controls
    int m_phaseNoiseLevel = 0;   // 0=None, 1=Low, 2=Medium, 3=High
    int m_iqImbalanceLevel = 0;
    int m_interferenceLevel = 0;

    int m_batchSize = 1;  // Variable to introduce creating data fast for later AI training

    // --- THE NEW HELPER ---
    void applyImpairments(float i_in, float q_in, float& i_out, float& q_out, float noise_power);
    bool m_muteSignals = false;
    long m_totalSymbols = 0;
    long m_errorSymbols = 0;


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

    //Added extra noise variables according to project requirements
    Q_PROPERTY(int phaseNoiseLevel READ phaseNoiseLevel WRITE setPhaseNoiseLevel NOTIFY phaseNoiseLevelChanged)
    Q_PROPERTY(int iqImbalanceLevel READ iqImbalanceLevel WRITE setIqImbalanceLevel NOTIFY iqImbalanceLevelChanged)
    Q_PROPERTY(int interferenceLevel READ interferenceLevel WRITE setInterferenceLevel NOTIFY interferenceLevelChanged)

    Q_PROPERTY(bool isAutomating READ isAutomating NOTIFY isAutomatingChanged)

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


    // Extra noise getters and setters
    int phaseNoiseLevel() const { return m_phaseNoiseLevel; }
    void setPhaseNoiseLevel(int level);

    int iqImbalanceLevel() const { return m_iqImbalanceLevel; }
    void setIqImbalanceLevel(int level);

    int interferenceLevel() const { return m_interferenceLevel; }
    void setInterferenceLevel(int level);

    // Functions for data generation
    Q_INVOKABLE void startDatasetGeneration();
    Q_INVOKABLE void confirmImageSaved();
    void setBatchSize(int size);

    //function to plot SNR
    Q_INVOKABLE void generateSEPCurves();

    bool isAutomating() const { return m_isAutomating; }

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

    // Signals to update extra noise levels

    void phaseNoiseLevelChanged();
    void iqImbalanceLevelChanged();
    void interferenceLevelChanged();

    void triggerScreenshot(QString filePath); // Function for saving AI training data

    void isAutomatingChanged();
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

    // Variables in memory for extra noise levels
    int m_phaseNoiseLevel = 0;   // 0=None, 1=Low, 2=Medium, 3=High
    int m_iqImbalanceLevel = 0;
    int m_interferenceLevel = 0;

    // Variables for saving file
    int m_batchSize = 1;
    void advanceDatasetLoop();
    void prepareScreenshot();

    bool m_isAutomating = false;
    int m_autoModIdx = 0;
    int m_autoSnrIdx = 0;
    int m_autoPhaseIdx = 0;
    int m_autoIqIdx = 0;
    int m_autoJamIdx = 0;

    QFile *m_csvFile = nullptr;
    QTextStream *m_csvStream = nullptr;
    int m_imageCounter = 0;
    QString m_currentCsvLine;

};

#endif // DSPENGINE_H
