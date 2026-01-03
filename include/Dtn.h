#ifndef DTN_H
#define DTN_H

#include <string>
#include <vector>
#include "DeviceManager.h"
#include "Utils.h"
#include "SerialPort.h"
#include "Server.h"
#include "SystemCommand.h"

class Dtn
{
private:

public:
    Dtn();
    ~Dtn();

    /**
     * @brief Configure power supply sequence
     * @return true on success
     */
    bool configureSequence();
};

extern Dtn g_dtn;

#endif // DTN_H
