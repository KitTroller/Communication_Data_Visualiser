#include "dspengine.h"
#include <cmath>
#include <complex>
#include <QTimer>

// --- WORKER IMPLEMENTATION ---

DspWorker::DspWorker(QObject *parent) : QObject(parent) {
    rebuildModem();
}

DspWorker::~DspWorker() {
    if (m_modem) modemcf_destroy(m_modem);
    if (m_interp) firinterp_crcf_destroy(m_interp);
}

void DspWorker::updateParameters(float snr, int modType, bool useRrc, float rollOff, int phaseNoise, int iqImbalance, int interference, int batchSize) {
    m_snr = snr; m_useRrc = useRrc;
    m_phaseNoiseLevel = phaseNoise; m_iqImbalanceLevel = iqImbalance; m_interferenceLevel = interference;
    m_batchSize = batchSize; // SAVE THE GEAR
    bool rebuild = false;
    if (m_modType != modType) { m_modType = modType; rebuild = true; }
    if (m_rollOff != rollOff) { m_rollOff = rollOff; rebuild = true; }
    if (rebuild) rebuildModem();
}

void DspWorker::rebuildModem() {
    if (m_modem) modemcf_destroy(m_modem);
    m_modem = nullptr;
    m_errorBits = 0; m_totalBits = 0;

    modulation_scheme scheme = LIQUID_MODEM_QAM16;

    // Assignment Modulations List:
    switch(m_modType) {
    case 0: scheme = LIQUID_MODEM_ASK4; m_mSize = 4; m_bitsPerSymbol = 2; m_modName = "4-ASK"; break;
    case 1: scheme = LIQUID_MODEM_ASK8; m_mSize = 8; m_bitsPerSymbol = 3; m_modName = "8-ASK"; break;
    case 2: scheme = LIQUID_MODEM_PSK2; m_mSize = 2; m_bitsPerSymbol = 1; m_modName = "BPSK"; break;
    case 3: scheme = LIQUID_MODEM_PSK4; m_mSize = 4; m_bitsPerSymbol = 2; m_modName = "QPSK"; break;
    // Note: liquid-dsp handles HQAM (Hierarchical/Cross) naturally for odd-bit QAMs, we map them directly
    case 4: scheme = LIQUID_MODEM_QAM4; m_mSize = 4; m_bitsPerSymbol = 2; m_modName = "4-HQAM"; break;
    case 5: scheme = LIQUID_MODEM_QAM16; m_mSize = 16; m_bitsPerSymbol = 4; m_modName = "16-HQAM"; break;
    case 6: scheme = LIQUID_MODEM_QAM64; m_mSize = 64; m_bitsPerSymbol = 6; m_modName = "64-HQAM"; break;
    case 7: scheme = LIQUID_MODEM_QAM16; m_mSize = 16; m_bitsPerSymbol = 4; m_modName = "16-QAM"; break;
    case 8: scheme = LIQUID_MODEM_QAM32; m_mSize = 32; m_bitsPerSymbol = 5; m_modName = "32-QAM"; break;
    case 9: scheme = LIQUID_MODEM_QAM64; m_mSize = 64; m_bitsPerSymbol = 6; m_modName = "64-QAM"; break;
    case 10: scheme = LIQUID_MODEM_QAM128; m_mSize = 128; m_bitsPerSymbol = 7; m_modName = "128-QAM"; break;
    case 11: scheme = LIQUID_MODEM_QAM256; m_mSize = 256; m_bitsPerSymbol = 8; m_modName = "256-QAM"; break;
    case 12: scheme = LIQUID_MODEM_APSK16; m_mSize = 16; m_bitsPerSymbol = 4; m_modName = "16-APSK"; break;
    case 13: scheme = LIQUID_MODEM_APSK32; m_mSize = 32; m_bitsPerSymbol = 5; m_modName = "32-APSK"; break;
    case 14: scheme = LIQUID_MODEM_APSK64; m_mSize = 64; m_bitsPerSymbol = 6; m_modName = "64-APSK"; break;
    case 15: scheme = LIQUID_MODEM_APSK128; m_mSize = 128; m_bitsPerSymbol = 7; m_modName = "128-APSK"; break;
    default: scheme = LIQUID_MODEM_QAM16; m_mSize = 16; m_bitsPerSymbol = 4; m_modName = "16-QAM"; break;
    }

    m_modem = modemcf_create(scheme);
    if (m_interp) firinterp_crcf_destroy(m_interp);
    m_interp = firinterp_crcf_create_prototype(LIQUID_FIRFILT_RRC, 4, 3, m_rollOff, 0.0f);
}

unsigned int DspWorker::countSetBits(unsigned int n) {
    unsigned int count = 0;
    while (n) { count += n & 1; n >>= 1; }
    return count;
}
void DspWorker::applyImpairments(float i_in, float q_in, float& i_out, float& q_out, float noise_power) {
    // 1. I/Q IMBALANCE
    float epsilon = m_iqImbalanceLevel * 0.08f;
    float delta_phi = m_iqImbalanceLevel * 0.0872f;
    float imb_i = i_in * (1.0f + epsilon) + q_in * sinf(delta_phi);
    float imb_q = q_in * cosf(delta_phi);

    // 2. PHASE NOISE (Wrapped Gaussian)
    float phase_std_dev = m_phaseNoiseLevel * 0.08f;
    float u1 = ((float)arc4random() / 4294967295.0f);
    float u2 = ((float)arc4random() / 4294967295.0f);
    float z0 = sqrtf(-2.0f * logf(u1)) * cosf(2.0f * M_PI * u2);
    float phase_error = z0 * phase_std_dev;

    float pn_i = imb_i * cosf(phase_error) - imb_q * sinf(phase_error);
    float pn_q = imb_i * sinf(phase_error) + imb_q * cosf(phase_error);

    // 3. EXTERNAL INTERFERENCE (Jamming)
    float jam_power = m_interferenceLevel * 0.2f;
    float jam_phase = ((float)arc4random() / 4294967295.0f) * 2.0f * M_PI;
    pn_i += jam_power * cosf(jam_phase);
    pn_q += jam_power * sinf(jam_phase);

    // 4. AWGN NOISE (Gaussian)
    float u1_n = ((float)arc4random() / 4294967295.0f);
    float u2_n = ((float)arc4random() / 4294967295.0f);
    float gn_i = sqrtf(-2.0f * logf(u1_n)) * cosf(2.0f * M_PI * u2_n);
    float gn_q = sqrtf(-2.0f * logf(u1_n)) * sinf(2.0f * M_PI * u2_n);

    i_out = pn_i + (noise_power * gn_i);
    q_out = pn_q + (noise_power * gn_q);
}


void DspWorker::process() {
    // THE OVERDRIVE LOOP: Runs 1x in Live Mode, 50x in Dataset Mode
    for (int batch = 0; batch < m_batchSize; batch++) {
        float noisy_i = 0.0f, noisy_q = 0.0f;
        unsigned int bit_errors = 0;
        float noise_power = powf(10.0f, -m_snr / 20.0f);

        if (m_modem) {
            // --- BASEBAND MODULATION ---
            unsigned int sym_in = arc4random_uniform(m_mSize);
            std::complex<float> iq_tx;
            modemcf_modulate(m_modem, sym_in, reinterpret_cast<liquid_float_complex*>(&iq_tx));

            // --- FIX 1: We use the helper function here for the main dot ---
            applyImpairments(iq_tx.real(), iq_tx.imag(), noisy_i, noisy_q, noise_power);

            // Let the demodulator guess (for BER)
            unsigned int sym_out;
            liquid_float_complex iq_rx = {noisy_i, noisy_q};
            modemcf_demodulate(m_modem, iq_rx, &sym_out);
            bit_errors = countSetBits(sym_in ^ sym_out);

            // Track Symbol Errors for the SEP calculation
            if (sym_in != sym_out) m_errorSymbols++;
            m_totalSymbols++;
            m_totalBits += m_bitsPerSymbol;
            bool is_error = (bit_errors > 0);

            if (m_useRrc && m_interp) {
                liquid_float_complex buffer_tx[4];
                firinterp_crcf_execute(m_interp, {iq_tx.real(), iq_tx.imag()}, buffer_tx);
                for (int i = 0; i < 4; i++) {
                    float vis_i, vis_q;

                    // --- FIX 2: We use the helper function here for the RRC trajectory line! ---
                    applyImpairments(buffer_tx[i].real, buffer_tx[i].imag, vis_i, vis_q, noise_power);

                    // --- FIX 3: Mute the UI emission if we are running the 10-million symbol SEP loop ---
                    if (!m_muteSignals) {
                        emit newConstellationData(vis_i, vis_q, is_error, (i == 0));
                    }
                }
            }
        }

        m_errorBits += bit_errors;

        // --- FIX 4: Removed the `|| m_modType == 4` ghost code so HQAM doesn't double-draw ---
        if (!m_useRrc) {
            if (!m_muteSignals) {
                emit newConstellationData(noisy_i, noisy_q, bit_errors > 0, true);
            }
        }
    }

    // TELEMETRY UPDATES
    m_frameCounter++;
    if (m_frameCounter >= 6) {
        m_frameCounter = 0;
        float sim_rssi = -100.0f + m_snr + (((float)arc4random() / 4294967295.0f) * 1.5f - 0.75f);
        float sim_snr = m_snr + (((float)arc4random() / 4294967295.0f) * 0.8f - 0.4f);
        float calculated_ber = m_totalBits > 0 ? ((float)m_errorBits / m_totalBits) * 100.0f : 0.0f;
        float sim_plr = calculated_ber > 5.0f ? calculated_ber * 1.5f : 0.0f;

        // Mute telemetry to save CPU during SEP math
        if (!m_muteSignals) {
            emit simulatedTelemetry(sim_rssi, calculated_ber, sim_snr, sim_plr);
        }
        if (m_totalBits > 5000) { m_totalBits = 0; m_errorBits = 0; }
    }

    // PACKET LOG UPDATES
    m_packetTimer++;
    if (m_packetTimer >= 60) {
        m_packetTimer = 0;
        QString txStr = "ARISTURTLE LINK: TELEMETRY NOMINAL";
        QString rxStr = "";
        bool hasError = false;
        int timeMs = (272 / m_bitsPerSymbol) * 2;
        float current_ber = m_totalBits > 0 ? ((float)m_errorBits / m_totalBits) : 0.0f;

        for (int i = 0; i < txStr.length(); i++) {
            QChar c = txStr[i];
            if (((float)arc4random() / 4294967295.0f) < (current_ber * 5.0f)) {
                c = QChar(c.unicode() + (arc4random() % 5 + 1));
                hasError = true;
            }
            rxStr += c;
        }
        // Mute packet log to save CPU during SEP math
        if (!m_muteSignals) {
            emit packetReceived(m_modName, m_bitsPerSymbol, timeMs, txStr, rxStr, hasError);
        }
    }
}
// --- ENGINE IMPLEMENTATION ---

DspEngine::DspEngine(QObject *parent) : QObject(parent), m_worker(new DspWorker()), m_timer(new QTimer(this)) {
    m_worker->moveToThread(&m_workerThread);

    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_timer, &QTimer::timeout, m_worker, &DspWorker::process);
    
    // Relay signals from worker to engine
    connect(m_worker, &DspWorker::newConstellationData, this, &DspEngine::newConstellationData);
    connect(m_worker, &DspWorker::simulatedTelemetry, this, &DspEngine::simulatedTelemetry);
    connect(m_worker, &DspWorker::packetReceived, this, &DspEngine::packetReceived);

    m_workerThread.start();
}

DspEngine::~DspEngine() {
    m_workerThread.quit();
    m_workerThread.wait();
}

void DspEngine::syncParameters() {
    QMetaObject::invokeMethod(m_worker, "updateParameters",
                              Q_ARG(float, m_snr), Q_ARG(int, m_modType),
                              Q_ARG(bool, m_useRrc), Q_ARG(float, m_rollOff),
                              Q_ARG(int, m_phaseNoiseLevel), Q_ARG(int, m_iqImbalanceLevel), Q_ARG(int, m_interferenceLevel), Q_ARG(int, m_batchSize));

}

void DspEngine::setBatchSize(int size) {    //  Passes batch size
    if (m_batchSize != size) { m_batchSize = size; syncParameters(); }
}

void DspEngine::setSnr(float snr) { if (m_snr != snr) { m_snr = snr; syncParameters(); emit snrChanged(); } }
void DspEngine::setModType(int type) { if (m_modType != type) { m_modType = type; syncParameters(); emit modTypeChanged(); } }
void DspEngine::setUseRrc(bool rrc) { if (m_useRrc != rrc) { m_useRrc = rrc; syncParameters(); emit useRrcChanged(); } }
void DspEngine::setGridOpacity(float opacity) { if (m_gridOpacity != opacity) { m_gridOpacity = opacity; emit gridOpacityChanged(); } }

void DspEngine::startSimulation() { m_timer->start(m_timerInterval); }
void DspEngine::stopSimulation() { m_timer->stop(); }


void DspEngine::setRollOff(float ro) {
    if (m_rollOff != ro) {
        m_rollOff = ro;
        syncParameters();
        emit rollOffChanged();
    }
}

void DspEngine::setTimerInterval(int ti) {
    if (m_timerInterval != ti) {
        m_timerInterval = ti;
        if (m_timer->isActive()) { m_timer->setInterval(m_timerInterval); }
        emit timerIntervalChanged();
    }
}

// --- EXTRA NOISE SETTERS ---

void DspEngine::setPhaseNoiseLevel(int level) {
    if (m_phaseNoiseLevel != level) {
        m_phaseNoiseLevel = level;
        syncParameters();
        emit phaseNoiseLevelChanged();
    }
}

void DspEngine::setIqImbalanceLevel(int level) {
    if (m_iqImbalanceLevel != level) {
        m_iqImbalanceLevel = level;
        syncParameters();
        emit iqImbalanceLevelChanged();
    }
}

void DspEngine::setInterferenceLevel(int level) {
    if (m_interferenceLevel != level) {
        m_interferenceLevel = level;
        syncParameters();
        emit interferenceLevelChanged();
    }
}

// --- DATASET AUTOMATOR ---

void DspEngine::startDatasetGeneration() {
    if (m_isAutomating) return;

    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QDir dir(desktopPath);
    if (!dir.exists("RF_Dataset")) dir.mkdir("RF_Dataset");

    QString csvPath = desktopPath + "/RF_Dataset/dataset_labels.csv";
    m_csvFile = new QFile(csvPath);
    if (m_csvFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_csvStream = new QTextStream(m_csvFile);
        *m_csvStream << "filename,modulation,phase_noise,iq_imbalance,interference,snr_range\n";
    }

    m_isAutomating = true;
    emit isAutomatingChanged();
    m_imageCounter = 0;
    m_autoModIdx = 0; m_autoSnrIdx = 0; m_autoPhaseIdx = 0; m_autoIqIdx = 0; m_autoJamIdx = 0;

    setUseRrc(false);
    setBatchSize(50); // SHIFT INTO OVERDRIVE (Generates ~3,000 pts/sec)

    advanceDatasetLoop();
}

void DspEngine::advanceDatasetLoop() {
    if (!m_isAutomating) return;

    // Include ALL 16 modulations required by the assignment
    int modulations[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    float snrLevels[] = {10.0f, 20.0f, 30.0f};
    QString snrLabels[] = {"Low", "Medium", "High"};
    QString severity[] = {"None", "Low", "Medium", "High"};

    // State Machine Iterator
    if (m_autoJamIdx > 3) { m_autoJamIdx = 0; m_autoIqIdx++; }
    if (m_autoIqIdx > 3) { m_autoIqIdx = 0; m_autoPhaseIdx++; }
    if (m_autoPhaseIdx > 3) { m_autoPhaseIdx = 0; m_autoSnrIdx++; }
    if (m_autoSnrIdx > 2) { m_autoSnrIdx = 0; m_autoModIdx++; }

    // THE FIX: Wait until all 16 modulations are complete (Index 16)
    if (m_autoModIdx >= 16) {
        m_isAutomating = false;
        emit isAutomatingChanged();
        setBatchSize(1);
        if (m_csvFile) { m_csvFile->close(); delete m_csvFile; m_csvFile = nullptr; }
        qDebug() << "DATASET GENERATION COMPLETE!";
        return;
    }

    // Apply Parameters with SNR Jitter for AI Generalization
    float jitter = (((float)arc4random() / 4294967295.0f) * 2.0f) - 1.0f; // Random between -1.0 and +1.0 dB
    setModType(modulations[m_autoModIdx]);
    setSnr(snrLevels[m_autoSnrIdx] + jitter);
    setPhaseNoiseLevel(m_autoPhaseIdx);
    setIqImbalanceLevel(m_autoIqIdx);
    setInterferenceLevel(m_autoJamIdx);

    // Explicitly clear canvas via signal (or via setModType which should trigger it)
    // We already have a clearCanvas tied to modTypeChanged in QML, but let's be safe.

    QString filename = QString("img_%1.png").arg(m_imageCounter, 5, 10, QChar('0'));

    QString modName;
    switch(modulations[m_autoModIdx]) {
    case 0: modName = "4-ASK"; break;
    case 1: modName = "8-ASK"; break;
    case 2: modName = "BPSK"; break;
    case 3: modName = "QPSK"; break;
    case 4: modName = "4-HQAM"; break;
    case 5: modName = "16-HQAM"; break;
    case 6: modName = "64-HQAM"; break;
    case 7: modName = "16-QAM"; break;
    case 8: modName = "32-QAM"; break;
    case 9: modName = "64-QAM"; break;
    case 10: modName = "128-QAM"; break;
    case 11: modName = "256-QAM"; break;
    case 12: modName = "16-APSK"; break;
    case 13: modName = "32-APSK"; break;
    case 14: modName = "64-APSK"; break;
    case 15: modName = "128-APSK"; break;
    }

    m_currentCsvLine = QString("%1,%2,%3,%4,%5,%6\n")
                           .arg(filename, modName, severity[m_autoPhaseIdx], severity[m_autoIqIdx], severity[m_autoJamIdx], snrLabels[m_autoSnrIdx]);
    // Give Canvas exactly 1 second to draw 3,000 dots before snapping the photo
    QTimer::singleShot(1000, this, &DspEngine::prepareScreenshot);
}

void DspEngine::prepareScreenshot() {
    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QString fullPath = desktopPath + "/RF_Dataset/" + QString("img_%1.png").arg(m_imageCounter, 5, 10, QChar('0'));
    emit triggerScreenshot(fullPath);
}

void DspEngine::confirmImageSaved() {
    if (m_csvStream) { *m_csvStream << m_currentCsvLine; m_csvStream->flush(); }
    m_imageCounter++;
    m_autoJamIdx++;
    advanceDatasetLoop(); // Loop to next configuration
}

void DspEngine::generateSEPCurves() {
    qDebug() << "STARTING SEP vs SNR SIMULATION...";


    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    // --- THE FIX: Ensure the folder exists BEFORE trying to open the file! ---
    QDir dir(desktopPath);
    if (!dir.exists("RF_Dataset")) {
        dir.mkdir("RF_Dataset");
    }
    QString csvPath = desktopPath + "/RF_Dataset/sep_curves.csv";
    QFile file(csvPath);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "ERROR: Failed to open CSV file!";
        return;
    };
    QTextStream out(&file);
    out << "Modulation,SNR_dB,PhaseNoise_Level,SymbolErrorProbability\n";

    // The required QAM orders
    int qamModulations[] = {7, 9, 11}; // 16-QAM, 64-QAM, 256-QAM
    QString qamNames[] = {"16-QAM", "64-QAM", "256-QAM"};

    // We will test two conditions: No Phase Noise vs High Phase Noise
    int phaseNoiseConditions[] = {0, 3};
    QString phaseNames[] = {"None", "High"};

    // Stop the UI timer so it doesn't interfere
    bool wasRunning = m_timer->isActive();
    if (wasRunning) m_timer->stop();

    // --- MUTE THE UI SIGNALS TO PREVENT FREEZE ---
    m_worker->setMuteSignals(true);

    // The heavy math loop (Runs instantly because we don't wait for UI rendering)
    for (int p = 0; p < 2; p++) {
        setPhaseNoiseLevel(phaseNoiseConditions[p]);
        setIqImbalanceLevel(0);
        setInterferenceLevel(0);
        setUseRrc(false);

        for (int m = 0; m < 3; m++) {
            setModType(qamModulations[m]);

            // Sweep SNR from 0 to 30 dB
            for (int snr_test = 0; snr_test <= 30; snr_test += 2) {
                setSnr((float)snr_test);

                m_worker->resetStats(); // Wipe the slate clean

                // Simulate 100,000 symbols per SNR step for high statistical accuracy
                for (int sim = 0; sim < 100000; sim++) {
                    QMetaObject::invokeMethod(m_worker, "process", Qt::DirectConnection);
                }

                // Extract the exact Symbol Error Probability
                long errSyms = m_worker->getErrorSymbols();
                long totSyms = m_worker->getTotalSymbols();
                float sep = totSyms > 0 ? (float)errSyms / (float)totSyms : 0.0f;

                // Write to the CSV
                out << qamNames[m] << "," << snr_test << "," << phaseNames[p] << "," << QString::number(sep, 'e', 6) << "\n";
            }
        }
    }
    // --- TURN SIGNALS BACK ON ---
    m_worker->setMuteSignals(false);

    file.close();
    qDebug() << "SEP SIMULATION COMPLETE! Saved to: " << csvPath;
    if (wasRunning) m_timer->start(m_timerInterval);
}