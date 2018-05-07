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

#include "Arduino.h"
#include "M2M_Quectel.h"

QuectelCellular::QuectelCellular(Stream* debugStream, uint8_t powerPin)
{
    _debugStream = debugStream;
    _powerPin = powerPin;

    pinMode(_powerPin, OUTPUT);
    pinMode(CM_STATUS, INPUT);

    //_apn = 0;
    //_apnusername = 0;
    //_apnpassword = 0;
}


bool QuectelCellular::begin(Uart* uart)
{
    _uart = uart;	
	_uart->begin(115200);

#if 1
    setPower(false);
    DEBUG_PRINT("Waiting");
    while (getStatus())
    {
        DEBUG_PRINT(".");
        delay(500);
        flush();
    }
    DEBUG_PRINTLN("");
#endif

    bool stat = getStatus();
    DEBUG_PRINT("Status: "); DEBUG_PRINTLN(stat);

    if (_powerPin != NOT_A_PIN &&
        !getStatus())
    {
        setPower(true);
        DEBUG_PRINT("Waiting");
        while (!getStatus())
        {
            DEBUG_PRINT(".");
            delay(500);
        }
        DEBUG_PRINTLN("");        
    }

    int16_t timeout = 7000;

	DEBUG_PRINTLN("Open communications");
    while (timeout > 0) 
    {
        flush();
        if (sendAndCheckReply(_AT, _OK, 1000))
        {
            DEBUG_PRINTLN("GOT OK");
            break;
        }
/*        flush();
        if (sendAndCheckReply(_AT, _AT, 1000))
        {
            DEBUG_PRINTLN("GOT AT");
            break;
        } */
        delay(500);
        timeout-=500;
    }

    if (timeout < 0)
    {
		DEBUG_PRINTLN("Failed to initialize cellular module");
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
            DEBUG_PRINTLN("Module initialized");
            break;
        }
    }

    // Wait for network registration
    NetworkRegistrationState state;
    do
    {
        state = getNetworkRegistration();
        switch (state)
        {
            case NetworkRegistrationState::NotRegistered:
                DEBUG_PRINTLN("Not registered");
                break;
            case NetworkRegistrationState::Registered:
                DEBUG_PRINTLN("Registered");
                break;
            case NetworkRegistrationState::Searching:
                DEBUG_PRINTLN("Searching");
                break;
            case NetworkRegistrationState::Denied:
                DEBUG_PRINTLN("Denied");
                break;
            case NetworkRegistrationState::Unknown:
                DEBUG_PRINTLN("Unknown");
                break;
            case NetworkRegistrationState::Roaming:
                DEBUG_PRINTLN("Roaming");
                break;
        }
        delay(1000);
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
            DEBUG_PRINTLN("Not a Quectel module");
            return false;
        }
        token = strtok(nullptr, linefeed);
        if (token == nullptr)
        {
            DEBUG_PRINTLN("Parse error");
            return false;
        }
		if (strcmp(token, "UG96"))
		{
			_moduleType = QuectelModule::UG96;
		}
        token = strtok(nullptr, linefeed);
                    DEBUG_PRINT("token "); DEBUG_PRINTLN(token);
        if (strlen(token) > 10)
        {
            strcpy(_firmwareVersion, token + 10);
        }
    }

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
    // +QCCID: 898600220909A0206023
    //
    // OK
    if (sendAndWaitForReply("AT+QCCID", 1000, 3))
    {
        const char delimiter[] = " ";
        char * token = strtok(_replyBuffer, delimiter);
        if (token)
        {
            token = strtok(nullptr, delimiter);
            uint8_t len = strlen(token);
            strncpy(buffer, token, len);
            return strlen(buffer);            
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
            DEBUG_PRINTLN(token);
            token = strtok(nullptr, delimiter);
            DEBUG_PRINTLN(token);
            token = strtok(nullptr, delimiter);
            DEBUG_PRINTLN(token);
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
        DEBUG_PRINTLN("Failed to setup PDP context");
        return false;
    }
    // Activate PDP context
    if (!sendAndCheckReply("AT+QIACT=1", _OK, 30000))
    {
        DEBUG_PRINTLN("Failed to activate PDP context");
        return false;
    }
    return true;
}

bool QuectelCellular::disconnectNetwork()
{
    if (!sendAndCheckReply("AT+QIDEACT=1", _OK, 30000))
    {
        DEBUG_PRINTLN("Failed to deactivate PDP context");
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
        DEBUG_PRINTLN("Connection failed");
        return false;
    }
    // Now we are waiting for connect
    // +QIOPEN: 1,0
    while (true)            // TODO: Timout
    {
        if (readReply(500, 1) &&
            strstr(_replyBuffer, "+QIOPEN"))
        {
            if (_replyBuffer[9] != '1')
            {
                DEBUG_PRINTLN("Connection failed");
                return false;
            }
            DEBUG_PRINTLN("Connection open");
            return true;
        }
    }
    DEBUG_PRINTLN(_replyBuffer);
    return false;
}

size_t QuectelCellular::write(uint8_t value)
{
    return write(&value, 1);
}

size_t QuectelCellular::write(const uint8_t *buf, size_t size)
{
    //DEBUG_PRINT("Send "); DEBUG_PRINTLN(size);
    char buffer[16];
    sprintf(buffer, "AT+QISEND=1,%i", size);
    COM_DEBUG_PRINT(" -> "); COM_DEBUG_PRINTLN(buffer);
    _uart->println(buffer);
   	COM_DEBUG_PRINT(" <- ");
    while (true)
    {
        if (_uart->available())
        {
            char c = _uart->read();
            COM_DEBUG_PRINT(c);
            if (c == '>')
            {
                break;
            }
        }
    }
    COM_DEBUG_PRINTLN();
    _uart->write(buf, size);
    while (true)
    {    
        if (readReply(5000, 1) &&
            strstr(_replyBuffer, "SEND OK"))
        {
            return size;
        }
    }
    DEBUG_PRINTLN("Send failed");
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
//                DEBUG_PRINT("Available: "); DEBUG_PRINTLN(unread);
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
        DEBUG_PRINT("Data len: "); DEBUG_PRINTLN(length);
        _uart->readBytes(_replyBuffer, length);
        memcpy(buf, _replyBuffer, length);
        buf[length] = '\0';
        for (int i=0; i < length; i++)
        {
            DEBUG_PRINT(buf[i], HEX); DEBUG_PRINT(" ");
        }
        DEBUG_PRINTLN();
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
        DEBUG_PRINTLN("Failed to close connection");
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
        return true;
    }
    return false;
}

// Private

bool QuectelCellular::setPower(bool state)
{
	DEBUG_PRINT("setPower: "); DEBUG_PRINTLN(state);
    if (_powerPin == NOT_A_PIN)
    {
        return false;
    }
    if (state)
    {
        pinMode(_powerPin, OUTPUT);
        digitalWrite(_powerPin, LOW);
        delay(200);
        digitalWrite(_powerPin, HIGH);
        return true;
    }
    return sendAndCheckReply("AT+QPOWD", _OK, 1000);
}

bool QuectelCellular::getStatus()
{
    return digitalRead(CM_STATUS);
}

bool QuectelCellular::sendAndWaitForMultilineReply(const char* command, uint8_t lines, uint16_t timeout)
{
	return sendAndWaitForReply(command, timeout, lines);
}

bool QuectelCellular::sendAndWaitForReply(const char* command, uint16_t timeout, uint8_t lines)
{
    flush();
	COM_DEBUG_PRINT(" -> ");
	COM_DEBUG_PRINTLN(command);
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
    			COM_DEBUG_PRINT(c);
                _replyBuffer[index] = c;
                index++;
            }
            if (c == '\n')
            {
                COM_DEBUG_PRINT(" <- "); COM_DEBUG_PRINT(linesFound); COM_DEBUG_PRINT(" - ");
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
    COM_DEBUG_PRINT(" (");
    for (int i=0; i < index; i++)
    {
        COM_DEBUG_PRINT(_replyBuffer[i], HEX); COM_DEBUG_PRINT(' ');
    }
    COM_DEBUG_PRINTLN(")");
    return true;
}
