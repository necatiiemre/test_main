#ifndef DTN_H
#define DTN_H

#include <string>
#include <vector>
#include "DeviceManager.h"
#include "Utils.h"
#include "SerialPort.h"
#include "Server.h"
#include "SystemCommand.h"

/**
 * @brief Log directory paths for different subsystems
 * @note Paths are relative to PROJECT_ROOT (defined at compile-time via CMake)
 */
#ifndef PROJECT_ROOT
    #define PROJECT_ROOT "."
#endif

namespace LogPaths {
    inline std::string baseDir()  { return std::string(PROJECT_ROOT) + "/LOGS"; }
    inline std::string CMC()      { return baseDir() + "/CMC"; }
    inline std::string VMC()      { return baseDir() + "/VMC"; }
    inline std::string MMC()      { return baseDir() + "/MMC"; }
    inline std::string DTN()      { return baseDir() + "/DTN"; }
    inline std::string HSN()      { return baseDir() + "/HSN"; }
}

class Dtn
{
private:
    /**
     * @brief Ensure LOGS directory structure exists
     * @return true on success
     */
    bool ensureLogDirectories();

    /**
     * @brief Ask user a yes/no question
     * @param question The question to ask
     * @return true if user answered yes, false otherwise
     */
    bool askQuestion(const std::string& question);

    // Dual-test system state
    bool m_loopbackTestCompleted = false;
    bool m_loopbackUsedDefault = false;
    bool m_unitTestCompleted = false;

public:
    Dtn();
    ~Dtn();

    /**
     * @brief Configure power supply sequence
     * @return true on success
     */
    bool configureSequence();

    /**
     * @brief Legacy function - calls completeLatencyTestSequence()
     * @return true on success
     */
    bool mellanoxLatencyTestSequence();

    // =========================================================================
    // DUAL-TEST LATENCY SYSTEM
    // =========================================================================

    /**
     * @brief Interactive loopback test sequence (optional)
     * Asks user if they want to run loopback test, checks cables
     * If skipped, uses default 14.0 us latency
     *
     * @return true if loopback test was run successfully
     */
    bool loopbackTestSequence();

    /**
     * @brief Unit test sequence (always runs)
     * Measures end-to-end latency through switch
     * Port mapping: 0↔1, 2↔3, 4↔5, 6↔7
     *
     * @return true on success
     */
    bool unitTestSequence();

    /**
     * @brief Complete latency test sequence
     * 1. Loopback Test (optional) - NIC latency measurement
     * 2. Unit Test (always) - End-to-end through switch
     * 3. Print combined results
     *
     * @return true on success
     */
    bool completeLatencyTestSequence();

    /**
     * @brief Print combined latency summary
     * Shows loopback, unit, and net latency status
     */
    void printCombinedLatencySummary();

    // =========================================================================

    /**
     * @brief Run Mellanox HW timestamp latency test on remote server
     *
     * Deploys mellanox_latency to server, runs it, and fetches the log back.
     * Log is saved to LOGS/DTN/mellanox_latency.log
     *
     * @param run_args Optional arguments for the test (e.g., "-n 10 -v", "--unit-mode")
     * @param timeout_seconds Timeout for test execution (default: 120 seconds)
     * @return true on success
     */
    bool runMellanoxLatencyTest(const std::string& run_args = "",
                                 int timeout_seconds = 120);

    // Getters for test state
    bool isLoopbackTestCompleted() const { return m_loopbackTestCompleted; }
    bool isLoopbackUsingDefault() const { return m_loopbackUsedDefault; }
    bool isUnitTestCompleted() const { return m_unitTestCompleted; }
};

extern Dtn g_dtn;

#endif // DTN_H
