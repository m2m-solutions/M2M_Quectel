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
#ifndef __M2M_Quectel_h__
#define __M2M_Quectel_h__
#include "Arduino.h"
#include <SPI.h>
#include <Ethernet.h>

#define NOT_A_PIN   -1
#define FLASHSTR	__FlashStringHelper*
#define M2M_QUECTEL_DEBUG
#define M2M_QUECTEL_COM_DEBUG


#ifdef M2M_QUECTEL_DEBUG
// need to do some debugging...
#define DEBUG_PRINT(...)		if (_debugStream != nullptr) _debugStream->print(__VA_ARGS__)
#define DEBUG_PRINTLN(...)		if (_debugStream != nullptr) _debugStream->println(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#define DEBUG_PRINTLN(...)
#endif
#ifdef M2M_QUECTEL_COM_DEBUG
// need to do some debugging...
#define COM_DEBUG_PRINT(...)		if (_debugStream != nullptr) _debugStream->print(__VA_ARGS__)
#define COM_DEBUG_PRINTLN(...)		if (_debugStream != nullptr) _debugStream->println(__VA_ARGS__)
#else
#define COM_DEBUG_PRINT(...)
#define COM_DEBUG_PRINTLN(...)
#endif

enum class QuectelModule : uint8_t
{
	UG96 = 0,
	BG96,
	M95
};

enum class NetworkRegistrationState : uint8_t
{
    NotRegistered = 0,
    Registered,
    Searching,
    Denied,
    Unknown,
    Roaming
};

#define WATCHDOG_CALLBACK_SIGNATURE void (*watchdogcallback)()

class QuectelCellular : public Client
{
public:
    QuectelCellular(Print* debugStream = nullptr, int8_t powerPin = NOT_A_PIN, int8_t statusPin = NOT_A_PIN);

    bool begin(Uart* uart);

	bool setPower(bool state);
    bool getStatus();    

	const char* getFirmwareVersion();
    uint8_t getIMEI(char* buffer);
    uint8_t getOperatorName(char* buffer);
    NetworkRegistrationState getNetworkRegistration();
    uint8_t getRSSI();
    uint8_t getSIMCCID(char* buffer);
    double getVoltage();

    bool connectNetwork(const char* apn, const char* userid, const char* password);
    bool disconnectNetwork();

    // Client interface
    int connect(IPAddress ip, uint16_t port);
    int connect(const char *host, uint16_t port);
    size_t write(uint8_t);
    size_t write(const uint8_t *buf, size_t size);
    int available();
    int read();
    int read(uint8_t *buf, size_t size);
    int peek();
    void flush();
    void stop();
    uint8_t connected();
    operator bool()
    {
        return connected();
    }

	void setDebugStream(Print* print);

    void setWatchdogCallback(WATCHDOG_CALLBACK_SIGNATURE);

private:
	bool sendAndWaitForReply(const char* command, uint16_t timeout = 1000, uint8_t lines = 1);
	bool sendAndWaitForMultilineReply(const char* command, uint8_t lines, uint16_t timeout = 1000);
	bool sendAndCheckReply(const char* command, const char* reply, uint16_t timeout = 1000);
    bool readReply(uint16_t timeout = 1000, uint8_t lines = 1);
    void callWatchdog();

    int8_t _powerPin;
    int8_t _statusPin;
    Uart* _uart;
    Print* _debugStream;
    char _replyBuffer[255];
	QuectelModule _moduleType;
	char _firmwareVersion[20];
    WATCHDOG_CALLBACK_SIGNATURE;

    //__FlashStringHelper* _apn;
    //__FlashStringHelper* _apnusername;
    //__FlashStringHelper* _apnpassword;
    boolean httpsredirect;
    const char* _useragent = "PP";
	const char* _AT = "AT";
    const char* _OK = "OK";
    const char* _ERROR = "ERROR";
};

#endif