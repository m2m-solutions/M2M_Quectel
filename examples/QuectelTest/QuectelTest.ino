/---------------------------------------------------------------------------------------------
//
// PurplePoint Hertz Test sketch
//
// Copyright 2017, M2M Solutions AB
// Jonny Bergdahl, 2017-09-21
//
//---------------------------------------------------------------------------------------------
// TODO: 
//---------------------------------------------------------------------------------------------
//
////////////////////////////////////////////////////////////////////////////////////////////////
//
// Project configuration defines
//
#define serial	 		SerialUSB
#define cellular		Serial
#define APN           	"internet.cxn"

////////////////////////////////////////////////////////////////////////////////////////////////
//
// Includes
//
#include "M2M_Quectel.h"

////////////////////////////////////////////////////////////////////////////////////////////////
//
// Global variables
//
QuectelCellular quectel(&serial, CM_PWRKEY);

////////////////////////////////////////////////////////////////////////////////////////////////
//
// Code
//
void setup() 
{
	serial.println("Quectel test sketch");
	serial.println("===================");
	serial.println("Initializing module");
	quectel.begin(&cellular);
  	serial.print("Firmware: ");
  	serial.println(quectel.getFirmwareVersion());
	serial.print("RSSI: ");	
 	serial.println(quectel.getRSSI());
	serial.print("ICCID: ");
  	serial.println(quectel.getSIMCCID(buffer));
	serial.print("IMEI: ");
	serial.print(quectel.getIMEI(buffer));

	serial.println("Connecting to network");
	quectel.connectNetwork(APN,"","");

	serial.println("GET www.google.se");
	quectel.httpGet("http://www.google.se");
}

void loop()
{
	delay(1000);	
	serial.print("RSSI: ");	
 	serial.println(quectel.getRSSI());
}