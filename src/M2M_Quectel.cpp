//---------------------------------------------------------------------------------------------
//
// Library for Quctel cellular modules.
//
// Copyright 2016-2018, M2M Solutions AB
// Written by Jonny Bergdahl, 2016-11-18
//
// Licensed under the MIT license, see the LICENSE.txt file.
//
////////////////////////////////////////////////////////////////////////////////////////////////

#include <Arduino.h>
#include "M2M_Quectel.h"

QuectelCellular::QuectelCellular(int8_t powerPin, int8_t statusPin)
{
    _powerPin = powerPin;
    _statusPin = statusPin;

    if (_powerPin != NOT_A_PIN)
    {
        pinMode(_powerPin, OUTPUT);
        digitalWrite(powerPin, HIGH);
    }
    if (statusPin != NOT_A_PIN)
    {
        pinMode(_statusPin, INPUT);
    }
}


bool QuectelCellular::begin(Uart* uart)
{
    _uart = uart;	
	_uart->begin(115200);

#if 0
    setPower(false);
    QT_TRACE_START("Begin... ");
    if (_statusPin != NOT_A_PIN)
    { 
        int stat;
        while ((stat = getStatus()))
        {
            QT_TRACE_PART(".");
            callWatchdog();
            delay(500);
            flush();
        }
        QT_TRACE_END("");
    }
    else
    {
        delay(1000);
    }
#endif

    setPower(true);
    QT_TRACE_START("Waiting for module");
    while (!getStatus())
    {
        QT_TRACE_PART(".");
        callWatchdog();
        delay(500);
    }
    QT_TRACE_END("");        

    int16_t timeout = 7000;

	QT_INFO("Open communications");
    while (timeout > 0) 
    {
        flush();
        if (sendAndCheckReply(_AT, _OK, 1000))
        {
            QT_COM_TRACE("GOT OK");
            break;
        }
        flush();
        if (sendAndCheckReply(_AT, _AT, 1000))
        {
            QT_COM_TRACE("GOT AT");
            break;
        }
        callWatchdog();
        delay(500);
        timeout-=500;
    }

    if (timeout < 0)
    {
		QT_ERROR("Failed to initialize cellular module");
        return false;
    }

    // Disable echo
    sendAndCheckReply("ATE0", _OK, 1000);
    // Set verbose error messages
    sendAndCheckReply("AT+CMEE=2", _OK, 1000);

    // Wait for ready. Module outputs:
    // 
    // +CPIN: READY
    //
    // +QIND: SMS DONE
    //
    // +QIND: PB DONE
    while (true)            // TODO: Timeout
    {
        if (readReply(500, 1) &&
            strstr(_replyBuffer, "PB DONE"))
        {
            QT_INFO("Module initialized");
            break;
        }
        callWatchdog();
    }

    // Wait for network registration
    NetworkRegistrationState state;
    do
    {
        callWatchdog();
        delay(500);
        state = getNetworkRegistration();
        switch (state)
        {
            case NetworkRegistrationState::NotRegistered:
                QT_DEBUG("Not registered");
                break;
            case NetworkRegistrationState::Registered:
                QT_DEBUG("Registered");
                break;
            case NetworkRegistrationState::Searching:
                QT_DEBUG("Searching");
                break;
            case NetworkRegistrationState::Denied:
                QT_DEBUG("Denied");
                break;
            case NetworkRegistrationState::Unknown:
                QT_DEBUG("Unknown");
                break;
            case NetworkRegistrationState::Roaming:
                QT_DEBUG("Roaming");
                break;
        }
    }
    while (state != NetworkRegistrationState::Registered &&
           state != NetworkRegistrationState::Roaming);

    if (sendAndWaitForReply("ATI", 1000, 5))
    {
		// response is:
		// Quectel
		// UG96
		// Revision: UG96LNAR02A06E1G
        //
        // OK
        
        const char linefeed[] = "\n";
        char * token = strtok(_replyBuffer, linefeed);
        if (token == nullptr ||
            strcmp(token, "Quectel") != 0)
        {
            QT_ERROR("Not a Quectel module");
            return false;
        }
        token = strtok(nullptr, linefeed);
        if (token == nullptr)
        {
            QT_ERROR("Parse error");
            return false;
        }
		if (strcmp(token, "UG96"))
		{
			_moduleType = QuectelModule::UG96;
		}
        token = strtok(nullptr, linefeed);
        QT_COM_TRACE_START("token");
        QT_COM_TRACE_PART(token);
        if (strlen(token) > 10)
        {
            strcpy(_firmwareVersion, token + 10);
        }
        QT_COM_TRACE_END("");
    }
    callWatchdog();
    return true;
}

const char* QuectelCellular::getFirmwareVersion()
{
	return _firmwareVersion;
}

uint8_t QuectelCellular::getIMEI(char* buffer)
{
    if (sendAndWaitForReply("AT+GSN"))
    {
        strncpy(buffer, _replyBuffer, 15);
        buffer[15] = 0;        
        return strlen(buffer);
    }
    return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////
//
// Logging
//
void QuectelCellular::setLogger(Logger* logger)
{
	_logger = logger;
}

////
uint8_t QuectelCellular::getOperatorName(char* buffer)
{
    // Reply is:
    // +COPS: 0,0,"Telenor SE",6
    //
    // OK   
    if (sendAndWaitForReply("AT+COPS?", 1000, 3))
    {
        const char delimiter[] = ",";
        char * token = strtok(_replyBuffer, delimiter);
        if (token)
        {
            token = strtok(nullptr, delimiter);
            token = strtok(nullptr, delimiter);
            if (token)
            {
                // Strip out the " characters                
                uint8_t len = strlen(token);
                strncpy(buffer, token + 1, len - 2);
                buffer[len - 2] = 0;        
                return strlen(buffer);
            }
        }
    }
    return 0;
}

uint8_t QuectelCellular::getRSSI()
{
    // Reply is:
    // +CSQ: 14,2
    //
    // OK
    if (sendAndWaitForReply("AT+CSQ", 1000, 3))
    {        
        char * token = strtok(_replyBuffer, " ");
        if (token)
        {
            token = strtok(nullptr, ",");
            if (token)
            {
                char* ptr;
                return strtol(token, &ptr, 10);
            }
        }
    }
    return 0;
}

uint8_t QuectelCellular::getSIMCCID(char* buffer)
{
    char delim[] = " \n";
    // +QCCID: 898600220909A0206023
    //
    // OK
    if (sendAndWaitForReply("AT+QCCID", 1000, 3))
    {
        char * token = strtok(_replyBuffer, delim);
        if (token)
        {
            token = strtok(nullptr, delim);
            uint8_t len = strlen(token);
            strncpy(buffer, token, len + 1);
            return len;                        
        }
    }
    return 0;    
}

NetworkRegistrationState QuectelCellular::getNetworkRegistration()
{
    if (sendAndWaitForReply("AT+CREG?", 1000, 3))   
    {
        const char delimiter[] = ",";
        char * token = strtok(_replyBuffer, delimiter);
        if (token)
        {
            token = strtok(nullptr, delimiter);
            if (token)
            {
                return (NetworkRegistrationState)(token[0] - 0x30);
            }
        }
    }
    return NetworkRegistrationState::Unknown;  
}

double QuectelCellular::getVoltage()
{
    if (sendAndWaitForReply("AT+CBC", 1000, 3))
    {
        const char delimiter[] = ",";
        char * token = strtok(_replyBuffer, delimiter);        
        if (token)
        {
            token = strtok(nullptr, delimiter);
            token = strtok(nullptr, delimiter);
            if (token)
            {
                char* ptr;
                uint16_t milliVolts = strtol(token, &ptr, 10);
                return milliVolts / 1000.0;
            }
        }
    }
    return 0;
}

bool QuectelCellular::connectNetwork(const char* apn, const char* userId, const char* password)
{
    char buffer[64];

    // First set up PDP context
    sprintf(buffer, "AT+QICSGP=1,1,\"%s\",\"%s\",\"%s\",1", apn, userId, password);
    if (!sendAndCheckReply(buffer, _OK, 1000))
    {
        QT_ERROR("Failed to setup PDP context");
        return false;
    }
    callWatchdog();
    // Activate PDP context
    if (!sendAndCheckReply("AT+QIACT=1", _OK, 30000))
    {
        QT_ERROR("Failed to activate PDP context");
        return false;
    }
    return true;
}

bool QuectelCellular::disconnectNetwork()
{
    if (!sendAndCheckReply("AT+QIDEACT=1", _OK, 30000))
    {
        QT_ERROR("Failed to deactivate PDP context");
        return false;
    }
    return true;
}

// Client interface
int QuectelCellular::connect(IPAddress ip, uint16_t port)
{
    char buffer[16];
    sprintf(buffer, "%i.%i.%i.%i", ip[0], ip[1], ip[2], ip[3]);    
    return connect(buffer, port);
}

int QuectelCellular::connect(const char *host, uint16_t port)
{
    // AT+QIOPEN=1,1,"TCP","220.180.239.201",8713,0,0
    char buffer[64];
    sprintf(buffer, "AT+QIOPEN=1,1,\"TCP\",\"%s\",%i,0,0", host, port);
    if (!sendAndCheckReply(buffer, _OK))
    {
        QT_ERROR("Connection failed");
        return false;
    }
    // Now we are waiting for connect
    // +QIOPEN: 1,0
    uint32_t expireTime = millis() + 30 * 1000;
    while (true)            // TODO: Timout
    {
        callWatchdog();
        if (readReply(500, 1) &&
            strstr(_replyBuffer, "+QIOPEN"))
        {
            if (_replyBuffer[9] != '1')
            {
                QT_ERROR("Connection failed");
                return false;
            }
            QT_DEBUG("Connection open");
            return true;
        }
        if (millis() > expireTime)
        {
                QT_ERROR("Connection timeout");
                return false;
        }
    }
    QT_TRACE(_replyBuffer);
    return false;
}

size_t QuectelCellular::write(uint8_t value)
{
    return write(&value, 1);
}

size_t QuectelCellular::write(const uint8_t *buf, size_t size)
{
    char buffer[16];
    sprintf(buffer, "AT+QISEND=1,%i", size);
    QT_COM_TRACE_START(" -> ");
    QT_COM_TRACE_PART(buffer);
    QT_COM_TRACE_END("");
    _uart->println(buffer);
    uint32_t timeout = millis() + 5000;
    bool success = false;
    while (millis() < timeout)
    {
        if (_uart->available())
        {
            char c = _uart->read();
            if (c == '>')
            {
                success = true;
                break;
            }
        }
    }
    if (!success)
    {
        QT_ERROR("+QISEND handshake error");
        return 0;
    }
   	QT_COM_TRACE_START(" <- ");
    QT_COM_TRACE_BUFFER(buf, size);
    QT_COM_TRACE_END("");
    _uart->write(buf, size);
    if (readReply(5000, 1) &&
        strstr(_replyBuffer, "SEND OK"))
    {
        return size;
    }    
    QT_ERROR("Send failed");
    return 0;
}

int QuectelCellular::available()
{
    if (sendAndWaitForReply("AT+QIRD=1,0", 1000, 3))
    {
        const char delimiter[] = ",";
        char * token = strtok(_replyBuffer, delimiter);                
        if (token)
        {
            token = strtok(nullptr, delimiter);
            token = strtok(nullptr, delimiter);
            if (token)
            {
                char* ptr;
                uint16_t unread = strtol(token, &ptr, 10);
                QT_COM_TRACE("Available: %i", unread);
                return unread;
            }
        }        
    }
    return 0;
}

int QuectelCellular::read()
{
    uint8_t buffer[2];
    read(buffer, 1);
    return buffer[0];
}

int QuectelCellular::read(uint8_t *buf, size_t size)
{
    char buffer[16];
    sprintf(buffer, "AT+QIRD=1,%i", size);
    if (sendAndWaitForReply(buffer, 1000, 1) &&
        strstr(_replyBuffer, "+QIRD:"))
    {        
        // +QIRD: <len>
        // <data>
        //
        // OK
        char* token = strtok(_replyBuffer, " ");
        token = strtok(nullptr, "\n");
        char* ptr;
        uint16_t length = strtol(token, &ptr, 10);
        QT_COM_TRACE("Data len: %i", length);
        _uart->readBytes(_replyBuffer, length);
        memcpy(buf, _replyBuffer, length);
        buf[length] = '\0';
        QT_COM_TRACE_START("");
        for (int i=0; i < length; i++)
        {
            QT_COM_TRACE_BUFFER(buffer, length);
        }
        QT_COM_TRACE_END("");
        return length;       
    }
    return 0;
}

int QuectelCellular::peek()
{
    // Not supported
    return 0;
}

void QuectelCellular::flush()
{
    while (_uart->available())
    {
        _uart->read();
    }
}

void QuectelCellular::stop()
{
    // AT+QICLOSE=1,10
    if (!sendAndCheckReply("AT+QICLOSE=1,10", _OK, 10000))
    {
        QT_ERROR("Failed to close connection");
    }
    uint32_t timeout = millis() + 20000;
    while (millis() < timeout)
    {
        sendAndWaitForReply("AT+QISTATE=1,1", 1000, 3);
        if (_replyBuffer[0] == 'O' && _replyBuffer[1] == 'K')
        {            
            QT_TRACE("Disconnected");
            return;
        }
        callWatchdog();
        delay(500);
    }
}

uint8_t QuectelCellular::connected()
{
    // Response is:
    // +QISTATE: 1,"TCP","54.225.64.197",80,4097,5,1,1,0,"uart1"
    //
    // OK
    //
    if (sendAndWaitForReply("AT+QISTATE=1,1", 1000, 3) &&
        strstr(_replyBuffer, "+QISTATE:"))
    {
        QT_TRACE("connected(): %s",_replyBuffer);
        char* token = strtok(_replyBuffer, ",");
        token = strtok(nullptr, ",");
        token = strtok(nullptr, ",");
        token = strtok(nullptr, ",");
        token = strtok(nullptr, ",");
        token = strtok(nullptr, ",");
        return strcmp(token, "3") == 0;        
    }
    return false;
}

// Private

bool QuectelCellular::setPower(bool state)
{
	QT_DEBUG("setPower: %i", state);
    if (_powerPin == NOT_A_PIN)
    {
        return false;
    }
    if (state)
    {
        digitalWrite(_powerPin, LOW);
        delay(300);
        digitalWrite(_powerPin, HIGH);
        return true;
    }
    return sendAndCheckReply("AT+QPOWD", _OK, 1000);
}

bool QuectelCellular::getStatus()
{
    if (_statusPin == NOT_A_PIN)
    {
        return true;
    }
    return digitalRead(_statusPin) == HIGH;
}

bool QuectelCellular::sendAndWaitForMultilineReply(const char* command, uint8_t lines, uint16_t timeout)
{
	return sendAndWaitForReply(command, timeout, lines);
}

bool QuectelCellular::sendAndWaitForReply(const char* command, uint16_t timeout, uint8_t lines)
{
    flush();
	QT_COM_TRACE(" -> %s", command);
    _uart->println(command);
    return readReply(timeout, lines);
}

bool QuectelCellular::sendAndCheckReply(const char* command, const char* reply, uint16_t timeout)
{
    sendAndWaitForReply(command, timeout);
    return (strstr(_replyBuffer, reply) != nullptr);
}

bool QuectelCellular::readReply(uint16_t timeout, uint8_t lines)
{
    uint16_t index = 0;
    uint16_t linesFound = 0;

	//COM_DEBUG_PRINT("timeout "); COM_DEBUG_PRINTLN(timeout);
	//COM_DEBUG_PRINT("lines "); COM_DEBUG_PRINTLN(lines);
	//COM_DEBUG_PRINT(" <- ");

    while (timeout--)
    {
        if (index > 254)
        {
            break;
        }
        while (_uart->available())
        {
            char c = _uart->read();

            if (c == '\r')
            {
                continue;
            }
            if (linesFound > 0)
            {    			
                _replyBuffer[index] = c;
                index++;
            }
            if (c == '\n')
            {
				linesFound++;
            }
    		if (linesFound > lines)
	    	{
    			break;
	    	}            
        }

   		if (linesFound > lines)
    	{
   			break;
    	}            

        if (timeout <= 0)
        {
            return false;
        }
        delay(1);
    }
    _replyBuffer[index] = 0;
    QT_COM_TRACE_START("");
    QT_COM_TRACE_PART("%i lines - ", linesFound);
    QT_COM_TRACE_BUFFER(_replyBuffer, index);
    QT_COM_TRACE_END("");
    return true;
}

void QuectelCellular::callWatchdog()
{
    if (watchdogcallback != nullptr)
    {
        (watchdogcallback)();
    }
}

void QuectelCellular::setWatchdogCallback(WATCHDOG_CALLBACK_SIGNATURE)
{
    this->watchdogcallback = watchdogcallback;
}

