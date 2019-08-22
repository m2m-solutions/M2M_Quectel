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
    uint32_t timeout = 5000;
    while (timeout > 0)            
    {
        if (readReply(500, 1) &&
            strstr(_buffer, "PB DONE"))
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
        char * token = strtok(_buffer, linefeed);
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
        strncpy(buffer, _buffer, 15);
        buffer[15] = 0;        
        return strlen(buffer);
    }
    return 0;
}

bool QuectelCellular::setEncryption(TlsEncryption enc) 
{
    _encryption = enc;
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
        char* token = strtok(_buffer, delimiter);
        token = strtok(nullptr, delimiter);
        if (token)
        {
            return token[0] == 0x31;
        }        
    }
    return false;
}

const char* QuectelCellular::getModuleType()
{
    switch (_moduleType)
    {
        case QuectelModule::UG96:
            return "UG96";
        case QuectelModule::BG96:
            return "BG96";
        case QuectelModule::M95:
            return "M95";
        default:
            return "Unknown";
    }
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
        char* token = strtok(_buffer, delimiter);
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
        char * token = strtok(_buffer, " ");
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
        char * token = strtok(_buffer, delim);
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

uint8_t QuectelCellular::getSIMIMSI(char* buffer)
{
    char delim[] = "\n";
    // 240080007440698
    //
    // OK
    if (sendAndWaitForReply("AT+CIMI", 1000, 3))
    {
        char * lf = strstr(_buffer, delim);
        if (lf)
        {
            uint8_t len = lf - _buffer;
            strncpy(buffer, _buffer, len);
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
        char * token = strtok(_buffer, delimiter);
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
        char * token = strtok(_buffer, delimiter);        
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
    // First set up PDP context
    sprintf(_buffer, "AT+QICSGP=1,1,\"%s\",\"%s\",\"%s\",1", apn, userId, password);
    if (!sendAndCheckReply(_buffer, _OK, 1000))
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

// HTTP client interface
bool QuectelCellular::httpGet(const char* url, const char* fileName)
{
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
        if (!activateSsl())
        {
            return false;
        }
    }

    sprintf(_buffer, "AT+QHTTPURL=%i,30", strlen(url));
    if (!sendAndCheckReply(_buffer, "CONNECT", 2000))
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
    char * token = strtok(_buffer, qHttpGet);
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
    QT_COM_DEBUG("HTTP status code: %i, size: %i", status, size);

    sprintf(_buffer, "AT+QHTTPREADFILE=\"RAM:%s\",60,1", fileName);
    if (!sendAndWaitForReply(_buffer, 60000, 3))
    {
        QT_ERROR("Failed to read response");
        return false;
    }
    const char qReadFile[] = "+QHTTPREADFILE: ";
    token = strtok(_buffer, qReadFile);
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
    sprintf(_buffer, "%i.%i.%i.%i", ip[0], ip[1], ip[2], ip[3]);
    return connect(_buffer, port);
}

int QuectelCellular::connect(IPAddress ip, uint16_t port, TlsEncryption encryption)
{
    _encryption = encryption;
    return connect(ip, port);
}

int QuectelCellular::connect(const char *host, uint16_t port, TlsEncryption encryption) {
    _encryption = encryption;
    return connect(host, port);
}

int QuectelCellular::connect(const char *host, uint16_t port)
{
    if (useEncryption())
    {
        if (!activateSsl())
        {
            return false;
        }
    }
	
	sprintf(_buffer, "AT+QCFG=\"urc/port\",0,\"uart1\"");
    if (!sendAndCheckReply(_buffer, _OK))
    {
        QT_ERROR("Could not remove urc messages");
        return false;
    }

    // AT+QIOPEN=1,1,"TCP","220.180.239.201",8713,0,0
    sprintf(_command, "+Q%sOPEN", useEncryption() ? _SSL_PREFIX : _INET_PREFIX);
    if (useEncryption())
    {
        sprintf(_buffer, "AT%s=1,1,1,\"%s\",%i,0", _command, host, port);    
    }
    else
    {
        sprintf(_buffer, "AT%s=1,1,\"TCP\",\"%s\",%i,0,0", _command, host, port);
    }
    if (!sendAndCheckReply(_buffer, _OK))
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
            strstr(_buffer, _command))
        {
            char* token = strtok(_buffer, " ");
            token = strtok(nullptr, ",");

            if (*token != '1')
            {
                QT_ERROR("Connection failed, %i, '%c'", strlen(_buffer), token);
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
    QT_TRACE(_buffer);
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
    sprintf(_command, "+Q%sSEND", useEncryption() ? _SSL_PREFIX : _INET_PREFIX);
    sprintf(_buffer, "AT%s=1,%i", _command, size);
    if (!sendAndWaitFor(_buffer, "> ", 5000))
    {
        QT_ERROR("%s handshake error, %s", _command, _buffer);
        return 0;
    }    
   	QT_COM_TRACE_START(" -> ");
    QT_COM_TRACE_BUFFER(buf, size);
    QT_COM_TRACE_END("");
    _uart->write(buf, size);
    if (readReply(5000, 1) &&
        strstr(_buffer, "SEND OK"))
    {
        return size;
    }    
    QT_ERROR("Send failed");
    return 0;
}

int QuectelCellular::available()
{
    if (useEncryption())
    {
        if (sslLength > 0)
        {
            return sslLength;
        }
        sprintf(_buffer, "AT+QSSLRECV=1,%i", sizeof(_buffer) - 36);
        if (sendAndWaitForReply(_buffer, 1000, 3))
        {
            char* recvToken = strstr(_buffer, "+QSSLRECV: ");
			recvToken += 11;
			
            /* Get length of data */
            char* lfToken = strstr(recvToken, "\n");
			uint32_t llen = lfToken - recvToken;
			
			char numberStr[llen];
			strncpy(numberStr, recvToken, llen);
			numberStr[llen] = '\0';
			
			sslLength = atoi(numberStr);     

			if (sslLength > 0)
			{
                //set start of data to after the last new line
                char* data = lfToken + 1;

                //The check below is because sometimes a URC-message gets through and ruins everything because it includes 2 more lines that we don't expect
                //First cut off the message so we only check the first 30 characters
                
                //get first 30 characters
                char subbuff[30];
                memcpy( subbuff, _buffer, 30 );
                subbuff[30] = '\0';

                //A bad message can at a maximum look like this: +QSSLURC: "recv",1<LF><LF>+QSSLRECV: [number]<LF>
                //but sometimes only part of it is included, example: v",1<LF><LF>+ QSSLRECV:[number]<LF>
                //i check for 2 new lines before +QSSLRECV because if there only is 1 then the data will still be read
                if(strstr(subbuff, "\n\n+QSSLRE") != nullptr) {
                    if(readReply(1000, 2)) {
                        data = _buffer;
                    }
                    else {
                        QT_ERROR("Could not get data after URC-interrupt");

                        sslLength = 0;
                    }
                }

				memcpy(_readBuffer, data, sslLength);
			}
			QT_TRACE("available sslLength: %i", sslLength);
			return sslLength;
        }
    }
    else
    {
        sprintf(_buffer, "AT+QIRD=1,0");
        if (sendAndWaitForReply(_buffer, 1000, 3))
        {
            const char delimiter[] = ",";
            char * token = strtok(_buffer, delimiter);              
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
    }
    QT_COM_ERROR("Failed to read response");
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
    if (size == 0)
    {
        return 0;
    }
    if (useEncryption())
    {
        uint32_t length = size > sslLength ? sslLength : size;
        QT_COM_TRACE("Data len: %i", length);
        memcpy(buf, _readBuffer, length);
        buf[length] = '\0';
        QT_COM_TRACE_START(" <- ");
        QT_COM_TRACE_ASCII(buf, length);
        QT_COM_TRACE_END("");
        sslLength -= length;
        QT_COM_TRACE("Remaining len: %i", sslLength);
        if (sslLength > 0)
        {
            QT_COM_TRACE("Move %i, %i", length, sslLength);
            memcpy(_readBuffer, _readBuffer + length, sslLength);            
        }
        return length;
    }
    else
    {
        sprintf(_buffer, "AT+QIRD=1,%i", size);
        if (sendAndWaitForReply(_buffer, 1000, 1) &&
            strstr(_buffer, "+QIRD:"))
        {        
            // +QIRD: <len>
            // <data>
            //
            // OK
            char* token = strtok(_buffer, " ");
            token = strtok(nullptr, "\n");
            char* ptr;
            uint16_t length = strtol(token, &ptr, 10);
            QT_COM_TRACE("Data len: %i", length);

            _uart->readBytes(buf, length);
            buf[length] = '\0';
            QT_COM_TRACE_START(" <- ");
            QT_COM_TRACE_ASCII(_buffer, size);
            QT_COM_TRACE_END("");
            return length;       
        }
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
    sprintf(_command, "+Q%sCLOSE", useEncryption() ? _SSL_PREFIX : _INET_PREFIX);
    sprintf(_buffer, "AT%s=1,10", _command);
    if (!sendAndCheckReply(_buffer, _OK, 10000))
    {
        QT_ERROR("Failed to close connection");
    }
    uint32_t timeout = millis() + 20000;
    while (millis() < timeout)
    {
        sprintf(_command, "AT+Q%sSTATE=1,1", useEncryption() ? _SSL_PREFIX : _INET_PREFIX);
        sendAndWaitForReply(_command, 1000, 3);
        if (_buffer[0] == 'O' && _buffer[1] == 'K')
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
    //NOTE ON SSL:
    //UG96 has a bug where it returns instead of qsslstate on qsslstate requests
    //to mitigate this we have changed occurances of _command to QISTATE

    sprintf(_command, "Q%sSTATE", useEncryption() ? _SSL_PREFIX : _INET_PREFIX);
    sprintf(_buffer, "AT+%s=1,1", _command);
    if (sendAndWaitForReply(_buffer, 1000, 3) &&
        strstr(_buffer, "QISTATE")) 
    {
        char* tokenStart = strstr(_buffer, "QISTATE");
        tokenStart = &_buffer[tokenStart - _buffer];
        
        char* token = strtok(tokenStart, ",");
        token = strtok(nullptr, ",");
        token = strtok(nullptr, ",");
        token = strtok(nullptr, ",");
        token = strtok(nullptr, ",");
        token = strtok(nullptr, ",");

        QT_COM_TRACE("Socket State: %s, Connected: %s", token, strcmp(token, "3") == 0 ? "true" : "false");

        return strcmp(token, "3") == 0;        
    }
    return false;
}

bool QuectelCellular::activateSsl()
{
	if(!useEncryption()) {
		_encryption = TlsEncryption::Tls12; //Set to Tls12 if no other encryption is specified
	}
	
    sprintf(_command, "AT+QSSLCFG=\"sslversion\",1,%i", (uint8_t)_encryption);
    if (!sendAndCheckReply(_command, _OK, 10000))    // Set TLS
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
    return true;
}

bool QuectelCellular::useEncryption()
{
    return _encryption != TlsEncryption::None;
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
    sprintf(_buffer,"AT+QFOPEN=\"RAM:%s\",%i", fileName, overWrite ? 1 : 0);
    if (!sendAndWaitForReply(_buffer, 1000, 3))
    {
        QT_ERROR("Timeout opening file");
        return NOT_A_FILE_HANDLE;
    }
    char* token = strtok(_buffer, "+QFOPEN: ");
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
    sprintf(_buffer, "AT+QFREAD=%li,%lu", fileHandle, length);
    if (!sendAndCheckReply(_buffer, _CONNECT, 1000))
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
    sprintf(_buffer, "AT+QFWRITE=%li,%lu", fileHandle, length);
    if (sendAndCheckReply(_buffer, _CONNECT, 1000))
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
        return (strstr(_buffer, "+QFWRITE:") != nullptr);
    }
    return false;
}

bool QuectelCellular::seekFile(FILE_HANDLE fileHandle, uint32_t length)
{
    // AT+QFSEEK=3000,0,0
    // OK
    sprintf(_buffer,"AT+QFSEEK=%li,%lu,0", fileHandle, length);
    if (!sendAndCheckReply(_buffer, _OK, 1000))
    {
        QT_ERROR("Seek error: %s", _buffer);
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
    sprintf(_buffer, "AT+QFPOSITION=%li", fileHandle);
    if (!sendAndWaitForReply(_buffer, 1000, 3))
    {
        QT_ERROR("File position error: %s", _buffer);
        return -1;
    }
    char* token = strtok(_buffer, "+QFPOSITION: ");
    if (!token)
    {
        QT_ERROR("Get position error: %s", _buffer);
        return -1;
    }
    uint32_t result = atoi(token);
    return result;
}

bool QuectelCellular::truncateFile(FILE_HANDLE fileHandle)
{
    // AT+QFTUCAT=3000
    // OK
    sprintf(_buffer, "AT+QFTUCAT=%li", fileHandle);
    if (!sendAndCheckReply(_buffer, _OK, 1000))
    {
        QT_ERROR("Timeout deleting file: %s", _buffer);
        return false;
    }
    return checkResult();
}

bool QuectelCellular::closeFile(FILE_HANDLE fileHandle)
{
    // AT+QFCLOSE=3000
    // OK
    sprintf(_buffer, "AT+QFCLOSE=%li", fileHandle);
    if (!sendAndCheckReply(_buffer, _OK, 1000))
    {
        QT_ERROR("Timeout closing file: %s", _buffer);
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
    sprintf(_buffer, "AT+QFUPL=\"RAM:%s\",%lu", fileName, length);
    if (!sendAndWaitForReply(_buffer, 1000, 2))
    {
        QT_ERROR("No response to upload command");
        return false;        
    }
    if (!strstr(_buffer, "CONNECT"))
    {
        QT_ERROR(_buffer);
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
    sprintf(_buffer, "AT+QFDWL=\"RAM:%s\",%lu", fileName, length);
    if (!sendAndWaitForReply(_buffer, 1000, 2))
    {
        QT_ERROR("No response to download command");
        return false;        
    }
    if (!strstr(_buffer, "CONNECT"))
    {
        QT_ERROR(_buffer);
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
    return (strstr(_buffer, "+QFDWL:") != nullptr);
}

uint32_t QuectelCellular::getFileSize(const char* fileName)
{    
    // AT+QFLST:"RAM:file.txt"
    // +QFLST:"RAM:file.txt"",734"
    // 
    // OK
    sprintf(_buffer, "AT+QFLST=\"RAM:%s\"", fileName);
    if (!sendAndWaitForReply(_buffer, 1000, 2))
    {
        QT_ERROR("Get file size error 1: %s", _buffer);
        return -1;
    }
    char* token = strtok(_buffer, "+QFLST: ");
    if (!token)
    {
        QT_ERROR("Get file size error: %s", _buffer);
        return -1;
    }
    token = strtok(nullptr, ",");
    token = strtok(nullptr, "\n");
    uint32_t result = atoi(token);
    return result;
}

bool QuectelCellular::deleteFile(const char* fileName)
{
    // AT+QFDEL:"RAM:file.txt"
    // OK
    sprintf(_buffer, "AT+QFDEL=\"RAM:%s\"", fileName);
    if (!sendAndCheckReply(_buffer, _OK, 1000))
    {
        QT_ERROR("Timeout deleting file: %s", _buffer);
        return false;
    }
    return checkResult();
}

// Private

bool QuectelCellular::setPower(bool state)
{
    uint32_t timeout;
	QT_DEBUG("setPower: %i", state);
    if (state == true)
    {
        if (_powerPin != NOT_A_PIN)
        {
            digitalWrite(_powerPin, LOW);
            delay(300);
            digitalWrite(_powerPin, HIGH);
        }

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
            if (sendAndCheckReply(_AT, "AT", 1000))
            {
                QT_COM_TRACE("GOT AT");
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
    }
    else
    {
        if (!getStatus())
        {
            QT_COM_TRACE("Module already off");
            return true;
        }
        QT_DEBUG("Powering down module");        
        // First check if already awake
        timeout = 5000;
        while (timeout > 0) 
        {
            flush();
            if (sendAndCheckReply(_AT, "OK", 1000))
            {   
                QT_COM_TRACE("GOT AT");
                break;
            }
            if (sendAndCheckReply(_AT, "OK", 1000))
            {   
                QT_COM_TRACE("GOT OK");
                break;
            }            
            callWatchdog();
            delay(500);
            timeout -= 500;
        }
        sendAndCheckReply("ATE0", _OK, 1000);
		
		if (!sendAndCheckReply("AT+QCFG=\"urc/port\",1,\"uart1\"", _OK))
		{
			QT_ERROR("Could not start urc messages");
			return false;
		}

        if (!sendAndCheckReply("AT+QPOWD=1", _OK, 10000))
        {
            return false;
        }
        timeout = millis() + 60000;  // max 60 seconds for a shutdown
        while (timeout > millis())
        {
            if (readReply(1000, 1))
            {
                if (strstr(_buffer, "+QIURC: \"pdpdeact\",1"))
                {
                    QT_DEBUG("PDP deactivated");
                }
                if (strstr(_buffer, "POWERED DOWN"))
                {
                    QT_DEBUG("Module powered down");
                    break;
                }
            }
            callWatchdog();
        }
        return false;
    }
    return true;
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

bool QuectelCellular::sendAndWaitFor(const char* command, const char* reply, uint16_t timeout)
{
    uint16_t index = 0;

    flush();
	QT_COM_TRACE(" -> %s", command);
    _uart->println(command);
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
            if (c == '\n' && index == 0)
            {
                // Ignore first \n.
                continue;
            }
            _buffer[index++] = c;
        }

        if (strstr(_buffer, reply))
        {
            QT_COM_TRACE("Match found");
            break;
        }
        if (timeout <= 0)
        {
            QT_COM_TRACE_START(" <- (Timeout) ");
            QT_COM_TRACE_ASCII(_buffer, index);
            QT_COM_TRACE_END("");
            return false;
        }
        callWatchdog();
        delay(1);
    }
    _buffer[index] = 0;
    QT_COM_TRACE_START(" <- ");
    QT_COM_TRACE_ASCII(_buffer, index);
    QT_COM_TRACE_END("");
    return true;
}

bool QuectelCellular::sendAndCheckReply(const char* command, const char* reply, uint16_t timeout)
{
    sendAndWaitForReply(command, timeout);
    return (strstr(_buffer, reply) != nullptr);
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
            if (c == '\n' && index == 0)
            {
                // Ignore first \n.
                continue;
            }
            _buffer[index++] = c;
            if (c == '\n')
            {
				linesFound++;
            }
    		if (linesFound >= lines)
	    	{
    			break;
	    	}            
        }

   		if (linesFound >=lines)
    	{
   			break;
    	}            

        if (timeout <= 0)
        {
            QT_COM_TRACE_START(" <- (Timeout) ");
            QT_COM_TRACE_ASCII(_buffer, index);
            QT_COM_TRACE_END("");
            return false;
        }
        callWatchdog();
        delay(1);
    }
    _buffer[index] = 0;
    QT_COM_TRACE_START(" <- ");
    QT_COM_TRACE_ASCII(_buffer, index);
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
    char* token = strstr(_buffer, _OK);
    if (token)
    {
        //QT_TRACE("*OK - %s", _buffer);
        _lastError = 0;
        return true;
    }
    token = strstr(_buffer, _CME_ERROR);
    if (!token)
    {
        //QT_TRACE("*NO CME ERROR: %s", _buffer);
        _lastError = -1;
        return false;
    }
    //QT_TRACE("*CME ERROR: %s", _buffer);
    token = strtok(_buffer, _CME_ERROR);
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



