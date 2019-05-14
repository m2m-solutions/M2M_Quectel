//---------------------------------------------------------------------------------------------
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
#define serial      SerialUSB
#define cellular    SerialCellular
#define APN             "m2m.cxn"

#define PWRKEY CM_PWRKEY
#define STATUS CM_STATUS

////////////////////////////////////////////////////////////////////////////////////////////////
//
// Includes
//
#include "M2M_Quectel.h"
#include "M2M_Logger.h"

////////////////////////////////////////////////////////////////////////////////////////////////
//
// Global variables
//
QuectelCellular quectel(PWRKEY, STATUS);
Logger logger;
char buffer[128];

////////////////////////////////////////////////////////////////////////////////////////////////
//
// Code
//
void setup() 
{
  while (!serial);
  serial.begin(115200);
  logger.begin(&serial, LogLevel::Trace);
  logger.setIncludeTimestamp(true);
  logger.setIncludeLogLevel(true);
  
  serial.println("Quectel test sketch");
  serial.println("===================");
  
  serial.println("Initializing module");
  quectel.setLogger(&logger);
  quectel.begin(&cellular);

    serial.print("Firmware: ");
    serial.println(quectel.getFirmwareVersion());
  serial.print("RSSI: "); 
  serial.println(quectel.getRSSI());
  serial.print("ICCID: ");
    serial.println(quectel.getSIMCCID(buffer));
  serial.print("IMEI: ");
  serial.println(quectel.getIMEI(buffer));

  serial.println("Connecting to network");
  quectel.connectNetwork(APN,"","");

  serial.println("GET site");
  bool getResult = quectel.httpGet("https://poc.logtrade.info/", "poc.htm");
  serial.println(getResult ? "Download complete" : "FAILED!");

  const char* fileName = "file.txt";
  const char* fileText = "1234567890";

  int length = strlen(fileText);
  serial.println("Creating file");
  FILE_HANDLE handle = quectel.openFile(fileName);
  checkResult("Write file", quectel.writeFile(handle, (const uint8_t*)fileText, length));
  
  uint32_t position;
  position = quectel.getFilePosition(handle);
  serial.print("Position: ");
  serial.println(position);
  checkResult("Write file", quectel.writeFile(handle, (const uint8_t*)fileText, length));
  checkResult("Close file", quectel.closeFile(handle));
  uint32_t size = quectel.getFileSize(fileName);
  serial.print("File size: ");
  Serial.print(size);
  serial.println("");

  char readBuffer[128];
  handle = quectel.openFile("poc.htm");
  serial.println(handle);
  checkResult("Seek", quectel.seekFile(handle, position -1));
  serial.print("Position: ");
  checkResult("Read file", quectel.readFile(handle, (uint8_t*)&readBuffer, position));
  readBuffer[position] = 0;
  serial.print("Content: ");
  serial.println(readBuffer);
  checkResult("Close file", quectel.closeFile(handle));
  //checkResult("Delete file", quectel.deleteFile(fileName));  
}

void loop()
{
  delay(30000); 
  serial.print("RSSI: "); 
  serial.println(quectel.getRSSI());
}

void checkResult(char * prefix, bool result)
{
  serial.print(prefix);
  serial.print(" - ");
  if (result)
  {
    serial.println("Success");
  }
  else
  {
    serial.print("Fail: ");
    serial.println(quectel.getLastError());
  } 
  if (cellular.available())
  {
    serial.println("ERROR - CHARS AVAILABLE!");
  }
}
