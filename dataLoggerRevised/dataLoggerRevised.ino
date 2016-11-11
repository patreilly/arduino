/*
 * Simple data logger.
 */
#include <SPI.h>
#include "SdFat.h"
#include <TimeLib.h>
#include <SoftwareSerial.h>
// SD chip select pin.  Be sure to disable any other SPI devices such as Enet.
const uint8_t chipSelect = 8;

// Interval between data records in milliseconds.
// The interval must be greater than the maximum SD write latency plus the
// time to acquire and write data to the SD to avoid overrun errors.
// Run the bench example to check the quality of your SD card.
const uint32_t SAMPLE_INTERVAL_MS = 1000;

// Log file base name.  Must be six characters or less.
#define FILE_BASE_NAME "Data"

#define TIME_HEADER  "T"   // Header tag for serial time sync message
#define TIME_REQUEST  7    // ASCII bell character requests a time sync message 
/* to communicate with the Bluetooth module's TXD pin */
#define BT_SERIAL_TX 10
/* to communicate with the Bluetooth module's RXD pin */
#define BT_SERIAL_RX 11
/* Initialise the software serial port */
SoftwareSerial BluetoothSerial(BT_SERIAL_TX, BT_SERIAL_RX);
//------------------------------------------------------------------------------
// File system object.
SdFat sd;

// Log file.
SdFile file;

// Time in micros for next data record.
uint32_t logTime;

//temperature stuff
//float tempC;
//int reading;
int tempPin = 0; //sensore on analog pin A0
//int offset = 10; //millivolt scale factor

//==============================================================================
// User functions.  Edit writeHeader() and logData() for your requirements.

const uint8_t ANALOG_COUNT = 4;
//------------------------------------------------------------------------------
// Write data header.
void writeHeader() {
  file.print(F("micros"));
  for (uint8_t i = 0; i < ANALOG_COUNT; i++) {
    file.print(F(",adc"));
    file.print(i, DEC);
  }
  file.println();
}

//write date and temp header
void writeDateTempHeader() {
  file.print(F("DateTime;TempCelsuis;"));
  file.println();
}
//------------------------------------------------------------------------------
// Log a data record.
void logData() {
  uint16_t data[ANALOG_COUNT];

  // Read all channels to avoid SD write latency between readings.
  for (uint8_t i = 0; i < ANALOG_COUNT; i++) {
    data[i] = analogRead(i);
  }
  // Write data to file.  Start with log time in micros.
  file.print(logTime);

  // Write ADC data to CSV record.
  for (uint8_t i = 0; i < ANALOG_COUNT; i++) {
    file.write(',');
    file.print(data[i]);
  }
  file.println();
}

//log date
// Log a data record.
void logDateAndTemp() {
  String data = padZeroes(hour()) + ":" + padZeroes(minute()) + ":" + padZeroes(second()) + " " + 
  year() + "-" + padZeroes(month()) + "-" +  padZeroes(day()) + ";" + returnTempC(analogRead(tempPin)) + ";";
  file.print(data);
  file.println();
}

float returnTempC(float reading){
  //return celcius
  return (reading / 9.31);
}

//pad zeroes for dates
String padZeroes(int digits){
  String number = String(digits);
  if(digits<10){
    number = '0' + number;
  }
  return number;
}
void processSyncMessage() {
  unsigned long pctime;
  const unsigned long DEFAULT_TIME = 1357041600; // Jan 1 2013

  if(Serial.find(TIME_HEADER)) {
     pctime = Serial.parseInt();
     if( pctime >= DEFAULT_TIME) { // check the integer is a valid time (greater than Jan 1 2013)
       setTime(pctime); // Sync Arduino clock to the time received on the serial port
     }
  }
}

time_t requestSync()
{
  Serial.write(TIME_REQUEST);  
  return 0; // the time will be sent later in response to serial mesg
}

//==============================================================================
// Error messages stored in flash.
#define error(msg) sd.errorHalt(F(msg))
//------------------------------------------------------------------------------
void setup() {
  const uint8_t BASE_NAME_SIZE = sizeof(FILE_BASE_NAME) - 1;
  char fileName[13] = FILE_BASE_NAME "00.csv";
  analogReference(INTERNAL);
  Serial.begin(9600);
  /* Set the baud rate for the software serial port */
  BluetoothSerial.begin(9600);
  
  // Wait for USB Serial 
  while (!Serial) {
    SysCall::yield();
  }
  delay(1000);

//---Bluetooth init---------------------------------------------------
// Should respond with OK
BluetoothSerial.print("AT");
waitForResponse();

// Should respond with its version
BluetoothSerial.print("AT+VERSION");
waitForResponse();

// Set pin to 1234
BluetoothSerial.print("AT+PIN1234");
waitForResponse();

// Set the name to BLU
BluetoothSerial.print("AT+NAMEBLU");
waitForResponse();

// Set baudrate from 9600 (default) to 57600
// * Note of warning * - many people report issues after increasing JY-MCU
// baud rate upwards from the default 9,600bps rate (e.g. 'AT+BAUD4')
// so you may want to leave this and not alter the speed!!
BluetoothSerial.print("AT+BAUD7");
waitForResponse();

Serial.println("Finished!");
//--end bluetooth init------------------------------------------------------

  Serial.println(F("Click sync to start"));
  while (!Serial.available()) {
    SysCall::yield();
  }
  
  // Initialize the SD card at SPI_HALF_SPEED to avoid bus errors with
  // breadboards.  use SPI_FULL_SPEED for better performance.
  if (!sd.begin(chipSelect, SPI_HALF_SPEED)) {
    sd.initErrorHalt();
  }

  // Find an unused file name.
  if (BASE_NAME_SIZE > 6) {
    error("FILE_BASE_NAME too long");
  }
  while (sd.exists(fileName)) {
    if (fileName[BASE_NAME_SIZE + 1] != '9') {
      fileName[BASE_NAME_SIZE + 1]++;
    } else if (fileName[BASE_NAME_SIZE] != '9') {
      fileName[BASE_NAME_SIZE + 1] = '0';
      fileName[BASE_NAME_SIZE]++;
    } else {
      error("Can't create file name");
    }
  }
  if (!file.open(fileName, O_CREAT | O_WRITE | O_EXCL)) {
    error("file.open");
  }
  // Read any Serial data.
  do {
    processSyncMessage();
    delay(10);
  } while (Serial.available() && Serial.read() >= 0);

  Serial.print(F("Logging to: "));
  Serial.println(fileName);
  Serial.println(F("Type any character to stop"));

  // Write data header.
  //writeHeader(); //uncomment later
  writeDateTempHeader();

  // Start on a multiple of the sample interval.
  logTime = micros()/(1000UL*SAMPLE_INTERVAL_MS) + 1;
  logTime *= 1000UL*SAMPLE_INTERVAL_MS;
}
//------------------------------------------------------------------------------
void loop() {
  /*
  // Time for next record.
  logTime += 1000UL*SAMPLE_INTERVAL_MS;

  // Wait for log time.
  int32_t diff;
  do {
    diff = micros() - logTime;
  } while (diff < 0);

  // Check for data rate too high.
  if (diff > 10) {
    error("Missed data record");
  }

logData();
*/
if (timeStatus()!= timeNotSet) {
  logDateAndTemp();  
}
delay(1000);

  // Force data to SD and update the directory entry to avoid data loss.
  if (!file.sync() || file.getWriteError()) {
    error("write error");
  }

  if (Serial.available()) {
    // Close file and stop.
    file.close();
    Serial.println(F("Done"));
    SysCall::halt();
  }
}


