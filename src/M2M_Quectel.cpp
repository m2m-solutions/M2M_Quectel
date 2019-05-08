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

    QT_DEBUG("Powering off module");
    setPower(false);
    QT_DEBUG("Powering on module");
    setPower(true);

    QT_TRACE_START("Waiting for module");
    while (!getStatus())
    {
        QT_TRACE_PART(".");
        callWatchdog();
        delay(500);
    }
    QT_TRACE_END("");      


	QT_DEBUG("Open communications");
    int32_t timeout = 7000;
    while (timeout > 0) 
    {
        flush();
        if (sendAndCheckReply(_AT, _OK, 1000))
        {
            QT_COM_TRACE("GOT OK");
            break;
        }
        callWatchdog();
        delay(500);
        timeout -= 500;
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

    QT_DEBUG("Checking SIM card");
    if (!getSimPresent())
    {
        QT_ERROR("No SIM card detected");
        return false;
    }

    QT_DEBUG("Waiting for module initialization");
    timeout = 5000;
    while (timeout > 0)            
    {
        if (readReply(500, 1) &&
            strstr(_replyBuffer, "PB DONE"))
        {
            QT_DEBUG("Module initialized");
            break;
        }
        callWatchdog();
        delay(500);
        timeout -= 500;
    }
    if (timeout <= 0)
    {
        // Non critical error
		QT_DEBUG("Failed waiting for phonebook initialization");
    }

    // Wait for network registration
    QT_DEBUG("Waiting for network registration");
    timeout = 60000;
    NetworkRegistrationState state;
    while (timeout > 0)
    {
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
        callWatchdog();
        delay(500);
        timeout -= 500;
        if (state == NetworkRegistrationState::Registered ||
           state == NetworkRegistrationState::Roaming)
        {
            break;
        }
    }
    if (timeout <= 0)
    {
        QT_ERROR("Network registration failed");
        return false;
    }

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
        if (strlen(token) > 10)
        {
            strcpy(_firmwareVersion, token + 10);
        }
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

bool QuectelCellular::getSimPresent()
{
    // Reply is:
    // +QSIMSTAT: 0,1
    //
    // OK
    if (sendAndWaitForReply("AT+QSIMSTAT?"))
    {
        const char delimiter[] = ",";
        char* token = strtok(_replyBuffer, delimiter);
        token = strtok(nullptr, delimiter);
        if (token)
        {
            return token[0] == 0x31;
        }        
    }
    return false;
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
        char* token = strtok(_replyBuffer, delimiter);
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
        SerialUSB.println(_replyBuffer);
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

// HTTP client interface
bool QuectelCellular::httpGet(const char* url, const char* fileName)
{
    char buffer[16];
    int status;
    int size;
    int result;

    // (Uses PDP context 2)
    // -> AT+QHTTPCFG="contextid",2
    // <- OK
    // -> AT+QHTTPURL=23,80
    // <- CONNECT
    // -> http://www.sina.com.cn
    // <- OK
    // -> AT+QHTTPGET=80
    // <- OK
    // <- +QHTTPGET: 0,200,631871
    // -> AT+QHTTPREADFILE="RAM:1.bin",60,2
    // <- OK
    // <- +QHTTPREADFILE
    bool ssl = strstr(url, "https://") != nullptr;

    if (!sendAndCheckReply("AT+QHTTPCFG=\"contextid\",1", _OK, 10000))
    {
        QT_ERROR("Failed to activate PDP context");
        return false;
    }

    if (ssl)
    {
        QT_TRACE("Enabling SSL support");
        if (!sendAndCheckReply("AT+QHTTPCFG=\"sslctxid\",1", _OK, 10000))
        {
            QT_ERROR("Failed to activate SSL context ID");
            return false;
        }
        if (!sendAndCheckReply("AT+QSSLCFG=\"sslversion\",1,3", _OK, 10000))    // Set TLS 1.2
        {
            QT_ERROR("Failed to set TLS version");
            return false;
        }
        if (!sendAndCheckReply("AT+QSSLCFG=\"ciphersuite\",1,\"0xFFFF\"", _OK, 10000))  // Allow all
        {
            QT_ERROR("Failed to set cipher suites");
            return false;
        }
        if (!sendAndCheckReply("AT+QSSLCFG=\"seclevel\",1,0", _OK, 10000))
        {
            QT_ERROR("Failed to set security level");
            return false;
        }
    }

    sprintf(buffer, "AT+QHTTPURL=%i,30", strlen(url));
    if (!sendAndCheckReply(buffer, "CONNECT", 2000))
    {
        QT_ERROR("Failed to activate URL");
        return false;
    }
    if (!sendAndCheckReply(url, "OK", 2000))
    {
        QT_ERROR("Failed to send URL");
        return false;
    }
    if (!sendAndWaitForReply("AT+QHTTPGET=60", 60000, 3))
    {
        QT_ERROR("Failed to send request");
        return false;
    }
    const char qHttpGet[] = "+QHTTPGET: ";
    QT_TRACE(_replyBuffer);    
    char * token = strtok(_replyBuffer, qHttpGet);
    if (!token)
    {
        QT_ERROR("Failed to receive data");
        return false;
    }
    const char delimiter[] = ",";
    token = strtok(nullptr, delimiter);
    token = strtok(nullptr, delimiter);
    if (!token)
    {
        QT_ERROR("Failed to receive data");
        return false;
    }
    status = atoi(token);
    token = strtok(nullptr, delimiter);
    size = atoi(token);
    QT_COM_DEBUG("HTTP status code: %i", status);
    QT_COM_DEBUG("HTTP response size: %i", size);

    sprintf(buffer, "AT+QHTTPREADFILE=\"RAM:%s\",60,1", fileName);
    if (!sendAndWaitForReply(buffer, 60000, 3))
    {
        QT_ERROR("Failed to read response");
        return false;
    }
    const char qReadFile[] = "+QHTTPREADFILE: ";
//    QT_TRACE(_replyBuffer);    
    token = strtok(_replyBuffer, qReadFile);
    if (!token)
    {
        QT_ERROR("Failed to save response");
        return false;
    }
    token = strtok(nullptr, delimiter);
    result = atoi(token);
    QT_COM_DEBUG("HTTP read response result: %i", result);
    if (result != 0)
    {
        QT_ERROR("Failed to save response, error %i", result);
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////
//
// TCP client interface
//
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
    // TODO: Max 1460 bytes can be sent in one +QISEND session
    // Add a loop
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
            if (token)
            {
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
            QT_COM_TRACE_ASCII(buffer, length);
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
        char* tokenStart = strstr(_replyBuffer, "+QISTATE:");
        tokenStart = &_replyBuffer[tokenStart - _replyBuffer];
        
        char* token = strtok(tokenStart, ",");
        token = strtok(nullptr, ",");
        token = strtok(nullptr, ",");
        token = strtok(nullptr, ",");
        token = strtok(nullptr, ",");
        token = strtok(nullptr, ",");
        
        return strcmp(token, "3") == 0;        
    }
    return false;
}

///////////////////////////////////////////////////////////
//
// File client interface
//
FILE_HANDLE QuectelCellular::openFile(const char* fileName, bool overWrite)
{
    // AT+QFOPEN="RAM:file.ext",0
    // +QFOPEN:3000
    //
    // OK
    char buffer[32];
    sprintf(buffer,"AT+QFOPEN=\"RAM:%s\",%i", fileName, overWrite ? 1 : 0);
    if (!sendAndWaitForReply(buffer, 1000, 3))
    {
        QT_ERROR("Timeout opening file");
        return NOT_A_FILE_HANDLE;
    }
    char* token = strtok(_replyBuffer, "+QFOPEN: ");
    if (token)
    {
        uint32_t result = atoi(token);
        return result;
    }
    return NOT_A_FILE_HANDLE;
}

bool QuectelCellular::readFile(FILE_HANDLE fileHandle, uint8_t* buffer, uint32_t length)
{
    // AT+QFREAD=3000,10
    // CONNECT
    // Read data
    //
    // OK
    char command[32];
    sprintf(command,"AT+QFREAD=%li,%lu", fileHandle, length);
    if (!sendAndCheckReply(command, _CONNECT, 1000))
    {
        QT_ERROR("Timeout for read command");
        return false;
    }
    for (uint32_t i=0; i < length; i++)
    {
        uint32_t timeout = 1000;
        while (!_uart->available())
        {
            timeout--;
            delay(1);
        }
        buffer[i] = _uart->read();
    }
    if (!readReply(1000, 1))
    {
        QT_ERROR("No reply after read");
        return false;
    }
    return checkResult();
}

bool QuectelCellular::writeFile(FILE_HANDLE fileHandle, const uint8_t* buffer, uint32_t length)
{
    // AT+QFWRITE=3000,10
    // CONNECT
    // write 10 bytes
    // +QFWRITE(10,10)
    char command[32];
    sprintf(command,"AT+QFWRITE=%li,%lu", fileHandle, length);
    if (sendAndCheckReply(command, _CONNECT, 1000))
    {
        for (uint32_t i=0; i < length; i++)
        {
            _uart->write(buffer[i]);
        }
        if (!readReply(1000, 3))
        {
            QT_ERROR("No reply after write");
            return false;
        }
        return (strstr(_replyBuffer, "+QFWRITE:") != nullptr);
    }
    return false;
}

bool QuectelCellular::seekFile(FILE_HANDLE fileHandle, uint32_t length)
{
    // AT+QFSEEK=3000,0,0
    // OK
    char buffer[32];
    sprintf(buffer,"AT+QFSEEK=%li,%lu,0", fileHandle, length);
    if (!sendAndCheckReply(buffer, _OK, 1000))
    {
        QT_ERROR("Seek error: %s", _replyBuffer);
        return false;
    }
    return checkResult();
}

uint32_t QuectelCellular::getFilePosition(FILE_HANDLE fileHandle)
{
    // AT+QFPOSITION=3000
    // +QFPOSITION: 123
    //
    // OK
    char command[32];
    sprintf(command, "AT+QFPOSITION=%li", fileHandle);
    if (!sendAndWaitForReply(command, 1000, 3))
    {
        QT_ERROR("File position error: %s", _replyBuffer);
        return -1;
    }
    char* token = strtok(_replyBuffer, "+QFPOSITION: ");
    if (!token)
    {
        QT_ERROR("Get position error: %s", _replyBuffer);
        return -1;
    }
    uint32_t result = atoi(token);
    return result;
}

bool QuectelCellular::truncateFile(FILE_HANDLE fileHandle)
{
    // AT+QFTUCAT=3000
    // OK
    char command[32];
    sprintf(command, "AT+QFTUCAT=%li", fileHandle);
    if (!sendAndCheckReply(command, _OK, 1000))
    {
        QT_ERROR("Timeout deleting file: %s", _replyBuffer);
        return false;
    }
    return checkResult();
}

bool QuectelCellular::closeFile(FILE_HANDLE fileHandle)
{
    // AT+QFCLOSE=3000
    // OK
    char command[32];
    sprintf(command, "AT+QFCLOSE=%li", fileHandle);
    if (!sendAndCheckReply(command, _OK, 1000))
    {
        QT_ERROR("Timeout closing file: %s", _replyBuffer);
        return false;
    }
    return checkResult();
}

// TODO: Not tested
bool QuectelCellular::uploadFile(const char* fileName, const uint8_t* buffer, uint32_t length)
{
    // AT+QFUPL="RAM:test1.txt",10
    // CONNECT
    // <data>
    // +QFUPL:300,B34A
    char command[32];
    sprintf(command, "AT+QFUPL=\"RAM:%s\",%lu", fileName, length);
    if (!sendAndWaitForReply(command, 1000, 2))
    {
        QT_ERROR("No response to upload command");
        return false;        
    }
    if (!strstr(_replyBuffer, "CONNECT"))
    {
        QT_ERROR(_replyBuffer);
        return false;
    }
    for (uint32_t i = 0; i < length; i++)
    {
        _uart->print(buffer[i]);
    }
    if (!readReply(1000, 2))
    {
        QT_ERROR("No reponse after upload");
    }
    return checkResult();
}

// TODO: Not tested
bool QuectelCellular::downloadFile(const char* fileName, uint8_t* buffer, uint32_t length)
{
    // AT+QFDWL="RAM:test.txt"
    // CONNECT
    // <read data>
    // +QFDWL: 10,613e
    char command[32];
    sprintf(command, "AT+QFDWL=\"RAM:%s\",%lu", fileName, length);
    if (!sendAndWaitForReply(command, 1000, 2))
    {
        QT_ERROR("No response to download command");
        return false;        
    }
    if (!strstr(_replyBuffer, "CONNECT"))
    {
        QT_ERROR(_replyBuffer);
        return false;
    }
    for (uint32_t i = 0; i < length; i++)
    {
        uint32_t timeout = 1000;
        while (timeout--)
        {
            while (_uart->available())
            {
                buffer[i] = _uart->read();
            }
            delay(1);
        }
    }
    if (!readReply(1000, 2))
    {
        QT_ERROR("No reponse after download");
    }
    return (strstr(_replyBuffer, "+QFDWL:") != nullptr);
}

// TODO: Not implemented
uint32_t QuectelCellular::getFileSize(const char* fileName)
{    
    return 0;
}

bool QuectelCellular::deleteFile(const char* fileName)
{
    // AT+QFDEL:"RAM:file.txt"
    // OK
    char command[32];
    sprintf(command, "AT+QFDEL=\"RAM:%s\"", fileName);
    if (!sendAndCheckReply(command, _OK, 1000))
    {
        QT_ERROR("Timeout deleting file: %s", _replyBuffer);
        return false;
    }
    return checkResult();
}

// Private

bool QuectelCellular::setPower(bool state)
{
	QT_DEBUG("setPower: %i", state);
    if (state == true)
    {
        if (_powerPin == NOT_A_PIN)
        {
            return true;
        }
        digitalWrite(_powerPin, LOW);
        delay(300);
        digitalWrite(_powerPin, HIGH);
        return true;
    }
    else
    {
        QT_DEBUG("Powering down module");
        if (!sendAndCheckReply("AT+QPOWD", _OK, 1000))
        {
            return false;
        }
        uint32_t timeout = millis() + 60000;  // max 60 seconds for a shutdown
        while (timeout > millis())
        {
            if (readReply(1000, 1))
            {
                if (strstr(_replyBuffer, "+QIURC: \"pdpdeact\",1"))
                {
                    QT_DEBUG("PDP deactivated");
                }
                if (strstr(_replyBuffer, "POWERED DOWN"))
                {
                    QT_DEBUG("Module powered down");
                    break;
                }
            }
            callWatchdog();
        }
        return false;
    }
}

bool QuectelCellular::getStatus()
{
    if (_statusPin == NOT_A_PIN)
    {
        return true;
    }
    return digitalRead(_statusPin) == HIGH;
}

int8_t QuectelCellular::getLastError()
{
    return _lastError;
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
        callWatchdog();
        delay(1);
    }
    _replyBuffer[index] = 0;
    QT_COM_TRACE_START("");
    QT_COM_TRACE_PART("%i lines - ", linesFound);
    QT_COM_TRACE_ASCII(_replyBuffer, index);
    QT_COM_TRACE_END("");
    return true;
}

bool QuectelCellular::checkResult()
{   
    // CheckResult returns one of these:
    // true    OK
    // false   Unknown result
    // false  CME Error
    //
    char* token = strstr(_replyBuffer, _OK);
    if (token)
    {
        //QT_TRACE("*OK - %s", _replyBuffer);
        _lastError = 0;
        return true;
    }
    token = strstr(_replyBuffer, _CME_ERROR);
    if (!token)
    {
        //QT_TRACE("*NO CME ERROR: %s", _replyBuffer);
        _lastError = -1;
        return false;
    }
    //QT_TRACE("*CME ERROR: %s", _replyBuffer);
    token = strtok(_replyBuffer, _CME_ERROR);
    _lastError = atoi(token);
    return false;
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

