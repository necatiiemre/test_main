#include "Dtn.h"
#include "SSHDeployer.h"
#include "CumulusHelper.h"
#include "SerialTimeForwarder.h"
#include <iostream>
#include <unistd.h>
#include <iomanip>

// PSU Configuration: 28V 3.0A

Dtn g_dtn;

Dtn::Dtn()
{
}

Dtn::~Dtn()
{
}

bool Dtn::configureSequence()
{
    // Create and connect PSU for DTN

    //g_Server.onWithWait(3);

    // // Create PSU G30 (30V, 56A)
    // if (!g_DeviceManager.create(PSUG30))
    // {
    //     std::cout << "DTN: Failed to create PSU G30!" << std::endl;
    //     return false;
    // }

    // // Connect to PSU G30
    // if (!g_DeviceManager.connect(PSUG30))
    // {
    //     std::cout << "DTN: Failed to connect to PSU G30!" << std::endl;
    //     return false;
    // }

    // if (!g_DeviceManager.setCurrent(PSUG30, 3.0))
    // {
    //     std::cout << "DTN: Failed to set current on PSU G30!" << std::endl;
    //     return false;
    // }

    // if (!g_DeviceManager.setVoltage(PSUG30, 28.0))
    // {
    //     std::cout << "DTN: Failed to set voltage on PSU G30!" << std::endl;
    //     return false;
    // }

    // if (!g_DeviceManager.enableOutput(PSUG30, true))
    // {
    //     std::cout << "DTN: Failed to enable output on PSU G30!" << std::endl;
    //     return false;
    // }

    // Send serial command
    g_systemCommand.execute("echo \"ID 1\" > /dev/ttyACM0");

    sleep(1);

    if (g_cumulus.deployNetworkInterfaces("/home/user/DTN/today/interfaces"))
    {
        std::cout << "Network configuration deployed successfully!" << std::endl;
    }
    else
    {
        std::cerr << "Failed to deploy network configuration!" << std::endl;
        return 1;
    }

    sleep(1);

    // Configure Cumulus switch VLANs
    if (!g_cumulus.configureSequence())
    {
        std::cout << "DTN: Cumulus configuration failed!" << std::endl;
        return false;
    }

    sleep(1);

    // SSH Deployment to Server
    if (!g_ssh_deployer_server.testConnection())
    {
        std::cout << "DTN: Cannot connect to server!" << std::endl;
        return false;
    }

    // List directory on server
    g_ssh_deployer_server.execute("ls -la /home/user");

    // Deploy, build and run with sudo (required for raw sockets)
    // Just pass folder name - path is auto-resolved from source root
    if (!g_ssh_deployer_server.deployAndBuild("remote_config_sender", "", true, true))
    {
        std::cout << "DTN: Deployment unsuccessful!" << std::endl;
        return false;
    }

    // Start SerialTimeForwarder after remote_config_sender is running
    // Reads time from MicroChip SyncServer (USB0), forwards to USB1, verifies on USB2
    // serial::SerialTimeForwarder timeForwarder("/dev/ttyUSB0", "/dev/ttyUSB1");
    // if (timeForwarder.start())
    // {
    //     std::cout << "DTN: SerialTimeForwarder started successfully." << std::endl;
    // }
    // else
    // {
    //     std::cerr << "DTN: Failed to start SerialTimeForwarder: "
    //               << timeForwarder.getLastError() << std::endl;
    // }

    // ==========================================
    // WIRE LATENCY TEST - DPDK'dan önce çalıştır
    // Gerçek wire-to-wire latency ölçümü (SO_TIMESTAMPING)
    // ==========================================
    std::cout << "\n=== WIRE LATENCY TEST ===" << std::endl;
    std::cout << "Deploying wire latency test (kernel SO_TIMESTAMPING)..." << std::endl;

    if (!g_ssh_deployer_server.deployAndBuild(
            "latency_test",     // kaynak klasör
            "wire_latency_test", // binary adı
            true,               // çalıştır
            true,               // sudo ile (raw socket için gerekli)
            BuildSystem::MAKE,  // Makefile kullan
            "",                 // runtime args
            "",                 // make args
            false               // ön planda çalıştır (tamamlanmasını bekle)
            ))
    {
        std::cout << "DTN: Wire latency test failed! Continuing anyway..." << std::endl;
        // Hata olsa bile devam et, DPDK çalışabilir
    }
    else
    {
        std::cout << "DTN: Wire latency test completed successfully!" << std::endl;
    }

    sleep(2);  // Latency test bitmeden DPDK başlamasın

    // ==========================================
    // DPDK - arka planda çalıştır (sürekli çalışan uygulama)
    // ==========================================
    std::cout << "\n=== DPDK APPLICATION ===" << std::endl;
    if (!g_ssh_deployer_server.deployAndBuild(
            "dpdk",            // kaynak klasör
            "",                // app name (otomatik algılar)
            true,              // çalıştır
            true,              // sudo ile (DPDK için gerekli)
            BuildSystem::AUTO, // otomatik algıla (Makefile bulacak)
            "-l 0-255 -n 16",  // EAL parametreleri
            "",                // make args (opsiyonel: "NUM_TX_CORES=4")
            true               // ARKA PLANDA ÇALIŞTIR!
            ))
    {
        std::cout << "DTN: DPDK deployment unsuccessful!" << std::endl;
        return false;
    }

    // DPDK başladı, şimdi istediğin işlemleri yap
    // sleep(360);

    utils::waitForCtrlC();

    // Stop SerialTimeForwarder and show stats
    // if (timeForwarder.isRunning())
    // {
    //     std::cout << "DTN: SerialTimeForwarder stats:" << std::endl;
    //     std::cout << "  Packets sent: " << timeForwarder.getPacketsSent() << std::endl;
    //     std::cout << "  Last timestamp: " << timeForwarder.getLastTimestamp() << std::endl;
    //     std::cout << "  Last time string: " << timeForwarder.getLastTimeString() << std::endl;
    //     timeForwarder.stop();
    //     std::cout << "DTN: SerialTimeForwarder stopped." << std::endl;
    // }

    //Çalışıyor mu kontrol et ve durdur
    // if (g_ssh_deployer_server.isApplicationRunning("dpdk_app"))
    // {
    //     g_ssh_deployer_server.stopApplication("dpdk_app", true);
    // }
    // // Monitor PSU measurements
    // for (int i = 0; i < 1000; i++)
    // {
    //     if (i % 20 == 0)
    //     {
    //         double current = g_DeviceManager.measureCurrent(PSUG30);
    //         double voltage = g_DeviceManager.measureVoltage(PSUG30);
    //         double power = g_DeviceManager.measurePower(PSUG30);
    //         double get_current = g_DeviceManager.getCurrent(PSUG30);
    //         double get_voltage = g_DeviceManager.getVoltage(PSUG30);

    //         {
    //             utils::FloatFormatGuard guard(std::cout, 2, true);
    //             std::cout << "Current: " << current << " " << "Voltage: " << voltage << " " << "Power: " << power << " " << "Get Current: " << get_current << " " << "Get Voltage:" << get_voltage << std::endl;
    //         }
    //     }
    // }

    // if (!g_DeviceManager.enableOutput(PSUG30, false))
    // {
    //     std::cout << "DTN: Failed to disable output on PSU G30!" << std::endl;
    //     return false;
    // }

    // if (!g_DeviceManager.disconnect(PSUG30))
    // {
    //     std::cout << "DTN: Failed to disconnect PSU G30!" << std::endl;
    //     return false;
    // }

    // g_Server.offWithWait(300);

    std::cout << "DTN: PSU configured successfully." << std::endl;
    return true;
}
