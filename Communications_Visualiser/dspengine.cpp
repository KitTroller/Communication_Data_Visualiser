#include "dspengine.h"
#include <cmath>
#include <complex>
#include <vector>
#include <algorithm>
#include <QTimer>

// arc4random()/arc4random_uniform() are BSD/macOS functions and are absent on
// Windows (MinGW). Provide a portable Mersenne-Twister-backed shim there.
#if defined(_WIN32)
#include <cstdint>
#include <cstring>
#include <random>
static inline uint32_t arc4random() {
    static thread_local std::mt19937 gen{std::random_device{}()};
    return gen();
}
static inline uint32_t arc4random_uniform(uint32_t upper_bound) {
    return upper_bound ? (arc4random() % upper_bound) : 0u;
}
// strsep() is also a BSD function missing on Windows; liquid's logging.c needs it.
extern "C" char *strsep(char **stringp, const char *delim) {
    char *start = *stringp;
    if (!start) return nullptr;
    char *p = std::strpbrk(start, delim);
    if (p) { *p = '\0'; *stringp = p + 1; }
    else   { *stringp = nullptr; }
    return start;
}
#endif

// Build the M lowest-energy points of the hexagonal (A2) lattice -- the classic
// "hexagonal QAM" constellation. The table is centered (zero-mean) and scaled to
// unit average symbol energy, so the AWGN/SNR math stays identical to liquid-dsp's
// built-in unit-energy QAM/PSK modems. M must be a power of 2.
static std::vector<std::complex<float>> buildHexConstellation(unsigned int M)
{
    // A2 lattice basis, unit minimum distance: e1 = (1, 0), e2 = (1/2, sqrt(3)/2)
    const float rt3_2 = std::sqrt(3.0f) / 2.0f;
    const int R = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(M)))) + 2;

    std::vector<std::complex<float>> pts;
    pts.reserve(static_cast<size_t>(2 * R + 1) * (2 * R + 1));
    for (int a = -R; a <= R; ++a)
        for (int b = -R; b <= R; ++b)
            pts.emplace_back(static_cast<float>(a) + 0.5f * b, rt3_2 * b);

    // Keep the M points nearest the origin (circular boundary); break energy ties
    // by angle so the result is deterministic and centrally symmetric.
    std::sort(pts.begin(), pts.end(),
              [](const std::complex<float> &p, const std::complex<float> &q) {
                  const float ep = std::norm(p), eq = std::norm(q);
                  if (std::fabs(ep - eq) > 1e-4f) return ep < eq;
                  return std::atan2(p.imag(), p.real()) < std::atan2(q.imag(), q.real());
              });
    pts.resize(M);

    // Center, then normalize to unit average energy: (1/M) * sum|s|^2 = 1.
    std::complex<float> mean(0.0f, 0.0f);
    for (const auto &p : pts) mean += p;
    mean /= static_cast<float>(M);
    for (auto &p : pts) p -= mean;

    float energy = 0.0f;
    for (const auto &p : pts) energy += std::norm(p);
    const float scale = std::sqrt(static_cast<float>(M) / energy);
    for (auto &p : pts) p *= scale;

    return pts;
}

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
    bool useHex = false; // cases 4/5/6 build a true hexagonal (A2-lattice) constellation

    // Assignment Modulations List:
    switch(m_modType) {
    case 0: scheme = LIQUID_MODEM_ASK4; m_mSize = 4; m_bitsPerSymbol = 2; m_modName = "4-ASK"; break;
    case 1: scheme = LIQUID_MODEM_ASK8; m_mSize = 8; m_bitsPerSymbol = 3; m_modName = "8-ASK"; break;
    case 2: scheme = LIQUID_MODEM_PSK2; m_mSize = 2; m_bitsPerSymbol = 1; m_modName = "BPSK"; break;
    case 3: scheme = LIQUID_MODEM_PSK4; m_mSize = 4; m_bitsPerSymbol = 2; m_modName = "QPSK"; break;
    // True Hexagonal QAM: points sit on the hexagonal (A2) lattice instead of a
    // square grid, making them visually distinct from the *-QAM schemes below.
    case 4: useHex = true; m_mSize = 4;  m_bitsPerSymbol = 2; m_modName = "4-HQAM";  break;
    case 5: useHex = true; m_mSize = 16; m_bitsPerSymbol = 4; m_modName = "16-HQAM"; break;
    case 6: useHex = true; m_mSize = 64; m_bitsPerSymbol = 6; m_modName = "64-HQAM"; break;
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

    if (useHex) {
        // Arbitrary-constellation modem fed our normalized hexagonal table. The
        // member keeps the table alive for the lifetime of the modem.
        m_hexTable = buildHexConstellation(m_mSize);
        m_modem = modemcf_create_arbitrary(
            reinterpret_cast<liquid_float_complex *>(m_hexTable.data()), m_mSize);
    } else {
        m_modem = modemcf_create(scheme);
    }

    if (m_interp) firinterp_crcf_destroy(m_interp);
    m_interp = firinterp_crcf_create_prototype(LIQUID_FIRFILT_RRC, 4, 3, m_rollOff, 0.0f);
}

unsigned int DspWorker::countSetBits(unsigned int n) {
    unsigned int count = 0;
    while (n) { count += n & 1; n >>= 1; }
    return count;
}
// One standard-normal sample via Box-Muller. u1 is shifted into (0,1] so the
// logarithm can never hit zero.
float DspWorker::randn() {
    const float u1 = (arc4random() + 1.0f) / 4294967296.0f; // (0, 1]
    const float u2 = (float)arc4random() / 4294967295.0f;   // [0, 1)
    return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * (float)M_PI * u2);
}

// Advance the "slow" impairment states once per transmitted symbol.
void DspWorker::advanceImpairmentStates() {
    // Phase noise as an Ornstein-Uhlenbeck (bounded random-walk) process: the
    // free-running oscillator integrates Gaussian phase increments (a Wiener
    // process), while a first-order carrier-tracking loop pulls the phase back
    // toward zero. The stationary RMS phase equals sigma_theta; alpha is the
    // normalized loop bandwidth (phase memory ~ 1/alpha symbols).
    const float sigma_theta = m_phaseNoiseLevel * 0.06f;            // 0, .06, .12, .18 rad
    const float alpha       = 0.02f;
    const float sigma_step  = sigma_theta * sqrtf(alpha * (2.0f - alpha));
    m_pnPhase = (1.0f - alpha) * m_pnPhase + sigma_step * randn();

    // Coherent continuous-wave interferer: a tone rotating at a fixed normalized
    // offset frequency (cycles per symbol).
    const float jamOffset = 0.013f;
    m_jamPhase += 2.0f * (float)M_PI * jamOffset;
    if (m_jamPhase > 2.0f * (float)M_PI) m_jamPhase -= 2.0f * (float)M_PI;
}

// Apply the channel/receiver impairment chain to one complex sample.
// Order: TX I/Q imbalance -> phase-noise rotation -> CW jammer -> AWGN.
void DspWorker::applyImpairments(float i_in, float q_in, float& i_out, float& q_out, float sigma) {
    // (1) I/Q imbalance: the quadrature branch carries a gain mismatch g and a
    //     phase-orthogonality error phi relative to the in-phase reference.
    const float g   = 1.0f + m_iqImbalanceLevel * 0.06f;
    const float phi = m_iqImbalanceLevel * 0.06f;
    const float a_i = i_in;
    const float a_q = g * (q_in * cosf(phi) - i_in * sinf(phi));

    // (2) Phase noise: rotate by the current accumulated phase state.
    const float c = cosf(m_pnPhase), s = sinf(m_pnPhase);
    float p_i = a_i * c - a_q * s;
    float p_q = a_i * s + a_q * c;

    // (3) Coherent CW interferer (jammer).
    const float jamAmp = m_interferenceLevel * 0.18f;
    p_i += jamAmp * cosf(m_jamPhase);
    p_q += jamAmp * sinf(m_jamPhase);

    // (4) AWGN: circularly-symmetric complex Gaussian, std-dev sigma per axis.
    i_out = p_i + sigma * randn();
    q_out = p_q + sigma * randn();
}


void DspWorker::process() {
    // THE OVERDRIVE LOOP: Runs 1x in Live Mode, 50x in Dataset Mode
    for (int batch = 0; batch < m_batchSize; batch++) {
        float noisy_i = 0.0f, noisy_q = 0.0f;
        unsigned int bit_errors = 0;
        // AWGN std-dev per real dimension for Es/N0 = m_snr dB on unit-energy
        // symbols: sigma = 10^(-SNR/20)/sqrt(2)  =>  Es/N0 = 1/(2*sigma^2).
        float sigma = powf(10.0f, -m_snr / 20.0f) * 0.70710678f;
        advanceImpairmentStates();

        if (m_modem) {
            // --- BASEBAND MODULATION ---
            unsigned int sym_in = arc4random_uniform(m_mSize);
            std::complex<float> iq_tx;
            modemcf_modulate(m_modem, sym_in, reinterpret_cast<liquid_float_complex*>(&iq_tx));

            // --- FIX 1: We use the helper function here for the main dot ---
            applyImpairments(iq_tx.real(), iq_tx.imag(), noisy_i, noisy_q, sigma);

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
                    applyImpairments(buffer_tx[i].real, buffer_tx[i].imag, vis_i, vis_q, sigma);

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
    setBatchSize(200); // OVERDRIVE: 200 symbols/tick so a full cloud builds in ~0.25 s

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
    // Let the Canvas accumulate ~3,300 dots (200/tick x ~16 ticks) before the grab.
    // ~4x faster than the old 1 s dwell while keeping the same dot density.
    QTimer::singleShot(250, this, &DspEngine::prepareScreenshot);
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
    QDir dir(desktopPath);
    if (!dir.exists("RF_Dataset"))
        dir.mkdir("RF_Dataset");

    QString csvPath = desktopPath + "/RF_Dataset/sep_curves.csv";
    QFile file(csvPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "ERROR: Failed to open CSV file!";
        return;
    }
    QTextStream out(&file);
    out << "Modulation,SNR_dB,PhaseNoise_Level,SymbolErrorProbability\n";

    int qamModulations[] = {7, 9, 11};            // 16-QAM, 64-QAM, 256-QAM
    QString qamNames[] = {"16-QAM", "64-QAM", "256-QAM"};
    int phaseNoiseConditions[] = {0, 3};          // None vs High phase noise
    QString phaseNames[] = {"None", "High"};

    // Remember the operator's settings so we can restore them afterwards.
    const int   savedMod = m_modType;          const float savedSnr = m_snr;
    const bool  savedRrc = m_useRrc;           const int   savedPhase = m_phaseNoiseLevel;
    const int   savedIq  = m_iqImbalanceLevel; const int   savedJam = m_interferenceLevel;
    const int   savedBatch = m_batchSize;

    bool wasRunning = m_timer->isActive();
    if (wasRunning) m_timer->stop();
    m_worker->setMuteSignals(true);

    // Push parameters to the worker SYNCHRONOUSLY (DirectConnection) so there is
    // no queued-update race against the DirectConnection process() calls below.
    auto pushParams = [&](int modType, float snr, int phase) {
        m_modType = modType; m_snr = snr; m_useRrc = false;
        m_phaseNoiseLevel = phase; m_iqImbalanceLevel = 0; m_interferenceLevel = 0;
        m_batchSize = 1;
        QMetaObject::invokeMethod(m_worker, "updateParameters", Qt::DirectConnection,
                                  Q_ARG(float, m_snr), Q_ARG(int, m_modType),
                                  Q_ARG(bool, m_useRrc), Q_ARG(float, m_rollOff),
                                  Q_ARG(int, m_phaseNoiseLevel), Q_ARG(int, m_iqImbalanceLevel),
                                  Q_ARG(int, m_interferenceLevel), Q_ARG(int, m_batchSize));
    };

    for (int p = 0; p < 2; p++) {
        for (int m = 0; m < 3; m++) {
            for (int snr_test = 0; snr_test <= 30; snr_test += 2) {
                pushParams(qamModulations[m], (float)snr_test, phaseNoiseConditions[p]);
                m_worker->resetStats();

                // 100,000 symbols per point (batchSize forced to 1 above).
                for (int sim = 0; sim < 100000; sim++)
                    QMetaObject::invokeMethod(m_worker, "process", Qt::DirectConnection);

                long errSyms = m_worker->getErrorSymbols();
                long totSyms = m_worker->getTotalSymbols();
                float sep = totSyms > 0 ? (float)errSyms / (float)totSyms : 0.0f;
                out << qamNames[m] << "," << snr_test << "," << phaseNames[p] << ","
                    << QString::number(sep, 'e', 6) << "\n";
            }
        }
    }

    m_worker->setMuteSignals(false);
    file.close();

    // Restore the operator's settings and resync the worker normally.
    m_modType = savedMod; m_snr = savedSnr; m_useRrc = savedRrc;
    m_phaseNoiseLevel = savedPhase; m_iqImbalanceLevel = savedIq;
    m_interferenceLevel = savedJam; m_batchSize = savedBatch;
    syncParameters();
    emit modTypeChanged(); emit snrChanged(); emit useRrcChanged();
    emit phaseNoiseLevelChanged(); emit iqImbalanceLevelChanged(); emit interferenceLevelChanged();

    qDebug() << "SEP SIMULATION COMPLETE! Saved to:" << csvPath;
    if (wasRunning) m_timer->start(m_timerInterval);
}