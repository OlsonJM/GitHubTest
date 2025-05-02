/*
   SEL Data Recorder
    by James M. Olson, 2022

    Compatible with:
      SEL751
      SEL751A

*/

#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTC_SAMD21.h>


//#define DEBUG           //Comment this line out to shut off USB serial
#define METER_STREAM    //comment this line out to stop serial stream of file data

#define BUTTON_A  0       //A-button pin on OLED
#define BUTTON_B  1       //B-button pin on OLED
#define BUTTON_C  2       //C-button pin on OLED
#define WIRE Wire         //OLED library interface

#define chipSelect 3      //SD Card select pin
//#define SD_CARD_STATUS 7  //input pint for sd card status
//#define ONBOARD_LED 13    //logger on-board RED LED
//#define SD_GREEN_LED 8    //SD Card on-board Green LED
#define MAX_LINES 25      //Maxiumum rows of data to recieve in Serial message
#define NUM_CHARS 120     //Maximum number of characters in each row of Serial message
#define MAX_MSG_COUNTER 500000 //maximum message count to show on display
#define MENU_TIMEOUT 30   //seconds until display goes to sleep
#define COMM_TIMEOUT 1    //seconds until loss of communication with SEL is flagged.    
#define MIN_MSG_LINES 10  //minimum number of acceptable lines in SEL message response    

//Required for OLED display
Adafruit_SSD1306 display = Adafruit_SSD1306(128, 32, &WIRE);

char receivedChars[MAX_LINES][NUM_CHARS];  // [ROWS][COLUMNS] - Serial recieve buffer

//Global Variables
bool newData;             //Flag set when new Serial message has arrived from SEL
int lineCtr;              //Counts number of lines of Serial data recieved
bool recvInProgress;      //Serial data is coming in from SEL
bool startRecording;      //Record serial data to SD Card
bool buttonPressDetect;   //determine if button has been pushed
bool recordingMenu;       //flag to state in-recording menu is being displayed.
bool baudRateMenu;        //flag to display baud rate menu
unsigned long selMsgs;    //number of messages recieved
unsigned int baudRates[] = {2400, 4800, 9600, 19200, 38400};
String sampleRates[] = {"5 SEC", "10 SEC", "30 SEC", "1 MIN", "5 MIN", "10 MIN", "30 MIN", "1 HOUR", "2 HOURS", "4 HOURS"};
int sampleRateSec[] = {5, 10, 30, 60, 300, 600, 1800, 3600, 7200, 14400};
int sampleRateIndex;
unsigned long nextSampleSeconds;
unsigned long lossOfCommTime;
bool sleepDisplay;
int storedDataCounter;
bool lossOfComm;
bool headerWritten;
unsigned int logFileCounter;
String relayType;
String currentLogFilename;
String filebuffer;

//Set up internal RTC
RTC_SAMD21 rtc;
/* Set inital data to irrelevant value. just reference for timed readings */
const byte seconds = 0;
const byte minutes = 0;
const byte hours = 0;
const byte day = 1;
const byte month = 1;
const byte year = 00;

char baudRateIndex;
//Sd2Card card;
//SdVolume volume;
//SdFile root;
bool SD_CardReady;
bool SD_CardInserted;


#ifdef DEBUG
  unsigned int messageCount;//number of messages recieved
  unsigned int loginCount;  //Number of login attempts
#endif

void setup() {

#if defined(DEBUG) || defined(METER_STREAM)
  Serial.begin(38400);
  //while (!Serial) {
  // ; // wait for serial port to connect. Needed for native USB port only
  //}
  Serial.flush();
  while (Serial.available() > 0)
    Serial.read();
#endif

  // Initialize RTC to fixed value just for time reference
  rtc.begin();
  rtc.setTime(hours, minutes, seconds);
  rtc.setDate(day, month, year);
  rtc.

  //Initialize global variables
  newData = false;
  lineCtr = 0;
  recvInProgress = false;
  startRecording = false;
  buttonPressDetect = false;
  recordingMenu = false;
  baudRateMenu = false;
  SD_CardReady = false;
  SD_CardInserted = false;
  sleepDisplay = false;
  sampleRateIndex = 0;
  lossOfComm = false;
  headerWritten = false;
  baudRateIndex = 4;  //default to 38400 baud
  selMsgs = 0;
  storedDataCounter = 0;
  logFileCounter = 0;
  relayType="";
  currentLogFilename="";
  filebuffer = "";

  //initalize port 1 at default 38400 baud & flush buffer
  Serial1.begin(baudRates[baudRateIndex]);
  Serial1.flush();
  while (Serial1.available() > 0)
    Serial1.read();


#ifdef DEBUG
  messageCount = 0;
  loginCount = 0;
#endif

  //Set digital pin modes
  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLUP);
  pinMode(BUTTON_C, INPUT_PULLUP);
  //pinMode(SD_CARD_STATUS, INPUT_PULLUP);
  //pinMode(ONBOARD_LED, OUTPUT);
  //pinMode(SD_GREEN_LED, OUTPUT);

  //Attach interrupts for button presses
  //attachInterrupt(digitalPinToInterrupt(SD_CARD_STATUS), INT_SDcardRemoved, FALLING);

  //OLED Display configuration
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // Address 0x3C for 128x32
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.display(); // actually display all of the above

  //Setup SD Card
  //SD_CardInserted = digitalRead(SD_CARD_STATUS);

  //if(SD_CardInserted)
  //{
    if (!SD.begin(chipSelect)) {
      #ifdef DEBUG
          Serial.println("SD initialization failed during setup. ");
      #endif
      SD_CardReady = false;
    } else
      SD_CardReady = true;
 // }

}

void loop() {

  //look for menu button press to wake up screen
  if ((!digitalRead(BUTTON_A) || !digitalRead(BUTTON_B) || !digitalRead(BUTTON_C)) && sleepDisplay)
  {
    //wake up the OLED display
    sleepDisplay = false;

  }

  //entry to main loop. start by displaying main menu
  if (!startRecording && !sleepDisplay)
  {
    menuScreen();
    if (headerWritten == true)
      headerWritten = false;
  }
  else if (startRecording && SD_CardInserted)
  {
    //send meter command if time for a new measurement
    if ((rtc.getY2kEpoch() >= nextSampleSeconds) && !recvInProgress)
    {
      nextSampleSeconds = rtc.getY2kEpoch() + sampleRateSec[sampleRateIndex];
      sendMeterCommand();

    }

    //Serial TX/RX loop
    recvWithStartEndMarkers();

    if (!recvInProgress ) //||  lossOfComm)
    {
      //Format serial data && write to SD Card
      if(processNewSerialData() && newData);
      {
        if(processSELmeterData())
          writeDataToSD();
      }

      //look for menu button press
      if (!digitalRead(BUTTON_A) || !digitalRead(BUTTON_B) || !digitalRead(BUTTON_C))
        sleepDisplay = false;

      if (!sleepDisplay)
        showRecordingMenu();
    }
  }

}

/*
void SD_Removed_Shutdown()
{
  //stop recording if sd card is removed
  SD_CardInserted = false;
  SD_CardReady = false;
  startRecording = false;
  newData = false;
  lineCtr = 0;
  memset(receivedChars,0,sizeof receivedChars);     //clear the recieved charater buffer
}
*/

void initSerial1port()
{
  Serial1.end();
  Serial1.begin(baudRates[baudRateIndex]);
  Serial1.setTimeout(500);
  //clear input buffer
  while (Serial1.available() > 0)
    Serial1.read();
}


void showRecordingMenu()
{
  //bool SD_shutdown = false;
  //if(!digitalRead(SD_CARD_STATUS))
  //{
    //wait and check again before shutting down
  //  delay(20);
  //if(!digitalRead(SD_CARD_STATUS))
   //   SD_shutdown = true;
  //}
    
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("SEL DATA RECORDER");
  display.print("Logger Count: ");
  display.println(selMsgs);
  if (lossOfComm)
    display.println(" - SEL COMM LOST -");
  else
    display.println();
  display.println("A - STOP RECORDER");
  display.display(); // actually display all of the above

  if (!digitalRead(BUTTON_A))//||SD_shutdown)
  {
    delay(50);
    while (!digitalRead(BUTTON_A));   //Wait here untill button is released
    startRecording = false;
    memset(receivedChars,0,sizeof receivedChars);     //clear the recieved charater buffer
    lineCtr = 0;
    delay(100);
  }
  delay(100);
}

void menuScreen()
{
  while (digitalRead(BUTTON_A) && digitalRead(BUTTON_B) && digitalRead(BUTTON_C))
  {
    //SD_CardInserted = digitalRead(SD_CARD_STATUS);
    if (!SD_CardReady)
        SD_CardReady = SD.begin(chipSelect);

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("SEL DATA RECORDER");
    display.print("A - BAUD  : ");
    display.println(baudRates[baudRateIndex]);

    display.print("B - LOGGER: ");
    display.println(sampleRates[sampleRateIndex]);

    if (SD_CardReady)
    {
      display.println("C - START RECORDER");
    } else
      display.println(" - Insert SD Card -");

    display.display(); // display all of the above
 
    delay(150);
  }      //wait here until button is pressed

#ifdef DEBUG
  Serial.println("Button push detected from Main Menu");
#endif

  //Set Baud Rate Menu
  if (!digitalRead(BUTTON_A))
  {
    delay(50);
    while (!digitalRead(BUTTON_A)); //stay here until button is released
    displayBaudRateMenu();
    while (!digitalRead(BUTTON_C));
    
  } else if (!digitalRead(BUTTON_B))
  {
    delay(50);
    while (!digitalRead(BUTTON_B)); //stay here until button is released
    displaySampleRateMenu();
    while (!digitalRead(BUTTON_C));
    //buttonPressDetect = false;
  } else if (!digitalRead(BUTTON_C) && SD_CardInserted)
  {
    delay(50);
    while (!digitalRead(BUTTON_C)); //stay here until button is released
    initSerial1port();
    nextSampleSeconds = 0;  //first measurement will occur immediately.
    startRecording = true;
    recordingMenu = true;
    selMsgs = 0;    
  }
  delay(100);
}

void displaySampleRateMenu()
{
  //buttonPressDetect = false;
  int numofSampleRates = sizeof(sampleRates) / sizeof(sampleRates[0]);
  bool exitMenu = false;

  while (!exitMenu)
  {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Sample Rate = ");
    display.println(sampleRates[sampleRateIndex]);
    display.println("A - INCREASE");
    display.println("B - DECREASE");
    display.println("C - RETURN TO MENU");
    display.display();
    while (digitalRead(BUTTON_A) && digitalRead(BUTTON_B) && digitalRead(BUTTON_C))
    {
      delay(150);
    } ;//wait for button to be depressed.

    if (!digitalRead(BUTTON_A))
    {
      delay(50);
      while (!digitalRead(BUTTON_A)); //stay here until button is released
      if (sampleRateIndex < (numofSampleRates - 1)) {
        sampleRateIndex++;
      }
    } else if (!digitalRead(BUTTON_B))
    {
      delay(50);
      while (!digitalRead(BUTTON_B)); //stay here until button is released
      if (sampleRateIndex > 0)
        sampleRateIndex--;
    } else if (!digitalRead(BUTTON_C))
    {
      delay(50);
      while (!digitalRead(BUTTON_C)); //stay here until button is released
      exitMenu = true;
    }
  }
  delay(100);
}

void displayBaudRateMenu()
{
  buttonPressDetect = false;
  int numofBaudRates = sizeof(baudRates) / sizeof(baudRates[0]);
  bool exitMenu = false;

  while (!exitMenu)
  {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("SEL BR = ");
    display.println(baudRates[baudRateIndex]);
    display.println("A - INCREASE");
    display.println("B - DECREASE");
    display.println("C - RETURN TO MENU");
    display.display();
    while (digitalRead(BUTTON_A) && digitalRead(BUTTON_B) && digitalRead(BUTTON_C))
    {
      delay(150);
    } ;//wait for button to be depressed.

    if (!digitalRead(BUTTON_A))
    {
      delay(50);
      while (!digitalRead(BUTTON_A)); //stay here until button is released
      if (baudRateIndex < (numofBaudRates - 1)) {
        baudRateIndex++;
      }
    } else if (!digitalRead(BUTTON_B))
    {
      delay(50);
      while (!digitalRead(BUTTON_B)); //stay here until button is released
      if (baudRateIndex > 0)
        baudRateIndex--;
    } else if (!digitalRead(BUTTON_C))
    {
      delay(50);
      while (!digitalRead(BUTTON_C)); //stay here until button is released
      exitMenu = true;
    }
  }
  delay(100);
}

void recvWithStartEndMarkers() {

  static byte ndx = 0;
  char startMarker = 2;
  char endMarker = 3;
  char rc;

  if(!digitalRead(BUTTON_A))
    {
      startRecording = false;
      recvInProgress = false;
      startRecording = false;
      lineCtr = 0;
      newData = false;
      while(!digitalRead(BUTTON_A));
      return;
    }

  /*
      if (recvInProgress == true && ((rtc.getY2kEpoch()-lossOfCommTime)>COMM_TIMEOUT))
      {
        #ifdef DEBUG
          if(!lossOfComm)
          {
            Serial.println("LOSS OF COMM WITH SEL");
            Serial.print("Delay = ");
            Serial.println((rtc.getY2kEpoch()-lossOfCommTime));
          }
        #endif
          lossOfComm = true;

      }else
          lossOfComm = false;
  */

  while (Serial1.available() > 0) {
    //digitalWrite(ONBOARD_LED, HIGH);
    if(!digitalRead(BUTTON_A))
    {
      startRecording = false;
      recvInProgress = false;
      startRecording = false;
      lineCtr = 0;
      newData = false;
      while(!digitalRead(BUTTON_A));
      return;
    }

    rc = Serial1.read();
    delayMicroseconds(250);

    //lossOfCommTime = rtc.getY2kEpoch();

    if (recvInProgress == true) {
      if (rc != endMarker) {
        if ((rc == 10 || rc == 13))
        {
          if (ndx != 0)
          {

            ndx = 0;
            //increase line count if not a empty line
            if (strlen(receivedChars[lineCtr]) > 0)
            {
              newData = true;
              lineCtr++;
            }
          }//end if(ndx!=0)
        } else  //Character is not a new line or carriage return
        {
          if (rc > 31) //Only add non symbol characters
          {
            receivedChars[lineCtr][ndx] = rc;
            receivedChars[lineCtr][ndx + 1] = '\0'; //add null char to new end of string
            ndx++;
            if (ndx >= NUM_CHARS) {
              #ifdef DEBUG
                Serial.println("MaxLineLength Hit!");
              #endif
              ndx = NUM_CHARS - 1;
            }
          } // end if not null
        }// end character is not NL or CR
      } else //end marker detected
      {
        //Serial.println("End Marker Detected");

        recvInProgress = false;
        ndx = 0;

        //check for default terminal pre login "="
        if (lineCtr == 0 && strlen(receivedChars[0]) > 0)
          newData = true;

      } //end if end marker detected
    } else if (rc == startMarker)
    {
      recvInProgress = true;
    } //end start marker detected
  } //end serial read loop
  //digitalWrite(ONBOARD_LED, LOW);
}// end main loop

bool processNewSerialData() {

  if (newData == true && recvInProgress == false) 
  {
      #ifdef DEBUG
          messageCount++;
          for (int i = 0; i <= lineCtr; i++)
            Serial.println(receivedChars[i]);
      #endif

    //Not logged on - Log on to relay at level 1
    if (lineCtr <= 2 && strstr(receivedChars[0], "Invalid Access Level")){
      login2sel();
      newData = false;
      lineCtr = 0;
      memset(receivedChars,0,sizeof receivedChars);     //clear the recieved charater buffer
      return false;
    }else if (lineCtr >= MIN_MSG_LINES && strstr(receivedChars[0], "Date:"))
    {
      if (selMsgs > MAX_MSG_COUNTER)
        selMsgs = 0;
      else
        selMsgs++;
      return true;
    }else
      memset(receivedChars,0,sizeof receivedChars);     //clear the recieved charater buffer
      return false;
  }else
    return false;
}

void sendMeterCommand()
{
  Serial1.println("METER");
  lossOfCommTime = rtc.getY2kEpoch();
  #ifdef DEBUG
    Serial.println("Meter Command sent to SEL");
  #endif
}

void login2sel()
{
  #ifdef DEBUG
    Serial.println("Logging In...");
  #endif
  //Login to SEL at Level 1
  Serial1.println("ACC");
  
  //clear receive buffer for "Password?"
  while (Serial1.available())
  {
  #ifdef DEBUG
      Serial.print(Serial1.read());
  #else
      Serial1.read();
  #endif
      delay(2);
  }

  delay(2);
  //Send password
  Serial1.println("OTTER");

  //Clear recieve buffer
  while (Serial1.available()) {
  #ifdef DEBUG
      Serial.print(Serial1.read());
  #else
      Serial1.read();
  #endif
    delay(1);
  }


  //update login count & display
  #ifdef DEBUG
    loginCount++;
  #endif

}

bool processSELmeterData()
{
  if (lineCtr <= 21){
    newData = false;
    lineCtr = 0;
    memset(receivedChars,0,sizeof receivedChars);     //clear the recieved charater buffer
    return false;
  }
  
  #ifdef DEBUG
    Serial.println("Processing meter Data");
    Serial.print("MeterDataLines: ");
    Serial.println(lineCtr);
  #endif
    
  String formattedMessage="";
  filebuffer = "";
  char *token;

  /*
     General meter output string format for SEL751 & SEL751A
      RID, Date, Time, FID, IA, IB, IC, IN, IG, IA_ang, IB_ang, IC_ang, IN_ang, IG_ang, Iavg, 3I2, Iunb,
          VA (or VAB), VB (or VBC), VC (or VCA), Vg, VA_ang, VB_ang. VC_ang, Vg_ang, Vavg, 3V2, Vunb,
          PA, PB, PC, 3P, QA, QB, QC, Q3, SA, SB, SC, S3, PFA, PFB, PFC, PF3, PFdA, PFdB, PFdC, PFd3, Freq

          the format of instantaneous metering changes based on PT configuration ("VA" or "VAB" for WYE or DELTA PT)
          May have additional VS or VN cvoltage channel if relay is optioned with that card.
          May have no voltage inputs if 3-phase voltage card E is not installed.
  */
  const int maxChannels = 10;

  String RID, FID, Date, Time;
  String voltageCH[maxChannels];
  String currentCH[maxChannels];
  String powerCH[maxChannels];
  int powerCHctr = 0;
  int currentCHctr = 0;
  int voltageCHctr = 0;
  String logFileDataString = "";
  String logFileHeaderString = "";
  String currentMag = "";
  String currentAng = "";
  String currentAvg = "";
  String currentNegSeq = "";
  String currentUnb = "";
  String voltMag = "";
  String voltAng = "";
  String voltAvg = "";
  String voltNegSeq = "";
  String voltUnb = "";
  String WATT = "";
  String VAR = "";
  String VA = "";
  String pf = "";
  String pfd = "";
  String freq = "";

  //Units
  String unitsAmp = "";
  String unitsAmpAng = "";
  String unitsAmpAvg = "";
  String unitsAmpNegSeq = "";
  String unitsAmpUnb = "";
  String unitsVolt = "";
  String unitsVoltAng = "";
  String unitsVoltAvg = "";
  String unitsVoltNegSeq = "";
  String unitsVoltUnb = "";
  String unitsWATT = "";
  String unitsVAR = "";
  String unitsVA = "";
  String unitsPF = "";
  String unitsFreq ="";

  int powerDataIndex = 1;  //start row of power data.
  int voltDataIndex = 1; //start row for volt data

  for (int i = 0; i < lineCtr; i++)
  {
    if (i == 0 && strstr(receivedChars[i], "Date:") && strstr(receivedChars[i], "Time:"))
    {
      token = strtok(receivedChars[i], " ");
      //if(token == NULL) break;
      while (token != NULL)
      {
        if (strstr(token, "Date:"))
        {
          token = strtok(0, " ");
          if (token != NULL)
            Date = token;
        } else if (strstr(token, "Time:"))
        {
          token = strtok(0, " ");
          if (token != NULL)
            Time = token;
        } else
        {
          RID += token;
          RID += " ";
        }
        token = strtok(0, " ");

      }
      RID.trim();
    }
    else if (i == 1 && strstr(receivedChars[i], "Time Source:"))
    {
      token = strtok(receivedChars[i], " ");
      bool stopFound = false;

      while (token != NULL)
      {
        if (strstr(token, "Time"))
          stopFound = true;
        else if (stopFound == false)
        {
          FID += token;
          FID += " ";

        }
        token = strtok(0, " ");

      }
      FID.trim();
    }
    //Current channels
    else if (i > 1 && strstr(receivedChars[i], "IA") && strstr(receivedChars[i], "IB") && !headerWritten)
    {
      token = strtok(receivedChars[i], " ");
      //if(token == NULL) break;
      while (token != NULL)
      {
        currentCH[currentCHctr] = token;
        currentCHctr++;
        if (currentCHctr >= (maxChannels - 1))
          break;
        token = strtok(0, " ");
      }
      //Get current magnitudes
    } else if (i > 1 && strstr(receivedChars[i], "Current Mag"))
    {
       getRowValues(receivedChars[i],&currentMag,&unitsAmp);
    }
    //get current angles
    else if (i > 1 && strstr(receivedChars[i], "Current Angle"))
    {
      getRowValues(receivedChars[i],&currentAng,&unitsAmpAng);
    }
    //get average current
    else if (i > 1 && strstr(receivedChars[i], "Ave Curr Mag"))
    {
      getSingleValue(receivedChars[i],&currentAvg,&unitsAmpAvg);
    }
    //get negative sequence current
    else if (i > 1 && strstr(receivedChars[i], "Neg-Seq Curr"))
    {
      getSingleValue(receivedChars[i],&currentNegSeq,&unitsAmpNegSeq);
    }
    //get current unbalance
    else if (i > 1 && strstr(receivedChars[i], "Current Imb"))
    {
      getSingleValue(receivedChars[i],&currentUnb,&unitsAmpUnb);
      voltDataIndex = i;
    }
    //Voltage channels
    else if (i >= voltDataIndex && strstr(receivedChars[i], "VA") && strstr(receivedChars[i], "VB") && !headerWritten)
    {
      token = strtok(receivedChars[i], " ");
      //if(token == NULL) break;
      while (token != NULL)
      {
        voltageCH[voltageCHctr] = token;
        voltageCHctr++;
        if (voltageCHctr >= (maxChannels - 1))
          break;
        token = strtok(0, " ");
      }
    }
    //get voltage magnitudes
    else if (i >= voltDataIndex && strstr(receivedChars[i], "Volt Mag"))
    {
      getRowValues(receivedChars[i],&voltMag,&unitsVolt);
    }
    //get voltage angles
    else if (i >= voltDataIndex && strstr(receivedChars[i], "Volt Angle"))
    {
       getRowValues(receivedChars[i],&voltAng,&unitsVoltAng);
    }
    //get average voltage
    else if (i >= voltDataIndex && strstr(receivedChars[i], "Avg Phase"))
    {
      getSingleValue(receivedChars[i],&voltAvg, &unitsVoltAvg);
    }
    //get negative sequence voltage
    else if (i >= voltDataIndex && strstr(receivedChars[i], "Neg-Seq Volt"))
    {
      getSingleValue(receivedChars[i],&voltNegSeq,&unitsVoltNegSeq);
    }
    //get voltage unbalance
    else if (i >= voltDataIndex && strstr(receivedChars[i], "Voltage Imb"))
    {
      getSingleValue(receivedChars[i],&voltUnb,&unitsVoltUnb);
      powerDataIndex = i;
    }

    //Power channels
    else if (i >= powerDataIndex && strstr(receivedChars[i], "A") && strstr(receivedChars[i], "3P") && !headerWritten)
    {
      token = strtok(receivedChars[i], " ");
      //if(token == NULL) break;
      while (token != NULL)
      {
        powerCH[powerCHctr] = token;
        powerCHctr++;
        if (powerCHctr >= (maxChannels - 1))
          break;
        token = strtok(0, " ");
      }
    }
    //get real power data
    else if (i >= powerDataIndex && strstr(receivedChars[i], "Real"))
    {
      getRowValues(receivedChars[i],&WATT,&unitsWATT);
    }
    //get reactive power data
    else if (i >= powerDataIndex && strstr(receivedChars[i], "Reactive"))
    {
      getRowValues(receivedChars[i],&VAR, &unitsVAR);
    }
    //get complex power data
    else if (i >= powerDataIndex && strstr(receivedChars[i], "Apparent"))
    {
      getRowValues(receivedChars[i],&VA,&unitsVA);
    }
    //get power factor data
    else if (i >= powerDataIndex && strstr(receivedChars[i], "Factor"))
    {
      getRowValues(receivedChars[i],&pf,&unitsPF);
    }
    //get power factor lead/lag data
    else if (i >= powerDataIndex && (strstr(receivedChars[i], "LEAD") || strstr(receivedChars[i], "LAG")))
    {
      token = strtok(receivedChars[i], " ");

      while (token != NULL)
      {
        pfd += token;
        pfd += ",";
        token = strtok(0, " ");
      }
    }
    //get frequency data
    else if (i >= powerDataIndex && strstr(receivedChars[i], "Freq"))
    {
      getSingleValue(receivedChars[i],&freq,&unitsFreq);
    }

  }//end "i" for loop

  #ifdef DEBUG
    Serial.println("Finished Proceesing meter command...");
  #endif

  //Create Header
  if (!headerWritten)
  {
    //Relay identification
    logFileHeaderString = "RID," + RID + ",\n";
    logFileHeaderString += "FID," + FID + ",\n";

    /*
     * Logger Units - Table Format
     */
    /*
    logFileHeaderString += "Amp Units," + unitsAmp + ",\n";
    logFileHeaderString += "Amp Angle Units," + unitsAmpAng + ",\n";
    logFileHeaderString += "Amp Avg Units," + unitsAmpAvg + ",\n";
    logFileHeaderString += "Amp Neg. Seq. Units," + unitsAmpNegSeq + ",\n";
    logFileHeaderString += "Amp Imbalance Units," + unitsAmpUnb + ",\n";
    logFileHeaderString += "Voltage Units," + unitsVolt + ",\n";
    logFileHeaderString += "Voltage Angle Units," + unitsVoltAng + ",\n";
    logFileHeaderString += "Voltage Avg Units," + unitsVoltAvg + ",\n";
    logFileHeaderString += "Volt Neg. Seq. Units," + unitsVoltNegSeq + ",\n";
    logFileHeaderString += "Volt Imbalance Units," + unitsVoltUnb + ",\n";
    logFileHeaderString += "Real Power Units," + unitsWATT + ",\n";
    logFileHeaderString += "Reactive Power Units," + unitsVAR + ",\n";
    logFileHeaderString += "Apparent Power Units," + unitsVA + ",\n";
    logFileHeaderString += "Frequency Units," + unitsFreq + ",\n";

    //time & date
    logFileHeaderString += "DATE,TIME,";

    //Current data headers
    for (int i = 0; i < currentCHctr; i++)
    {
      logFileHeaderString += currentCH[i];
      logFileHeaderString += "_MAG,";
    }
    for (int i = 0; i < currentCHctr; i++)
    {
      logFileHeaderString += currentCH[i];
      logFileHeaderString += "_ANG,";
    }
    logFileHeaderString += "I_AVG,I2,IMB,";

    //voltage data headers
    for (int i = 0; i < voltageCHctr; i++)
    {
      logFileHeaderString += voltageCH[i];
      logFileHeaderString += "_MAG,";
    }
    for (int i = 0; i < voltageCHctr; i++)
    {
      logFileHeaderString += voltageCH[i];
      logFileHeaderString += "_ANG,";
    }
    logFileHeaderString += "V_AVG,V2,V_IMB,";

    //power data headers
    for (int i = 0; i < powerCHctr; i++)
    {
      logFileHeaderString += "P";
      logFileHeaderString += powerCH[i];
      logFileHeaderString += ",";
    }
    for (int i = 0; i < powerCHctr; i++)
    {
      logFileHeaderString += "Q";
      logFileHeaderString += powerCH[i];
      logFileHeaderString += ",";
    }
    for (int i = 0; i < powerCHctr; i++)
    {
      logFileHeaderString += "S";
      logFileHeaderString += powerCH[i];
      logFileHeaderString += ",";
    }
    for (int i = 0; i < powerCHctr; i++)
    {
      logFileHeaderString += "pf";
      logFileHeaderString += powerCH[i];
      logFileHeaderString += ",";
    }
    for (int i = 0; i < powerCHctr; i++)
    {
      logFileHeaderString += "pf_dir";
      logFileHeaderString += powerCH[i];
      logFileHeaderString += ",";
    }
    logFileHeaderString += "freq,";
    */

     /*
     * Logger Units - INLINE FORMAT
     */
    
    //time & date
    logFileHeaderString += "DATE,TIME,";

    //Current data headers
    for (int i = 0; i < currentCHctr; i++)
    {
      logFileHeaderString += currentCH[i];
      logFileHeaderString += "_MAG" + unitsAmp + ",";
    }
    for (int i = 0; i < currentCHctr; i++)
    {
      logFileHeaderString += currentCH[i];
      logFileHeaderString += "_ANG" + unitsAmpAng + ",";
    }
    logFileHeaderString += "I_AVG" + unitsAmpAvg + ",3I2" + unitsAmpNegSeq + ",IMB" + unitsAmpUnb + ",";

    //voltage data headers
    for (int i = 0; i < voltageCHctr; i++)
    {
      logFileHeaderString += voltageCH[i];
      logFileHeaderString += "_MAG" + unitsVolt + ",";
    }
    for (int i = 0; i < voltageCHctr; i++)
    {
      logFileHeaderString += voltageCH[i];
      logFileHeaderString += "_ANG" +unitsVoltAng + ",";
    }
    logFileHeaderString += "V_AVG" + unitsVoltAvg +",3V2" + unitsVoltNegSeq + ",V_IMB" + unitsVoltUnb + ",";

    //power data headers
    for (int i = 0; i < powerCHctr; i++)
    {
      logFileHeaderString += "P";
      logFileHeaderString += powerCH[i];
      logFileHeaderString += unitsWATT + ",";
    }
    for (int i = 0; i < powerCHctr; i++)
    {
      logFileHeaderString += "Q";
      logFileHeaderString += powerCH[i];
      logFileHeaderString += unitsVAR + ",";
    }
    for (int i = 0; i < powerCHctr; i++)
    {
      logFileHeaderString += "S";
      logFileHeaderString += powerCH[i];
      logFileHeaderString += unitsVA + ",";
    }
    for (int i = 0; i < powerCHctr; i++)
    {
      logFileHeaderString += "pf";
      logFileHeaderString += powerCH[i];
      logFileHeaderString += ",";
    }
    for (int i = 0; i < powerCHctr; i++)
    {
      logFileHeaderString += "pf_dir";
      logFileHeaderString += powerCH[i];
      logFileHeaderString += ",";
    }
    logFileHeaderString += "freq" + unitsFreq + ",\n";
  
  }
  //Create Data Line
  logFileDataString = Date + "," + Time + "," + currentMag + currentAng +
                      currentAvg + currentNegSeq + currentUnb + voltMag + voltAng +
                      voltAvg + voltNegSeq + voltUnb + WATT + VAR + VA + pf + pfd + freq;
  //only send header on the first new write
  if (!headerWritten)
  {
    headerWritten = true;
    logFileCounter++;
    formattedMessage = logFileHeaderString;
  }
  
  formattedMessage+=logFileDataString;
  filebuffer=formattedMessage;
  newData = false;
  memset(receivedChars,0,sizeof receivedChars);     //clear the recieved charater buffer
  lineCtr = 0;
  return true;

}

void writeDataToSD()
{
    bool append2file = false;
    
    if (filebuffer.length()>10)
    {
      #ifdef DEBUG 
        Serial.println("In SD write");
        Serial.println(filebuffer);
        //Serial.print("Generated Filename: ");
        //Serial.println(String(logFileCounter) + "_SEL_LOG.CSV");
      #endif
    

      
      //String filename = ("SEL_LOG_" + String(logFileCounter) +".CSV");
      String filename = "SEL_TEST.CSV";
      
      
      if(filename == currentLogFilename)
        append2file = true;
      else
        currentLogFilename = filename;
  
      //double check SD card is inserted
      if(SD.begin(chipSelect))
      {
        /*
        while(SD.exists(filename)&&!append2file)
        {
            logFileCounter++;
            logFileCounter + "_SEL_LOG.csv";
        }
        */
  
        
        File dataFile = SD.open(currentLogFilename, FILE_WRITE);
        if(dataFile)
        {
          //digitalWrite(SD_GREEN_LED,HIGH);
          dataFile.println(filebuffer);
          filebuffer = "";
          dataFile.close();
          #if defined(DEBUG) || defined(METER_STREAM)
            Serial.print("SD Write Successfull: ");
            Serial.println(currentLogFilename);
          #endif
        }
        //digitalWrite(SD_GREEN_LED,LOW);
      }
    }
}

void getRowValues(char *rowData, String *value, String *units)
{

  char *token;
  token = strtok(rowData, " ");
  bool unitStart = false;
  bool unitsFound = false;

  while (token != NULL)
  {
    if(strstr(token,")") && !unitsFound && unitStart)
    {
      *units+=" ";
      *units+=token;
      unitsFound = true;  
    }
    else if((strstr(token,"(") ||unitStart) && !unitsFound)
    {
        *units+=token;
        if(strstr(token,")"))
          unitsFound = true;
        else
          unitStart = true;
    }
        
    //check if token is a number
    else if (strspn(token, "0123456789.-+") == strlen(token))
    {
      *value += token;
      *value += ",";
    }
    token = strtok(0, " ");
  }


}


void getSingleValue(char *rowData, String *value, String *units)
{

  char *token;
  token = strtok(rowData, " ");
  bool unitStart = false;
  bool unitsFound = false;
  
  while (token != NULL)
  {
    if(strstr(token,")")&& !unitsFound && unitStart)
    {
      *units+=" ";
      *units+=token;
      unitsFound = true; 
    }
    else if((strstr(token,"(") ||unitStart) && !unitsFound)
    {
        *units+=token;
        if(strstr(token,")"))
          unitsFound = true;
        else
          unitStart = true;
    }
    //check if token is a number
    if (strspn(token, "0123456789.-+") == strlen(token))
    {
      *value = token;
      *value += ",";
      break;
    }
    token = strtok(0, " ");
  }


}
