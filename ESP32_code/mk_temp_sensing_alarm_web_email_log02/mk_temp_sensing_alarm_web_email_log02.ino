/*
 * data collection code that uses a DHT11 module and 2x DS18B20 1-wire sensors 
 * with the ESP32 Maker Kit v3 PCB with custom headers
 * with button press detection added, and alarms with time of day 'usage'
 *  + file storage of key parameters
 *  + a web interface that also shows alarm status
 *  + the sending of email alerts when an alarm is triggered and
 *  a web option to 'log' results in SPIFFS files over a period of time and
 *  to list and download the available stored files
 *  02 version extends the number of parameters stored in SPIFFS updateable in the web access
 */

// mk_temp_sensing_alarm_web_email_log02.ino - ESP32 therm proto project code that collects temperature and humidity
// readings from a DHT11 module that has either a 5kOhm or 1kOhm pull-up resistor 
// built in to the module between the data line and the 3.3V power supply line
// and 2 DS18B20 1-wire sensors that 'share' a 4.7kOhm pull-up resistor on the protoboard

// Data is collected at intervals and displayed on the OLED using a 4 row 12pt screen layout
//  with button press interrupts detected to allow:
//  - system restart with the RED button
//  - Alarm ON/OFF 'mix' set by toggling through options with the YELLOW button
// 
//  code developed and compiled using the Arduino IDE v1.8.19
//  ESP32 Dev Module set as the Board in the IDE - using board version 1.0.4
//   even though the board is probably the NodeMCU-32S i.e. 38 pin ESP32
//  Flash Size: 4MB
//  Partition Scheme set to: Default 4MB with spiffs (1.2 MB App / 1.5MB SPIFFS)
//   lots of other Board settings! but none experimented with

// SPIFF memory initially uploaded using the separate file 'data' upload method
#include "Arduino.h"
#include "FS.h"
#include "SPIFFS.h"

// ******************************
//  System parameters
// ******************************
String version = "02";
String location = "airing cupboard hot water cylinder";  // overwritten by SPIFFS file
String location_str;
int minterval = 20;                        // overwritten by SPIFFS file
String minterval_str;


// ******************************
//  Data logging parameters
// ******************************
int starthr;
String starthrstr;
int stophr;
String stophrstr;
String datalabel;
String datafile;      // 'constructed' file name where the current data logging is stored
String downloadfile;  // current selected completed data logging file to be downloaded 
String deletefile;    // data logging file to be deleted
bool loggingstatus = false;  // logic flag to indicate whether data logging is active
// loggeddata string is constructed as below and appended to the datafile opened in SPIFFS
// each file addition from 1 to n is: timestamp, DHT11temp, DHThumid, ds18temp[1], ds18temp[2]
String loggeddata = ""; 
int lognum = 0;  // total number of records
String dataerror;
String datafilelist;

// ***************************
//  SMTP email configuration
// ***************************
#include <ESP_Mail_Client.h>
String smtp_host;
#define SMTP_PORT 465

/* The sign in credentials */
String author_email;     // address of the sending email populated from a SPIFFS file
String author_password;  // password for the sending email populated from a SPIFFS file
String msg_sendname;     // 'real' name for sending email populated from a SPIFFS file
String msg_senttext;     // text appended to sent message populated from a SPIFFS file

String recipient_email;  // recipient's email address populated from a SPIFFS file
String recipient_name;   // recoipient's 'real' name populated from a SPIFFS file

/* The SMTP Session object used for Email sending */
SMTPSession smtp;

/* Declare the session config data */
ESP_Mail_Session session;

/* Declare the message class */
SMTP_Message message;

/* Callback function to get the Email sending status */
void smtpCallback(SMTP_Status status);

String htmlMsg;   // updated on a per send basis

// *****************************************
//  WiFi, web server and time configuration
// *****************************************
#include <WiFi.h>
#include "WebServer.h"
#include <WiFiClient.h>
#include <ESPmDNS.h>         // Include the mDNS library
#include "time.h"
String hour_time = "xx";
String mins_time = "yy";
String secs_time;

WebServer server(80);  //instantiate the web server at port 80 (http port#)
String namehost = "tempsense01";
String namehost_str;
String page_content = "";   // initialised global variable for web page content
String header_content;

// network credentials to connect to local WiFi networks 
//  these variables are simply initialised here since it is assumed that all the values have
//  already been 'written' to the appropriate SPIFFS files or will be populated via the web 
// N.B. variables are all strings BUT thse need to be converted to a char for use with the WiFi
String ssid_selected = "";
int NSSIDs = 5;   // number of local WiFi credentials that can be set up: the first one found is used
String str_NSSIDs = " ";
String ssid1 = " ";
String ssid2 = " ";
String ssid3 = " ";
String ssid4 = " ";
String ssid5 = " ";
String password1 = " ";
String password2 = " ";
String password3 = " ";
String password4 = " ";
String password5 = " ";

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;

/**
 * include the various specialised libraries that are availble once
 *  the OneWireNg main library has been installed in the Arduino IDE
 */
// **********************************
//  1-wire & DS18B20  configuration
// **********************************
#include "OneWireNg_CurrentPlatform.h"
#include "drivers/DSTherm.h"
#include "utils/Placeholder.h"
#define OW_PIN          33

/*
 * Set to true for parasitically powered sensors.
 */
#define PARASITE_POWER  false

/*
 * Uncomment for single sensor setup.
 *
 * With this scenario only one sensor device is allowed to be connected
 * to the bus. The library may be configured with 1-wire search activity
 * disabled to reduce its footprint.
 */
//#define SINGLE_SENSOR
#define CONFIG_SEARCH_ENABLED true

/*
 * Uncomment for power provided by a switching
 * transistor and controlled by this pin.
 */
//#define PWR_CTRL_PIN    9

/*
 * Uncomment to set permanent, common resolution for all
 * sensors on the bus. Resolution may vary from 9 to 12 bits.
 */
//#define COMMON_RES      (DSTherm::RES_12_BIT)

#if !defined(SINGLE_SENSOR) && !CONFIG_SEARCH_ENABLED
# error "CONFIG_SEARCH_ENABLED is required for non SINGLE_SENSOR setup"
#endif

static Placeholder<OneWireNg_CurrentPlatform> _ow;

float ds18tempfl[3] = {};    // initialise the DS18B20 temperature float array
String ds18tempstr[3] = {};  // initialise the DS18B20 temperature string array

// **********************
//  OLED configuration
// **********************
// uses the ThingPulse OLED driver "ESP8266 and ESP32 Oled Driver for SSD1306 display"
#include "SSD1306Wire.h"        // used for the 128x64 OLED
#include "Open_Sans_Regular_12.h"
#include "Open_Sans_Regular_16.h"
#include "Open_Sans_Regular_18.h"
#include "Open_Sans_Regular_24.h"
// initialise the text strings for each of the OLED 'rows'
String row1;
String r1;
String row2;
String r2;
String row3;
String r3;
String alrow;
String row4;
String r4;

// ******************************
//  initialise alarm  parameters
// ******************************
int alarmset;       // 1 to 5 integer that defines the ON/OFF mix for two alarms
String alarmset_str;
float a1threshold;  // alarm 1 threshold value
String a1threshold_str;
int a1alarm_send = 0;

// 0 = not alarming  1 = alarm but out of window  2 = alarm within window
int a1status;

String a1criteria;  // alarm 1 < or > criteria
String a1criteria_str;
float a2threshold;  // alarm 2 threshold value
String a2threshold_str;
int a2alarm_send = 0;

// 0 = not alarming  1 = alarm but out of window  2 = alarm within window
int a2status;

String a2criteria;  // alarm 2 < or > criteria
String a2criteria_str;
int a1open;         // hour that the window for alarm1 'opens'
String a1open_str;
int a1close;        // hour that the window for alarm1 'closes'
String a1close_str;
int a2open;         // hour that the window for alarm2 'opens'
String a2open_str;
int a2close;        // hour that the window for alarm2 'closes'
String a2close_str;

// ***************************************************
//  DHT11 temperature & humidity sensor configuration 
// *************************************************** 
#include "DHT.h"  // needs the Arduino "DHT sensor library" and associated "Adadruit Unified Sensor"
#define DHTPIN 32     // Digital pin connected to the DHT signal line - uses the PCB's servo2 pin

// Uncomment whatever type of sensor is being used
#define DHTTYPE DHT11     // DHT 11
//#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
//#define DHTTYPE DHT21   // DHT 21 (AM2301)
String dhttemp;
String dhthumid;

// Initialize DHT sensor.
// Note that older versions of this library took an optional third parameter to
// tweak the timings for faster processors.  This parameter is no longer needed
// as the current DHT reading algorithm adjusts itself to work on faster procs.
DHT dht(DHTPIN, DHTTYPE);

// ************************
//  button configuration
// ************************
int switch_pin1 = 39; // this is the GPIO pin connected to one side of the 1st button
int switch_pin2 = 34; // this is the GPIO pin connected to one side of the 2nd button

// ************************
//  buzzer configuration
// ************************
int buzzpin = 25;            // buzzer GPIO pin

// *****************************
//  miscellaneous configuration
// *****************************
bool printon = true;   // print control flag
bool skipreaddht = false; // logic flag to skip updates if DHT11 gives a bad reading
bool skipreadds = false;  // logic flag to skip updates if just one of the DS18B20's gives a bad reading


// ****** initial set up ***********
void setup() {

    Serial.begin(115200);
    // short delay to set up the monitor
    delay(5500); 
    Serial.println(" ");
    Serial.println("program starting ..... ");

    // ESP32 special considerations
    // pins 1, 3, 5, 14, and 15 are set HIGH on boot - so set them LOW unless/until they are needed
    //pinMode(1, OUTPUT);
    pinMode(3, OUTPUT);
    pinMode(5, OUTPUT);
    pinMode(14, OUTPUT);
    pinMode(15, OUTPUT);
    //digitalWrite(1, LOW);
    digitalWrite(3, LOW);
    digitalWrite(5, LOW);
    digitalWrite(14, LOW);
    digitalWrite(15, LOW); 

    // **********************************************
    // set up the OLED and display startup OLED text
    // **********************************************
    SSD1306Wire display(0x3c, 21, 22);
    display.init();  
    Serial.println("128x64 OLED initialised");
    oledclear();
    row1 = "1:  xx.xC   2:  xx.xC";
    row2 = "xx.x degC  xx.x% RH";
    alarmrow(alarmset);  // this function sets row3
    row4 = hour_time + ":" + mins_time + " starting up ....";
    oledtext(4, 0, 0, 0, 0, 12, row1, row2, row3, row4);
    Serial.println("OLED startup text displayed .... ");

    // ********************************
    // start the SPI Flash File System
    // ********************************
    oledclear();
    r1 = "reading the";
    r2 = "SPIFFS data";
    oledtext(2, 0, 0, 0, 0, 24, r1, r2, "", "");
    delay(1500);

    if(!SPIFFS.begin(true)){
        Serial.println("An Error has occurred while mounting SPIFFS");  // need to have a fallback if this happens??
        return;
    } else {
        Serial.println("SPIFFS mounted OK");
    }

    // *****************************************
    // List all the SPIFFS system info (FSinfo)
    // *****************************************
    Serial.print("SPIFFS total available bytes: ");
    Serial.println(SPIFFS.totalBytes());
    Serial.print("SPIFFS used bytes: ");
    Serial.println(SPIFFS.usedBytes());

    // **************************************************************
    // read all the WiFi setting data from the SPIFFS text files
    // **************************************************************
    Serial.println("-------------------------------"); 
    str_NSSIDs = read_text("/str_NSSIDs.txt");
    Serial.println("-------------------------------");
    ssid1 = read_text("/ssid1.txt");
    ssid2 = read_text("/ssid2.txt");
    ssid3 = read_text("/ssid3.txt");
    ssid4 = read_text("/ssid4.txt");
    ssid5 = read_text("/ssid5.txt");
    Serial.println("-------------------------------");
    password1 = read_text("/password1.txt");
    password2 = read_text("/password2.txt");
    password3 = read_text("/password3.txt");
    password4 = read_text("/password4.txt");
    password5 = read_text("/password5.txt");
    Serial.println("-------------------------------");

    // ***************************************************************
    // read all the SMTP/email parameters from the SPIFFS text files
    // ***************************************************************
    Serial.println("-------------------------------"); 
    smtp_host = read_text("/smtphost.txt");
    author_email = read_text("/authoremail.txt");
    author_password = read_text("/authorpswd.txt");
    recipient_email = read_text("/recipientemail.txt");
    recipient_name = read_text("/recipientname.txt");
    msg_sendname = read_text("/msgsendname.txt");
    msg_senttext = read_text("/msgsenttext.txt");
    Serial.println("-------------------------------");

    // ******************************************************************
    // read all the system & alarm parameters from the SPIFFS text files
    // ******************************************************************
    Serial.println("-------------------------------"); 

    namehost = read_text("/hostname.txt");

    minterval_str = read_text("/minterval.txt");
    minterval = minterval_str.toInt();

    location = read_text("/location.txt");
    location_str = location;

    a1open_str = read_text("/a1open.txt");
    a1open = a1open_str.toInt();

    a2open_str = read_text("/a2open.txt");
    a2open = a2open_str.toInt();

    a1close_str = read_text("/a1close.txt");
    a1close = a1close_str.toInt();

    a2close_str = read_text("/a2close.txt");
    a2close = a2close_str.toInt();

    alarmset_str = read_text("/aset.txt");
    alarmset = alarmset_str.toInt();

    a1threshold_str = read_text("/a1thr.txt");
    a1threshold = a1threshold_str.toFloat();

    a1criteria = read_text("/a1cr.txt");
    a1criteria_str = a1criteria;

    a2threshold_str = read_text("/a2thr.txt");
    a2threshold = a2threshold_str.toFloat();

    a2criteria = read_text("/a2cr.txt");
    a2criteria_str = a2criteria;
    Serial.println("-------------------------------");

    // ***************************************************
    // set up WiFi and get current time from a NTP server
    // ***************************************************
    oledclear();
    r1 = "connecting";
    r2 = "to WiFi ..";
    oledtext(2, 0, 0, 0, 0, 24, r1, r2, "", "");
    // Connect to Wi-Fi
    selectWiFi();
    oledclear();
    r1 = String(ssid_selected);
    r2 = "connected";
    oledtext(2, 0, 0, 0, 0, 24, r1, r2, "", "");
    beep(3);
    delay(1500);
    oledclear();
    r1 = "Getting";
    r2 = "local time";
    oledtext(2, 0, 0, 0, 0, 24, r1, r2, "", "");
    // Init and get the time
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    printLocalTime();    // function to print a stylised version of the time

    // ***************************
    //  SMTP email setup
    // ***************************
    /** Enable the debug via Serial port
     * none debug or 0
     * basic debug or 1
    */
    Serial.println("setting up smtp.debug");
    smtp.debug(1);
    Serial.println("smtp.debug set up");
        
    Serial.println("setting up smtp.callback");
    /* Set the callback function to get the sending results */
    smtp.callback(smtpCallback);
    Serial.println("smtp.callback set up");

    /* Set the session config */
    Serial.println("setting up session config");
    session.server.host_name = smtp_host;
    session.server.port = SMTP_PORT;
    session.login.email = author_email;
    session.login.password = author_password;
    session.login.user_domain = "";
    Serial.println("session config set up");

    /* Set the message headers */
    Serial.println("setting up message headers");
    message.sender.name = msg_sendname;  
    message.sender.email = author_email;
    message.subject = "ESP Test Email";  // changed on a per send basis
    message.addRecipient(recipient_name, recipient_email);
    Serial.println("message headers set up");

    /*HTML message details*/
    Serial.println("setting up initial message details");
    htmlMsg = "<div style=\"color:#2f4468;\"><h1>Hello World!</h1><p>- Sent from ESP board</p></div>";   //changed on a per send basis
    message.html.content = htmlMsg.c_str();    // this also updated on a per send basis
    message.text.charSet = "us-ascii";
    message.html.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
    Serial.println("initial message details set up");

    // **************************************************************************************
    // button setup: each are 'hard wired' with a 4.7kOhm pull-up resistor on the protoboard
    // **************************************************************************************
    pinMode(switch_pin1, INPUT);   // make button 1 connected  to a GPIO pin an INPUT
    pinMode(switch_pin2, INPUT);   // make button 2 connected  to a GPIO pin an INPUT

    // ****************
    //  buzzer setup
    // ****************
    pinMode(buzzpin, OUTPUT);
    digitalWrite(buzzpin, LOW);

    // ********************************
    //  1-wire & DS18B20 sensor setup
    // ********************************

    #ifdef PWR_CTRL_PIN
        # if !CONFIG_PWR_CTRL_ENABLED
            #  error "CONFIG_PWR_CTRL_ENABLED needs to be configured"
        # endif
        new (&_ow) OneWireNg_CurrentPlatform(OW_PIN, PWR_CTRL_PIN, false);
    #else
        new (&_ow) OneWireNg_CurrentPlatform(OW_PIN, false);
    #endif
    DSTherm drv(_ow);

    #if (CONFIG_MAX_SEARCH_FILTERS > 0)
        static_assert(CONFIG_MAX_SEARCH_FILTERS >= DSTherm::SUPPORTED_SLAVES_NUM,
            "CONFIG_MAX_SEARCH_FILTERS too small");

        drv.filterSupportedSlaves();
    #endif

    #ifdef COMMON_RES
        /*
         * Set common resolution for all sensors.
         * Th, Tl (high/low alarm triggers) are set to 0.
         */
        drv.writeScratchpadAll(0, 0, COMMON_RES);

        /*
         * The configuration above is stored in volatile sensors scratchpad
         * memory and will be lost after power unplug. Therefore store the
         * configuration permanently in sensors EEPROM.
         */
        drv.copyScratchpadAll(PARASITE_POWER);
    #endif

    // 'start' the DHT11 sensor
    dht.begin();

    // update the OLED status and alarm rows
    getCurrentTime();
    row4 = hour_time + ":" + mins_time + " " + String(minterval) + "sec readings";
    alarmrow(alarmset);  // sets row3 and alrow text

    // ********************************
    //  web server setup
    // ********************************

    // ************************************************************************
    // interrupt 'handlers' triggered by various Web server requests:
    //  each specific URL that is detected by the web server invokes a 
    //  designated function that does something and then refreshes the web page
    // ************************************************************************
    // ** do this if web root i.e. / is requested
    server.on("/", handle_root);

    // ****** main selection actions ******

    // ** do this if /run_tests is requested
    server.on("/run_tests", handle_run_tests);
    // ** do this if /parameters is requested
    server.on("/parameters", handle_parameters);
    // ** do this if /sysinfo is requested
    server.on("/sysinfo", handle_sysinfo);
    // ** do this if /alarm_details is requested
    server.on("/alarm_details", handle_alarm_details);
    // ** do this if /logdata is requested
    server.on("/logdata", handle_logdata);

    // ****** sub selection parameter update actions ******
    // ** do this if /WiFi_params is requested
    server.on("/WiFi_params", handle_WiFi_params);
    // ** do this if /system_updates is requested
    server.on("/system_updates", handle_system_updates);

    // ****** detailed parameter update submission actions ******
    // ** do this if /WiFi_updates1 is requested
    server.on("/WiFi_updates1", handle_WiFi_updates1);
    // ** do this if /WiFi_updates2 is requested
    server.on("/WiFi_updates2", handle_WiFi_updates2);
    // ** do this if /WiFi_updates3 is requested
    server.on("/WiFi_updates3", handle_WiFi_updates3);
    // ** do this if /WiFi_updates4 is requested
    server.on("/WiFi_updates4", handle_WiFi_updates4);
    // ** do this if /WiFi_updates5 is requested
    server.on("/WiFi_updates5", handle_WiFi_updates5);

    // ****** component testing control actions ******
    // ** do this if /buzzbeep is requested 
    server.on("/buzzbeep", handle_buzzbeep);
    // ** do this if /testOLED is requested 
    server.on("/testOLED", handle_testOLED);
    // ** do this if /sensors is requested 
    server.on("/sensors", handle_sensors);

    // ****** sensor data logging actions ******
    // ** do this if /logging_updates is requested 
    server.on("/logging_updates", handle_logging_updates);
    // ** do this if /startlogging is requested 
    server.on("/startlogging", handle_startlogging);
    // ** do this if /stoplogging is requested 
    server.on("/stoplogging", handle_stoplogging);
    // ** do this if /datafilelist is requested 
    server.on("/datafilelist", handle_datafilelist);
    // ** do this if /downloaddatafile is requested 
    server.on("/downloaddatafile", handle_downloaddatafile);
    // ** do this if /deletedatafile is requested 
    server.on("/deletedatafile", handle_deletedatafile);

    // start web server
    server.begin();
    Serial.println("Web server started!");
    oledclear();
    r1 = "web server";
    r2 = "started";
    oledtext(2, 0, 0, 0, 0, 24, r1, r2, "", "");
    delay(1500);
 
    // ** do an initial population of the common header HTML used in all web pages
    header_content = HTMLheader();

    a1status = 0;
    a2status = 0;

    oledclear();
    r1 = "start up";
    r2 = "complete";
    oledtext(2, 0, 0, 0, 0, 24, r1, r2, "", "");
    delay(1500);

    oledclear();
    r1 = "starting";
    r2 = "to monitor";
    r3 = "every " + String(minterval) +"s";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    delay(1500);

}

// *** the continuous operational commands ***
void loop() {

    Serial.println(" ... taking measurements");
    // Do a 'for' loop so that the button presses and web interrupts can still be detected whilst
    // waiting for a reasonable amount of time between measurements.
    // e.g. for minterval=30 doing 60 loops with a 500ms delay produces a nominal 30s measurement interval
    for (int i=0; i<minterval*2; i++) {

        server.handleClient();  // look for an HTTP request from a browser to run/update web pages

        // check if button 1 has been pressed
        if (digitalRead(switch_pin1) == LOW) {
            // if here button 1 has been pressed so restart the ESP32 in 5 seconds
            for (int i=0; i<5; i++) {
                oledclear();
                r2 = "in " + String(5-i) + " secs";
                oledtext(2, 0, 0, 0, 0, 24, "Restarting", r2, "", "");
                delay(1000);  // extra delay so that the display text is 'seen'
            }
            oledclear();
            r2 = "now!!";
            oledtext(2, 0, 0, 0, 0, 24, "Restarting", r2, "", "");
            delay(500);  
            ESP.restart();
        }

        // check if button 2 has been pressed
        if (digitalRead(switch_pin2) == LOW) {
            // if here button 2 has been pressed so 'toggle' the alarm setting
            alarmset = alarmset + 1;
            if (alarmset > 5) alarmset = 1;
            alarmrow(alarmset);  // sets row3 and alrow text
            oledclear();
            r2 = "to " + alrow;
            oledtext(2, 0, 0, 0, 0, 24, "alarms set", r2, "", "");
            delay(1000);  // delay for the 1 second of count down
        }
        delay(500);
    }


    // ****************************************
    //  get DHT11 temp + humidity measurements
    // ****************************************
    DHT11temphumid();

    // ********************************************
    //  get DS18B20 1-wire temperature measurements
    // ********************************************
    DS18temp();


    oledclear();
    // update the OLED status row
    getCurrentTime();
    row4 = hour_time + ":" + mins_time + " " + String(minterval) + "sec readings";
    oledtext(4, 0, 0, 0, 0, 12, row1, row2, row3, row4);
    Serial.println("OLED display updated .... ");

    // check if logging has been initiated and we are in the start-stop hour window
    if (loggingstatus == true and hour_time.toInt() >= starthr and hour_time.toInt() <= stophr) {
        lognum = lognum + 1;
        Serial.print("data logging number: ");
        Serial.println(lognum);
        // now append to the file previously opened
        loggeddata = "";
        if (lognum < 10) {
            loggeddata = "0" + String(lognum) + ", ";
        } else {
            loggeddata = String(lognum) + ", ";
        }
        loggeddata += hour_time +  ":" + mins_time + ":" + secs_time + ", ";
	    	loggeddata += dhttemp + ", ";
    		loggeddata += dhthumid + ", ";
	    	loggeddata += ds18tempstr[1] + ", ";
	    	loggeddata += ds18tempstr[2] + ", ";
	    	loggeddata += a1threshold_str + ",  ";
	    	loggeddata += a2threshold_str;
        append_text(datafile, loggeddata);
        Serial.println("data appended to the file");
    }
    
}// ******************************************************
//  function to check tht a file exists in the SPIFFS 
// ******************************************************
bool exists(String path){
  bool yes = false;
  File file = SPIFFS.open(path, "r");
  if(!file.isDirectory()){
    yes = true;
  }
  file.close();
  return yes;
}

// ******************************************************
//  Callback function to get the Email sending status 
// ******************************************************
void smtpCallback(SMTP_Status status){
  /* Print the current status */
  Serial.println(status.info());

  /* Print the sending result */
  if (status.success()){
    Serial.println("----------------");
    ESP_MAIL_PRINTF("Messages sent success: %d\n", status.completedCount());
    ESP_MAIL_PRINTF("Messages sent  failed: %d\n", status.failedCount());
    Serial.println("----------------\n");
    struct tm dt;

    for (size_t i = 0; i < smtp.sendingResult.size(); i++){
      /* Get the result item */
      SMTP_Result result = smtp.sendingResult.getItem(i);
      time_t ts = (time_t)result.timestamp;
      localtime_r(&ts, &dt);

      ESP_MAIL_PRINTF("Message No: %d\n", i + 1);
      ESP_MAIL_PRINTF("Status: %s\n", result.completed ? "success" : "failed");
      ESP_MAIL_PRINTF("Date/Time: %d/%d/%d %d:%d:%d\n", dt.tm_year + 1900, dt.tm_mon + 1, dt.tm_mday, dt.tm_hour, dt.tm_min, dt.tm_sec);
      ESP_MAIL_PRINTF("Recipient: %s\n", result.recipients);
      ESP_MAIL_PRINTF("Subject: %s\n", result.subject);
    }
    Serial.println("----------------\n");
  }
}


// *************************************
//  DHT11 temp + humidity measurements
// *************************************
void DHT11temphumid() {
    skipreaddht = false;

    // Reading temperature or humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
    float h = dht.readHumidity();
    // Read temperature as Celsius (the default)
    float t = dht.readTemperature();
    // Read temperature as Fahrenheit (isFahrenheit = true)
    float f = dht.readTemperature(true);

    // Check if any reads failed and exit early (to try again).
    if (isnan(h) || isnan(t) || isnan(f)) {
      Serial.println(F("Failed to read from DHT sensor!"));
      skipreaddht = true;
    }

    if (!skipreaddht) {
        // The 'heat index' is what the temperature feels like to the human body when
        //  relative humidity is combined with the air temperature
        // Compute heat index in Fahrenheit (the default)
        float hif = dht.computeHeatIndex(f, h);
        // Compute heat index in Celsius (isFahreheit = false)
        float hic = dht.computeHeatIndex(t, h, false);

        Serial.print(F("Humidity: "));
        Serial.print(h);
        Serial.println(F("%"));
        Serial.print(F("Temperature: "));
        Serial.print(t);
        Serial.print(F("°C "));
        Serial.print(f);
        Serial.println(F("°F"));
        Serial.print(F("Heat index: "));
        Serial.print(hic);
        Serial.print(F("°C "));
        Serial.print(hif);
        Serial.println(F("°F"));
        Serial.println(" ");

        dhttemp = String(t,1);
        dhthumid = String(h,1);

        row2 = dhttemp + " degC  " + dhthumid + "% RH";

    }

}


// *************************************
//  DS18B20 temperature measurements
// *************************************
void DS18temp() {
    OneWireNg& ow = _ow;
    DSTherm drv(ow);
    Placeholder<DSTherm::Scratchpad> _scrpd;

    /* convert temperature on all sensors connected... */
    drv.convertTempAll(DSTherm::SCAN_BUS, PARASITE_POWER);

    /* read sensors one-by-one */
    int i = 0;
    ds18tempstr[0] = "2";   // the number of DS18B20 sensors
    for (const auto& id: ow) {
        i = i + 1;
        if (printId(id)) {
            if (drv.readScratchpad(id, &_scrpd) == OneWireNg::EC_SUCCESS) {
                printScratchpad(_scrpd);
                ds18tempfl[i] = getfloattemp(_scrpd);
                ds18tempstr[i] = String(ds18tempfl[i],1);
            } else {
                Serial.println("  Invalid device.");
                ds18tempstr[i] = "*invalid device*";
                skipreadds = true;
            }
        }
    }

    if (!skipreadds) {        
        row1 = "1: " + ds18tempstr[1] + " C   2: " + ds18tempstr[2] + " C" ;
        a1status = 0;
        a2status = 0;
        // check for alarm conditions
        if (alarmset == 2 or alarmset == 4) {   // alarm 1 active
            if (a1criteria == "gt") {      // check for above temperature condition
                if (ds18tempfl[1] > a1threshold) {   // alarm > temp 1 !!
                    Serial.print("Alarm 1 ON: # times email sent: ");
                    Serial.println(a1alarm_send);
                    oledclear();
                    r1 = "alarm 1: " + ds18tempstr[1];
                    r2 = "> " + a1threshold_str + " degC";
                    if (hour_time.toInt() >= a1open and hour_time.toInt() < a1close) {
                        oledtext(2, 0, 0, 0, 0, 24, r1, r2, "", "");
                        a1status = 2;
                        beep(3);  // this produces a delay so that OLED is 'seen'
                        /*HTML message details changed on a per send basis*/
                        htmlMsg = "<div style=\"color:#2f4468;\"><h1>Alarm 1! at " + hour_time + ":" + mins_time + "</h1>"; 
                        htmlMsg +="<p>DS18B20 sensor 1: " + ds18tempstr[1] + " is greater than the " + a1threshold_str + " degC threshold</p>";
                        htmlMsg +="<p>" + msg_senttext + "</p></div>";
                        message.html.content = htmlMsg.c_str();    // this also updated on a per send basis
                        message.subject = location + " Alarm 1 at " + hour_time + ":" + mins_time;
                        a1alarm_send = sendalarm(a1alarm_send);

                    } else {
                        r3 = "out of window";
                        a1status = 1;
                        oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
                        delay(2000);
                    }
                } else {  // not in an alarm condition so reset parameters
                    a1alarm_send = 0;  // reset the number of emails sent for an alarm 1 condition
                }


            } else if (a1criteria == "lt") {      // check for below temperature condition
                if (ds18tempfl[1] < a1threshold) {   // alarm < temp 1 !!
                    Serial.print("Alarm 1 ON: # times email sent: ");
                    Serial.println(a1alarm_send);
                    oledclear();
                    r1 = "alarm 1: " + ds18tempstr[1];
                    r2 = "< " + a1threshold_str + " degC";
                    if (hour_time.toInt() >= a1open and hour_time.toInt() <= a1close) {
                        oledtext(2, 0, 0, 0, 0, 24, r1, r2, "", "");
                        a1status = 2;
                        beep(3);  // this produces a delay so that OLED is 'seen'
                        /*HTML message details changed on a per send basis*/
                        htmlMsg = "<div style=\"color:#2f4468;\"><h1>Alarm 1! at " + hour_time + ":" + mins_time + "</h1>"; 
                        htmlMsg +="<p>DS18B20 sensor 1: " + ds18tempstr[1] + " is less than the " + a1threshold_str + " degC threshold</p>";
                        htmlMsg +="<p>" + msg_senttext + "</p></div>";
                        message.html.content = htmlMsg.c_str();    // this also updated on a per send basis
                        message.subject = location + " Alarm 1 at " + hour_time + ":" + mins_time;
                        a1alarm_send = sendalarm(a1alarm_send);

                    } else {
                        r3 = "out of window";
                        a1status = 1;
                        oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
                        delay(2000);
                    }

                } else {  // not in an alarm condition so reset parameters
                    a1alarm_send = 0;  // reset the number of emails sent for an alarm 1 condition
                }
            }
        }

        if (alarmset == 3 or alarmset == 4) {   // alarm 2 ON
            if (a2criteria == "gt") {      // check for above temperature condition
                if (ds18tempfl[2] > a2threshold) {   // alarm > temp 2 !!
                    Serial.print("Alarm 2 ON: # times email sent: ");
                    Serial.println(a2alarm_send);
                    oledclear();
                    r1 = "alarm 2: " + ds18tempstr[2];
                    r2 = "> " + a2threshold_str + " degC";
                    if (hour_time.toInt() >= a2open and hour_time.toInt() <= a2close) {
                        oledtext(2, 0, 0, 0, 0, 24, r1, r2, "", "");
                        a2status = 2;
                        beep(3);
                        /*HTML message details changed on a per send basis*/
                        htmlMsg = "<div style=\"color:#2f4468;\"><h1>Alarm 2! at " + hour_time + ":" + mins_time + "</h1>";
                        htmlMsg +="<p>DS18B20 sensor 2: " + ds18tempstr[2] + " is greater than the " + a2threshold_str + " degC threshold</p>";
                        htmlMsg +="<p>" + msg_senttext + "</p></div>";
                        message.html.content = htmlMsg.c_str();    // this also updated on a per send basis
                        message.subject = location + " Alarm 2 at " + hour_time + ":" + mins_time;
                        a2alarm_send = sendalarm(a2alarm_send);

                    } else {
                        r3 = "out of window";
                        a2status = 1;
                        oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
                        delay(2000);
                    }
                }
            } else if (a2criteria == "lt") {      // check for below temperature condition
                if (ds18tempfl[2] < a2threshold) {   // alarm < temp 2 !!
                    Serial.print("Alarm 2 ON: # times email sent: ");
                    Serial.println(a2alarm_send);
                    oledclear();
                    r1 = "alarm 2: " + ds18tempstr[2];
                    r2 = "< " + a2threshold_str + " degC";
                    if (hour_time.toInt() >= a2open and hour_time.toInt() <= a2close) {
                        oledtext(2, 0, 0, 0, 0, 24, r1, r2, "", "");
                        a2status = 2;
                        beep(3);
                        /*HTML message details changed on a per send basis*/
                        htmlMsg = "<div style=\"color:#ff0000;\"><h1>Alarm 2! at " + hour_time + ":" + mins_time + "</h1>";
                        htmlMsg +="<p>DS18B20 sensor 2: " + ds18tempstr[2] + " is less than the " + a2threshold_str + " degC threshold</p>";
                        htmlMsg +="<p>" + msg_senttext + "</p></div>";
                        message.html.content = htmlMsg.c_str();    // this also updated on a per send basis
                        message.subject = location + " Alarm 2 at " + hour_time + ":" + mins_time;
                        a2alarm_send = sendalarm(a2alarm_send);

                    } else {
                        r3 = "out of window";
                        a2status = 1;
                        oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
                        delay(2000);
                    }

                } else {  // not in an alarm condition so reset parameters
                    a2alarm_send = 0;  // reset the number of emails sent for an alarm 2 condition
                }
            }
        }

    }

}

// *********************
//  send alarm email
// *********************
int sendalarm(int timessent) {
    Serial.print("#times email sent at start of send: ");
    Serial.println(timessent);
    Serial.print("time minutes (string): ");
    Serial.println(mins_time);

    bool checktries = false;
    if (timessent == 0) {   // first time for this alarm so send email straightaway
        Serial.println("1st time alarm: immediate email send");
        /* Connect to server with the session config */
        // try for a maximum of 3 times
        for (int i=0; i<=2; i++) { 
            Serial.print("session connect try ");
            Serial.println(i);
            checktries = smtp.connect(&session);
            if (checktries) {
                Serial.println("SMTP session started OK");
                break;
            } else {
                Serial.print("Error establishing SMTP session: try ");  // in a new version this should be sent to a log file
                Serial.println(i);
                checktries = false;
            }
        }
        checktries = false;  
        /* Start sending Email and close the session */
        // try for a maximum of 3 times
        for (int i=0; i<=2; i++) { 
            Serial.print("email send try ");
            Serial.println(i);
            checktries = MailClient.sendMail(&smtp, &message);
            if (checktries) {
                Serial.println("email sent OK");
                break;
            } else {
                Serial.print("Error sending email: try ");  // in a new version this should be sent to a log file
                Serial.println(i);
                Serial.println(smtp.errorReason());
                checktries = false;
            }
        }
        timessent = 1;

    } else if (timessent > 0 and timessent < 3 and mins_time.toInt() == 0) {   // only send 2 more 'reminder' emails on the hour
        /* Connect to server with the session config */
        // try for a maximum of 3 times
        for (int i=0; i<=2; i++) { 
            checktries = smtp.connect(&session);
            if (checktries) {
                Serial.println("SMTP session started OK");
                break;
            } else {
                Serial.print("Error establishing SMTP session: try ");  // in a new version this should be sent to a log file
                Serial.println(i);
            }
        }
        /* Start sending Email and close the session */
        for (int i=0; i<=2; i++) { 
            checktries = MailClient.sendMail(&smtp, &message);
            if (checktries) {
                Serial.println("email sent OK");
                if (minterval < 31) {
                    delay(minterval*1000);  // wait 'minterval' seconds to avoid double sending in the same mins_time=0 'minute'
                }
                break;
            } else {
                Serial.print("Error sending email: try ");  // in a new version this should be sent to a log file
                Serial.println(i);
                Serial.println(smtp.errorReason());
            }
        }
        
        timessent = timessent + 1;
    }
    
    Serial.print("#times email sent at end of send: ");
    Serial.println(timessent);
    return timessent;

} 


// *********************
//  OLED text display
// *********************
void oledtext(int rows, int col1, int col2, int col3, int col4, int fontsize, String text1, String text2, String text3, String text4) {
    SSD1306Wire display(0x3c, 21, 22);
    display.init();
    display.flipScreenVertically();
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    if (fontsize == 12) {
        display.setFont(Open_Sans_Regular_12);
    } else if (fontsize == 16) {
        display.setFont(Open_Sans_Regular_16);
    } else if (fontsize == 18) {
        display.setFont(Open_Sans_Regular_18);
    } else if (fontsize == 24) {
        display.setFont(Open_Sans_Regular_18);
    } else {
        display.setFont(Open_Sans_Regular_12);  // set a default if an unknown font size is set
        Serial.println("Default font size set to 12");
    }
    if (rows == 1) {
        display.drawString(col1, 24, text1);
    } else if (rows == 2) {
        display.drawString(col1, 0, text1);
        display.drawString(col2, 31, text2);
    } else if (rows == 3) {   // normally fontsize should =16
        display.drawString(col1, 0, text1);
        display.drawString(col2, 21, text2);
        display.drawString(col3, 42, text3);
    } else if (rows == 4) {
        display.drawString(col1, 0, text1);
        display.drawString(col2, 16, text2);
        display.drawString(col3, 32, text3);
        display.drawString(col3, 48, text4);
    }
    display.display();
}

// *********************
//  OLED text cllear
// *********************
void oledclear() {
    SSD1306Wire display(0x3c, 21, 22);
    display.init();
    display.clear();
    display.displayOff();

}


// ***************************************
//  logic check for a valid 1-wire device
// ***************************************
/* returns false if not supported */
static bool printId(const OneWireNg::Id& id) {
    const char *name = DSTherm::getFamilyName(id);

    Serial.print(id[0], HEX);
    for (size_t i = 1; i < sizeof(OneWireNg::Id); i++) {
        Serial.print(':');
        Serial.print(id[i], HEX);
    }
    if (name) {
        Serial.print(" -> ");
        Serial.print(name);
    }
    Serial.println();

    return (name != NULL);
}

// ***********************************************
//  DS1820B read scratchpad and just print values
// ***********************************************
static void printScratchpad(const DSTherm::Scratchpad& scrpd) {
    const uint8_t *scrpd_raw = scrpd.getRaw();

    Serial.print("  Scratchpad:");
    for (size_t i = 0; i < DSTherm::Scratchpad::LENGTH; i++) {
        Serial.print(!i ? ' ' : ':');
        Serial.print(scrpd_raw[i], HEX);
    }

    Serial.print("; Th:");
    Serial.print(scrpd.getTh());

    Serial.print("; Tl:");
    Serial.println(scrpd.getTl());

    Serial.print("  Resolution:");
    Serial.print(9 + (int)(scrpd.getResolution() - DSTherm::RES_9_BIT));

    long temp = scrpd.getTemp();
    Serial.print("  Temp:");
    if (temp < 0) {
        temp = -temp;
        Serial.print('-');
    }
    Serial.print(temp / 1000);
    Serial.print('.');
    Serial.print(temp % 1000);
    Serial.print(" C");

    Serial.println();
}

// *****************************************************
//  DS1820B read scratchpad and return temps as floats
// *****************************************************
static float getfloattemp(const DSTherm::Scratchpad& scrpd) {
    const uint8_t *scrpd_raw = scrpd.getRaw();

    long temp = scrpd.getTemp();
    float tempds = (float) temp / 1000;
    Serial.print("  Temp:");
    Serial.print(tempds);
    Serial.print(" C");
    Serial.println();

    return tempds;

}


// *****************************************************
//  DS1820B read scratchpad and return temps as strings
// *****************************************************
static String getstringtemp(const DSTherm::Scratchpad& scrpd) {
    const uint8_t *scrpd_raw = scrpd.getRaw();

    bool negtemp;
    long temp = scrpd.getTemp();
    Serial.print("  Temp:");
    if (temp < 0) {
        negtemp = true;
        temp = -temp;
        Serial.print('-');
    }
    Serial.print(temp / 1000);
    Serial.print('.');
    Serial.print(temp % 1000);
    Serial.print(" C");

    Serial.println();

    float tempds = (float) temp / 1000;
    String tempstrds = String(tempds,1);
    if (negtemp) {
        tempstrds = "-" + tempstrds;
    }
    return tempstrds;

}

void alarmrow(int alset) {
    if (alset == 1) {
        row3 = "alarm1 n/a alarm2 n/a";
        alrow = "n/a  n/a";
    } else if (alset == 2) {
        row3 = "alarm1 " + String(a1open) + "-" + String(a1close) + " alarm2 OFF";
        alrow = "ON  OFF";
    } else if (alset == 3) {
        row3 = "alarm1 OFF alarm2 "  + String(a2open) + "-" + String(a2close);
        alrow = "OFF  ON";
    } else if (alset == 4) {
        row3 = "alarm1 "  + String(a1open) + "-" + String(a1close) + " alarm2 "  + String(a2open) + "-" + String(a2close);
        alrow = "ON  ON";
    } else if (alset == 5) {
        row3 = "alarm1 OFF alarm2 OFF";
        alrow = "OFF  OFF";
    }
}

// *************************************************************************
// Function to 'sound' the passive buzzer with a specific frequency/duration
// *************************************************************************
int buzz(int frequency, int duration) {   
    int buzzstat;
    // create the function "buzz" and feed it the note (e.g. DS6=1245Hz) and duration (length in ms))
    //Serial.print("Buzzing: pin ");
    //Serial.println(buzzpin);
    //Serial.print("Buzzing: frequency ");   // pitch/frequency of the note
    //Serial.println(frequency);
    //Serial.print("Buzzing: length (ms) "); // length/duration of the note in ms
    //Serial.println(duration);
    if (frequency == 0) {
        delay(duration);
        buzzstat = 0;
        return buzzstat;
    }
    // from the frequency calculate the time between buzzer pin HIGH-LOW setting in microseconds
    //float period = 1.0 / frequency;       // in physics, the period (sec/cyc) is the inverse of the frequency (cyc/sec)
    int delayValue = (1000*1000/frequency)/2;  // calculate the time in microseconds for half of the wave
    int numCycles = int((duration * frequency)/1000);   // the number of waves to produce is the duration times the frequency
    //Serial.print("Number of cycles: ");
    //Serial.println(numCycles);
    //Serial.print("Hi-Low delay (microsecs): ");
    //Serial.println(delayValue);
    for (int i=0; i<=numCycles-1; i++) {  // start a loop from 0 to the variable "cycles" calculated above
        digitalWrite(buzzpin, HIGH);      // set buzzer pin to high
        delayMicroseconds(delayValue);    // wait with buzzer pin high
        digitalWrite(buzzpin, LOW);       // set buzzer pin to low
        delayMicroseconds(delayValue);    // wait with buzzer pin low
    }

    buzzstat = 1;
    return buzzstat;
}

// **********************
// simple beep function
// **********************
void beep(int beeptime) {
    // beeptime in seconds
    int status;
    Serial.println("beeping buzzer at 900Hz for beeptime seconds");
    for (int j=0; j<=beeptime-1; j++) {
        // total duration of all the steps below to add up to 1 second
        status = buzz(900, 300);
        delay(200);
        status = buzz(900, 300);
        delay(200);
    }

    return;
}

// ************************
// time printing function
// ************************
void printLocalTime(){
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  Serial.print("Day of week: ");
  Serial.println(&timeinfo, "%A");
  Serial.print("Month: ");
  Serial.println(&timeinfo, "%B");
  Serial.print("Day of Month: ");
  Serial.println(&timeinfo, "%d");
  Serial.print("Year: ");
  Serial.println(&timeinfo, "%Y");
  Serial.print("Hour: ");
  Serial.println(&timeinfo, "%H");
  Serial.print("Hour (12 hour format): ");
  Serial.println(&timeinfo, "%I");
  Serial.print("Minute: ");
  Serial.println(&timeinfo, "%M");
  Serial.print("Second: ");
  Serial.println(&timeinfo, "%S");

  Serial.println("Time variables");
  char timeHour[3];
  strftime(timeHour,3, "%H", &timeinfo);
  Serial.println(timeHour);
  char timeWeekDay[10];
  strftime(timeWeekDay,10, "%A", &timeinfo);
  Serial.println(timeWeekDay);
  Serial.println();
}

// **********************************************************
// Function to read a single string from a written text file
// **********************************************************
String read_text(String rfile) {
    int i;
    String str_read;
    //open the file for reading
    File f = SPIFFS.open(rfile, "r");
  
    if (!f) {
        str_read = "file open failed";
        Serial.println(str_read);
    }
    else
    {
        Serial.print("Reading Text Data from File ");
        Serial.print(rfile);
        //read string from file
        str_read = f.readStringUntil('\n');
        if (rfile.substring(1,5) == "pass") {
            Serial.println("password not shown");
        } else {
            Serial.print(": ");
            Serial.println(str_read);
        }
        f.close();  //Close file
        Serial.println("File Closed");
    }
    return str_read;

}

// **********************************************************************
// Function to create/open an existing file and write a single string to 
//  it - the file name and text are passed strings to the function 
// **********************************************************************
void write_text(String wfile, String wtext) {
    //w=Write Open file for writing
    File SPIfile = SPIFFS.open(wfile, "w");
  
    if (!SPIfile) {
        Serial.println("file open failed");
    }
    else
    {
        //Write data to file
        Serial.print("Writing Data to File: ");
        Serial.println(wtext);
        SPIfile.print(wtext);
        SPIfile.close();  //Close file

    }
}

// **********************************************************************
// Function to open an existing file and append a single string to 
//  it - the file name and text are passed strings to the function 
// **********************************************************************
void append_text(String wfile, String wtext) {
    //a=Append file for writing
    File SPIfile = SPIFFS.open(wfile, "a");
  
    if (!SPIfile) {
        Serial.println("file append open failed");
    }
    else
    {
        //Append data to file
        Serial.print("Writing Data to File: ");
        Serial.println(wtext);
        if (SPIfile.println(wtext)) { 
            Serial.println("appended data was written");
        } else {
            Serial.println("append write failed");
        }
        SPIfile.close();  //Close file

    }
}

// *****************************************************************************
// Function to get the current time and 'set' the hours, mins & secs parameters
// *****************************************************************************
void getCurrentTime()  // get current time 
{
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
 
  hour_time = String(timeinfo.tm_hour);
  int tmins = timeinfo.tm_min;
  if (tmins < 10) {
    mins_time = "0" + String(timeinfo.tm_min);
  } else { 
    mins_time = String(timeinfo.tm_min);
  }
  //Serial.print("minutes: ");
  //Serial.println(mins_time);

  int tsecs = timeinfo.tm_sec;
  if (tsecs < 10) {
    secs_time = "0" + String(timeinfo.tm_sec);
  } else { 
    secs_time = String(timeinfo.tm_sec);
  }
 
}


// ********************************************
// ****           select  WiFi             ****
// ********************************************
void selectWiFi() {

    // scan to find all the 'live' broadcast SSID's ....
    int n = WiFi.scanNetworks();
    Serial.print("Number of WiFi networks found: ");
    Serial.println(n);
    ssid_selected ="";

    Serial.print("checking ");
    Serial.println(ssid1);
    // check if ssid1 is in the 'live' list
    for (int i = 0; i < n; ++i) {
        Serial.print("Checking SSID: ");
        Serial.println(WiFi.SSID(i));
        if (WiFi.SSID(i)== ssid1 ) {
            ssid_selected = ssid1;
			Serial.print(WiFi.SSID(i));
			Serial.print(" is being broadcast");
            break;
        }
    }
    if (ssid_selected != "" ) {
        // make connection to selected WiFi
        connectWiFi();
        return;
    }

    // --------------------------------------------
    // now try ssid2 if ssid1 not already selected
    Serial.print("trying to connect to: ");
    Serial.println(ssid2);
    for (int i = 0; i < n; ++i) {
        Serial.print("Checking SSID: ");
        Serial.println(WiFi.SSID(i));
        if (WiFi.SSID(i)== ssid2 ) {
            ssid_selected = ssid2;
			Serial.print(WiFi.SSID(i));
			Serial.print(" is being broadcast");
            break;
        }       
    }
    if (ssid_selected != "" ) {
        // make connection to selected WiFi
        connectWiFi();
        return;
    }

    // --------------------------------------------
    // now try ssid3 
    Serial.print("trying to connect to: ");
    Serial.println(ssid3);
    for (int i = 0; i < n; ++i) {
        Serial.print("Checking SSID: ");
        Serial.println(WiFi.SSID(i));
        if (WiFi.SSID(i)== ssid3 ) {
            ssid_selected = ssid3;
			Serial.print(WiFi.SSID(i));
			Serial.print(" is being broadcast");
            break;
        }       
    }
    if (ssid_selected != "" ) {
        // make connection to selected WiFi
        connectWiFi();
        return;
    }

    // --------------------------------------------
    // now try ssid4
    Serial.print("trying to connect to: ");
    Serial.println(ssid4);
    for (int i = 0; i < n; ++i) {
        Serial.print("Checking SSID: ");
        Serial.println(WiFi.SSID(i));
        if (WiFi.SSID(i)== ssid4 ) {
            ssid_selected = ssid4;
			Serial.print(WiFi.SSID(i));
			Serial.print(" is being broadcast");
            break;
        }       
    }
    if (ssid_selected != "" ) {
        // make connection to selected WiFi
        connectWiFi();
        return;
    }

    // --------------------------------------------
    // now try ssid5
    Serial.print("trying to connect to: ");
    Serial.println(ssid5);
    for (int i = 0; i < n; ++i) {
        Serial.print("Checking SSID: ");
        Serial.println(WiFi.SSID(i));
        if (WiFi.SSID(i)== ssid5 ) {
            ssid_selected = ssid5;
			Serial.print(WiFi.SSID(i));
			Serial.print(" is being broadcast");
            break;
        }       
    }
    if (ssid_selected != "" ) {
        // make connection to selected WiFi
        connectWiFi();
        return;
    }

    // if here then no allowed local WiFi found 
    Serial.println(" No allowed WiFi found");


}

// ******************************
// **** make WiFi connection ****
// ******************************
void connectWiFi() {

    Serial.print(ssid_selected);
    Serial.println(" selected - so now trying to connect");
	
	  if (ssid_selected == ssid1 ) {
            // convert the selected ssid and password to the char variables
            WiFi.begin(ssid1.c_str(), password1.c_str());    //initiate connection to ssid1
            Serial.print("starting to connect to SSID: ");
            Serial.println(ssid1);

    } else if (ssid_selected == ssid2 ) {
           // convert the selected ssid and password to the char variables
            WiFi.begin(ssid2.c_str(), password2.c_str());    //initiate connection to ssid2
            Serial.print("starting to connect to SSID: ");
            Serial.println(ssid2);

    } else if (ssid_selected == ssid3 ) {
            // convert the selected ssid and password to the char variables
            WiFi.begin(ssid3.c_str(), password3.c_str());    //initiate connection to ssid3
            Serial.print("starting to connect to SSID: ");
            Serial.println(ssid3);

    } else if (ssid_selected == ssid4 ) {
            // convert the selected ssid and password to the char variables
            WiFi.begin(ssid4.c_str(), password4.c_str());    //initiate connection to ssid4
            Serial.print("starting to connect to SSID: ");
            Serial.println(ssid4);

    } else if (ssid_selected == ssid5 ) {
            // convert the selected ssid and password to the char variables
            WiFi.begin(ssid5.c_str(), password5.c_str());    //initiate connection to ssid5
            Serial.print("starting to connect to SSID: ");
            Serial.println(ssid5);

    }
    Serial.println("");
    // Wait for connection
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    // Connected to the first available/defined WiFi Access Point
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid_selected);
    Serial.print("IP address: ");
    delay(500);
    Serial.println(WiFi.localIP());
    oledclear();
    r1 = "IP addr:";
    r2 = WiFi.localIP().toString();
    oledtext(2, 0, 0, 0, 0, 24, r1, r2, "", "");
    beep(3);
    delay(3000);
    Serial.print("MAC address: ");
    Serial.println(WiFi.macAddress());
    delay(500);
    WiFi.setHostname(namehost.c_str());
    delay(1500);
    Serial.print("updated hostname: ");
    Serial.println(WiFi.getHostname());

    return;
}

// *****************************************************************
// ***  this section is for all the browser response 'handlers'  ***
// *****************************************************************

void handle_root() {
    // ** do this if web root i.e. / is requested
    r1 = "web server:";
    r2 = "home page";
    r3 = "selected";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    Serial.println("web: home page selected");
    delay(2000);
    server.send(200, "text/html", HTMLmain()); 
}

void handleNotFound() {
    // ** do this if a non-existent page is requested
    r1 = "web server:";
    r2 = "page not";
    r3 = "found";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    Serial.println("web: page not found");
    delay(2000);
    server.send(200, "text/html", HTMLmain()); 
}

// **********************************
// *** main selection 'handlers' ****
// **********************************
void handle_run_tests() {
    // ** do this if /run_tests is requested
    r1 = "web server:";
    r2 = "run tests";
    r3 = "selected";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    Serial.println("selecting component testing");
    delay(2000);
    server.send(200, "text/html", HTMLrun_tests()); 
}

void handle_parameters() {
    // ** do this if /parameters is requested
    r1 = "web server:";
    r2 = "parameters";
    r3 = "selected";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    Serial.println("selecting parameter update");
    delay(2000);
    server.send(200, "text/html", HTMLparameter_selection()); 
}

void handle_logdata() {
    // ** do this if /logdata is requested
    r1 = "web server:";
    r2 = "log data";
    r3 = "selected";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    Serial.println("selecting data logging");
    delay(2000);
    server.send(200, "text/html", HTMLlogdata()); 
}

void handle_sysinfo() {
    // ** do this if /sysinfo is requested
    r1 = "web server:";
    r2 = "system info";
    r3 = "selected";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    Serial.println("selecting system information display");
    delay(2000);
    server.send(200, "text/html", HTMLsysinfo()); 
}

void handle_alarm_details() {
    // ** do this if /alarm_details is requested
    r1 = "web server:";
    r2 = "alarm details";
    r3 = "selected";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    Serial.println("showing alarm details");
    delay(2000);
    server.send(200, "text/html", HTMLalarm_details()); 
}


// ******************************************************
// ***  sub selection sensor data logging 'handlers'  ***
// ******************************************************
void handle_logging_updates() {
    // ** do this if /logging_updates is requested
    r1 = "web server:";
    r2 = "data logging";
    r3 = "updates";
    datafilelist = "";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    Serial.println("updating the data logging parameters");
    dataerror = "";
    if (server.arg("starthrstr") != "") {
        starthrstr = server.arg("starthrstr");
        starthr = starthrstr.toInt();
    }
    if (server.arg("stophrstr") != "") {
        stophrstr = server.arg("stophrstr");
        stophr = stophrstr.toInt();
    }
    if (server.arg("datalabel_str") != "") {
        if (server.arg("datalabel_str").indexOf("data") > 0)  {
            datalabel = server.arg("datalabel_str");
        } else {
            datalabel = "";
            dataerror = "Sorry: the data label text must include the text <i>data</i>";
        }
    }
    if (server.arg("downloadfile_str") != "") {
        downloadfile = server.arg("downloadfile_str");
    }
    if (server.arg("deletefile_str") != "") {
        deletefile = server.arg("deletefile_str");
    }

    server.send(200, "text/html", HTMLlogdata()); 
}

void handle_startlogging() {
    // ** do this if /startlogging is requested
    r1 = "web server:";
    r2 = "data logging";
    r3 = "started";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    delay(1000);
    Serial.println("starting data logging");
    loggingstatus = true;
    dataerror = "";
    datafilelist = "";
    if (datalabel != "" and starthrstr != "" and stophrstr != "") {
        datafile = "/" + datalabel + starthrstr + "-" + stophrstr + ".txt";
        // create new data file with 1st and 2nd 'descriptive' lines
        // 1st line
        loggeddata = "sensor data file: " + datalabel + starthrstr + "-" + stophrstr + ".txt\n";
        write_text(datafile, loggeddata);
        // 2nd line
        loggeddata = " #   time     degC  %RH   degC  degC  thres1 thres2";
        append_text(datafile, loggeddata);
    } else {
        dataerror = "sensor data file parameters have not been set";
        loggingstatus = false;
    }

    server.send(200, "text/html", HTMLlogdata()); 
}

void handle_stoplogging() {
    // ** do this if /stoplogging is requested
    r1 = "web server:";
    r2 = "data logging";
    r3 = "stopped";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    delay(1000);
    dataerror = "";
    datafilelist = "";
    Serial.println("stopping data logging");
    loggingstatus = false;
    datalabel =  "";
    starthrstr = "";
    stophrstr = "";
    server.send(200, "text/html", HTMLlogdata()); 
}

void handle_datafilelist() {
    // ** do this if /datafilelist is requested
    r1 = "web server:";
    r2 = "data file";
    r3 = "listing";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    delay(1000);
    dataerror = "";
    datafilelist = "";
    Serial.println("building data file listing");

    // list SPIFFs files and filter into a string those that have 'data' in their name
    File root = SPIFFS.open("/");
    File listfile = root.openNextFile();
    datafilelist +="<table border=\"1\" style=\"width: 350px;\" align=\"center\" cellpadding=\"3\" cellspacing=\"0\">\n";
    datafilelist +="<tr><td style=\"width: 200px; text-align: right;\">File name</td><td style=\"text-align: left;\">File size</td></tr>\n";
    while (listfile) {
        String filename = listfile.name();
        if (filename.indexOf("data") > 0) {
            datafilelist +="<tr><td style=\"text-align: right;\">";
            datafilelist +=listfile.name();
            datafilelist +="</td><td style=\"text-align: left;\">";
            datafilelist +=listfile.size();
            datafilelist +="</td></tr>\n";
        }
        listfile = root.openNextFile();
    }
    datafilelist +="</table>\n";

    server.send(200, "text/html", HTMLlogdata()); 
}

void handle_downloaddatafile() {
    // ** do this if /downloaddatafile is requested
    dataerror = "";
    datafilelist = "";
    if (downloadfile != "") {
        r1 = "web server:";
        r2 = "downloading";
        r3 = "data file";
        oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");	
        delay(1000);
        Serial.println("downloading data file");    	
        File opendatafile = SPIFFS.open("/" + downloadfile, "r");
        if (opendatafile) {
            server.sendHeader("Content-Disposition", "attachment; filename=" + downloadfile);
            server.streamFile(opendatafile, "application/octet-stream");
        } else {
            dataerror = "Sorry download failed: couldn't open the SPIFFS file";
        }

    } else {
        dataerror = "Sorry: the download data file has not been defined";
    }

    server.send(200, "text/html", HTMLlogdata()); 
}

void handle_deletedatafile() {
    // ** do this if /deletedatafile is requested
    dataerror = "";
    datafilelist = "";
    if (deletefile != "") {
        if (exists("/" + deletefile)) {
            r1 = "web server:";
            r2 = "deleting";
            r3 = "data file";
            oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");	
            delay(1000);
            Serial.print("deleting data file ");   
            Serial.println(deletefile);  	
            SPIFFS.remove("/" + deletefile);
            deletefile = "";
        } else {
            dataerror = "Sorry data file deletion failed: couldn't find the SPIFFS file";
        }

    } else {
        dataerror = "Sorry: the data file to be deleted has not been defined";
    }

    server.send(200, "text/html", HTMLlogdata()); 
}

// ******************************************************
// ***       sub selection run_tests 'handlers'       ***
// ******************************************************
void handle_buzzbeep() {
    // ** do this if /buzzbeep is requested
    r1 = "web server:";
    r2 = "beeping";
    r3 = "for 3 secs";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    Serial.println("testing buzzer");
    beep(3);
    server.send(200, "text/html", HTMLrun_tests()); 
}

void handle_testOLED() {
    // ** do this if /testOLED is requested
    r1 = "web server:";
    r2 = "abcdefghijklmnopqrst";
    r3 = "uvwxyz-!£$%^&*()+{}@";
    r4 = "<>?,./~#=_1234567890";
    oledtext(4, 0, 0, 0, 0, 12, r1, r2, r3, r4);
    Serial.println("testing OLED");
    delay(2000);
    server.send(200, "text/html", HTMLrun_tests()); 
}

void handle_sensors() {
    // ** do this if /sensors is requested

    // ****************************************
    //  get DHT11 temp + humidity measurements
    // ****************************************
    DHT11temphumid();

    // ********************************************
    //  get DS18B20 1-wire temperature measurements
    // ********************************************
    DS18temp();

    r1 = "web server:";
    r2 = "temp + humid";
    r3 = "sensed";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    Serial.println("testing temperature and humidity sensors");
    delay(2000);
    server.send(200, "text/html", HTMLrun_tests()); 
}

// *******************************************************
// *** parameter update submission detailed 'handlers' ***
// *******************************************************

// ****** detailed system parameter update submission actions ******
void handle_system_updates() {
    // ** do this if /system_updates is requested
    // get parameter strings from the browser response & convert as needed
    if (server.arg("namehost_str") != "") {
        namehost = server.arg("namehost_str");
        Serial.print("new namehost: ");
        Serial.println(namehost);
        write_text("/hostname.txt", namehost);
        // ** do an update to the common header HTML used in all web pages
        header_content = HTMLheader();
    }
    if (server.arg("location_str") != "") {
        location = server.arg("location_str");
        Serial.print("new location: ");
        Serial.println(location);
        location_str = location;
        write_text("/location.txt", location);
        // ** do an update to the common header HTML used in all web pages
        header_content = HTMLheader();
    }
    if (server.arg("minterval_str") != "") {
        minterval_str = server.arg("minterval_str");
        minterval = minterval_str.toInt();
        Serial.print("new minterval: ");
        Serial.println(minterval);
        write_text("/minterval.txt", minterval_str);
    }
    if (server.arg("alarmset_str") != "") {
        alarmset_str = server.arg("alarmset_str");
        alarmset = alarmset_str.toInt();
        Serial.print("new alarmset: ");
        Serial.println(alarmset);
        alarmrow(alarmset);  // sets row3 and alrow text
        write_text("/alarmset.txt", alarmset_str);
    }
    if (server.arg("a1threshold_str") != "") {
        a1threshold_str = server.arg("a1threshold_str");
        a1threshold = a1threshold_str.toFloat();
        write_text("/a1threshold.txt", a1threshold_str);
    }
    if (server.arg("a1criteria_str") != "") {
        a1criteria = server.arg("a1criteria_str");
        a1criteria_str = a1criteria;
        write_text("/a1criteria.txt", a1criteria);
    }
    if (server.arg("a2threshold_str") != "") {
        a2threshold_str = server.arg("a2threshold_str");
        a2threshold = a2threshold_str.toFloat();
        write_text("/a2threshold.txt", a2threshold_str);
    }
    if (server.arg("a2criteria_str") != "") {
        a2criteria = server.arg("a2criteria_str");
        a2criteria_str = a2criteria;
        write_text("/a2criteria.txt", a2criteria);
    }
    if (server.arg("a1open_str") != "") {
        a1open_str = server.arg("a1open_str");
        a1open = a1open_str.toInt();
        alarmrow(alarmset);  // sets row3 and alrow text
        write_text("/a1open.txt", a1open_str);
    }
    if (server.arg("a1close_str") != "") {
        a1close_str = server.arg("a1close_str");
        a1close = a1close_str.toInt();
        alarmrow(alarmset);  // sets row3 and alrow text
        write_text("/a1close.txt", a1close_str);
    }
    if (server.arg("a2open_str") != "") {
        a2open_str = server.arg("a2open_str");
        a2open = a2open_str.toInt();
        alarmrow(alarmset);  // sets row3 and alrow text
        write_text("/a2open.txt", a2open_str);
    }
    if (server.arg("a2close_str") != "") {
        a2close_str = server.arg("a2close_str");
        a2close = a2close_str.toInt();
        alarmrow(alarmset);  // sets row3 and alrow text
        write_text("/a2close.txt", a2close_str);
    }
    if (server.arg("smtp_host_str") != "") {
        smtp_host = server.arg("smtp_host_str");
        write_text("/smtphost.txt", smtp_host);
    }
    if (server.arg("author_email_str") != "") {
        author_email = server.arg("author_email_str");
        write_text("/authoremail.txt", author_email);
    }
    if (server.arg("author_password_str") != "") {
        author_password = server.arg("author_password_str");
        write_text("/authorpswd.txt", author_password);
    }
    if (server.arg("recipient_email_str") != "") {
        recipient_email = server.arg("recipient_email_str");
        write_text("/recipientemail.txt", recipient_email);
    }
    if (server.arg("recipient_name_str") != "") {
        recipient_name = server.arg("recipient_name_str");
        Serial.print("new recipient_name: ");
        Serial.println(recipient_name);
        write_text("/recipientname.txt", recipient_name);
    }
    if (server.arg("msg_sendname_str") != "") {
        msg_sendname = server.arg("msg_sendname_str");
        Serial.print("new msg_sendname: ");
        Serial.println(msg_sendname);
        write_text("/msgsendname.txt", msg_sendname);
    }
    if (server.arg("msg_senttext_str") != "") {
        msg_senttext = server.arg("msg_senttext_str");
        Serial.print("new msg_senttext: ");
        Serial.println(msg_senttext);
        write_text("/msgsenttext.txt", msg_senttext);
    }

    r1 = "web server:";
    r2 = "system";
    r3 = "updated";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    Serial.println("system parameters updated");
    delay(2000);
    server.send(200, "text/html", HTMLsystem_updates()); 
}

// ****** WiFi parameter updates submission actions ******
void handle_WiFi_params() {
    // ** do this if /WiFi_params is requested
    r1 = "web server:";
    r2 = "WiFi";
    r3 = "parameters";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    Serial.println("WiFi parameters main screen");
    delay(2000);
    server.send(200, "text/html", HTMLWiFi_params());
}

// *** WiFi update input 'handlers' ***
// -----------------------------------
void handle_WiFi_updates1() {
    // ** do this if /WiFi_updates1 is requested
    Serial.println("WiFi SSID1 settings update");
    ssid1 = server.arg("ssid_1");          // get string from browser response
    password1 = server.arg("password_1");  // get string from browser response
    // resave the WiFi SSID and its password
    write_text("/ssid1.txt", ssid1);
    write_text("/password1.txt", password1);
    r1 = "web server:";
    r2 = "ssid1";
    r3 = "updated";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    Serial.println("ssid1 and password updated");
    delay(2000);
    server.send(200, "text/html", HTMLWiFi_params());
}

void handle_WiFi_updates2() {
    // ** do this if /WiFi_updates2 is requested
    Serial.println("WiFi SSID2 settings update");
    ssid2 = server.arg("ssid_2");          // get string from browser response
    password2 = server.arg("password_2");  // get string from browser response
    // resave the WiFi SSID and its password
    write_text("/ssid2.txt", ssid2);
    write_text("/password2.txt", password2);
    r1 = "web server:";
    r2 = "ssid2";
    r3 = "updated";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    Serial.println("ssid2 and password updated");
    delay(2000);
    server.send(200, "text/html", HTMLWiFi_params());
}

void handle_WiFi_updates3() {
    // ** do this if /WiFi_updates3 is requested
    Serial.println("WiFi SSID3 settings update");
    ssid3 = server.arg("ssid_3");          // get string from browser response
    password3 = server.arg("password_3");  // get string from browser response
    // resave the WiFi SSID and its password
    write_text("/ssid3.txt", ssid3);
    write_text("/password3.txt", password3);
    r1 = "web server:";
    r2 = "ssid3";
    r3 = "updated";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    Serial.println("ssid3 and password updated");
    delay(2000);
    server.send(200, "text/html", HTMLWiFi_params());
}

void handle_WiFi_updates4() {
    // ** do this if /WiFi_updates4 is requested
    Serial.println("WiFi SSID4 settings update");
    ssid4 = server.arg("ssid_4");          // get string from browser response
    password4 = server.arg("password_4");  // get string from browser response
    // resave the WiFi SSID and its password
    write_text("/ssid4.txt", ssid4);
    write_text("/password4.txt", password4);
    r1 = "web server:";
    r2 = "ssid4";
    r3 = "updated";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    Serial.println("ssid4 and password updated");
    delay(2000);
    server.send(200, "text/html", HTMLWiFi_params());
}

void handle_WiFi_updates5() {
    // ** do this if /WiFi_updates5 is requested
    Serial.println("WiFi SSID5 settings update");
    ssid5 = server.arg("ssid_5");          // get string from browser response
    password5 = server.arg("password_5");  // get string from browser response
    // resave the WiFi SSID and its password
    write_text("/ssid5.txt", ssid5);
    write_text("/password5.txt", password5);
    r1 = "web server:";
    r2 = "ssid5";
    r3 = "updated";
    oledtext(3, 0, 0, 0, 0, 18, r1, r2, r3, "");
    Serial.println("ssid5 and password updated");
    delay(2000);
    server.send(200, "text/html", HTMLWiFi_params());
}


// ****************************************************************
// ******  create the various web pages that are being used  ******
// ****************************************************************

// --------------------------------------------------------------------------------
// create the header area used in all the web pages - done once in the setup stage
// --------------------------------------------------------------------------------
String HTMLheader() {
    String h_content = "<!DOCTYPE html> <html>\n";
    h_content +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=yes\">\n";
    h_content +="<title>";
    h_content +=location;
    h_content +="</title>\n";
	
    // style content in the page header ( to be replaced by a css file eventually )
    h_content +="<style>html { font-family: Verdana; display: inline-block; margin: 0px auto; text-align: center; font-size: 15px;}\n";
    h_content +="body{margin-top: 10px;} h1 {color: #444444; margin: 10px auto 10px; font-size: 32px;} h3 {color: #444444; margin: 10px auto 10px; font-size: 24px;} h4 {color: #444444; margin: 10px auto 10px; font-size: 18px;}\n";
    h_content +=".button {display: block; width: 80px; background-color: #1abc9c;border: none;color: white; padding: 0px 20px 10px 20px; text-decoration: none; font-size: 32px; margin: 5px auto 5px; cursor: pointer; border-radius: 4px;}\n";
    h_content +=".btninline {display: inline-block; }\n";
    h_content +=".button-on {background-color: #1abc9c;}\n";
    h_content +=".button-on:active {background-color: #16a085;}\n";
    h_content +=".button-off {background-color: #34495e;}\n";
    h_content +=".button-off:active {background-color: #2c3e50;}\n";
    h_content +=".button-red {background-color: #f51031;}\n";
    h_content +=".button-red:active {background-color: #d20e2a;}\n";
    h_content +="p {font-size: 18px; color: #888; margin: 5px;}\n";
    h_content +=".pred {font-size: 18px; color: red; margin: 5px; margin-bottom: 10px;}\n";
    h_content +="</style>\n";
    h_content +="</head>\n";

    // start of web page content
    h_content +="<body>\n";
    h_content +="<h1>";
    h_content += location;
    h_content +="</h1>\n";
    h_content +="<h1>ESP32 sensor-alarm<br/>Web Server (";
    h_content +=namehost;
    h_content +=")</h1>\n";

    return h_content;
}


// -----------------------------------------
// create the main selection web page
// -----------------------------------------
String HTMLmain(){
    String page_content = header_content;

    page_content +="<h3>main selections</h1>\n";
    if (a1status > 0 or a2status > 0) {
        page_content +="<p class=\"pred\">ALARM condition: <a href=\"alarm_details\">click here</a> for more details</p>\n";
    }
    page_content +="<p><a class=\"button btninline button-off\" href=\"run_tests\"><button>Component testing</button></a>&nbsp; &nbsp; &nbsp; ";
    page_content +="<a class=\"button btninline button-off\" href=\"parameters\"><button>Update parameters</button></a></p>\n";
    page_content +="<p><a class=\"button btninline button-off\" href=\"logdata\"><button>Log sensor data</button></a>&nbsp; &nbsp; &nbsp; ";
    page_content +="<a class=\"button btninline button-off\" href=\"sysinfo\"><button>System information</button></a></p>\n";

    page_content +="<p>&nbsp;</p>\n";
    page_content +="<p>&nbsp;</p>\n";

    page_content +="</body>\n";
    page_content +="</html>\n";
    return page_content;

}


// -----------------------------------------
// create the alarms web page
// -----------------------------------------
String HTMLalarm_details(){
    String page_content = header_content;

    page_content +="<h3>Alarm details: setting ";
    page_content += alarmset;
    page_content +="</h3>\n";

    page_content +="<p><table border=\"1\" style=\"width: 510px; text-align: center;\" align=\"center\" cellpadding=\"3\" cellspacing=\"0\">\n";
    page_content +="<tr><td style=\"width: 70px; \">Alarm</td>" ;
    page_content +="<td style=\"width: 80px; \">Criteria</td>" ;
    page_content +="<td style=\"width: 90px; \">Threshold</td>" ;
    page_content +="<td style=\"width: 100px; \">Window</td>" ;
    page_content +="<td style=\"width: 80px; \">Reading</td>" ;
    page_content +="<td style=\"width: 90px; \">Condition</td></tr>\n" ;

    page_content +="<tr><td>1</td>";
    page_content +="<td>";
    page_content +=a1criteria;
    page_content +="</td>";
    page_content +="<td>";
    page_content +=a1threshold_str;
    page_content +="</td>";
    page_content +="<td>";
    page_content +=a1open_str;
    page_content +=" - ";
    page_content +=a1close_str;
    page_content +="</td>";
    page_content +="<td>";
    page_content +=ds18tempstr[1];
    page_content +="</td>";
    if (alarmset == 1 or alarmset == 3 or alarmset == 5) {
        page_content +="<td>not set</td></tr>\n";
    } else if (a1status > 0) {
        page_content +="<td>ALARM</td></tr>\n";
    } else {
        page_content +="<td>OK</td></tr>\n";
    }

    page_content +="<tr><td>2</td>";
    page_content +="<td>";
    page_content +=a2criteria;
    page_content +="</td>";
    page_content +="<td>";
    page_content +=a2threshold_str;
    page_content +="</td>";
    page_content +="<td>";
    page_content +=a2open_str;
    page_content +=" - ";
    page_content +=a2close_str;
    page_content +="</td>";
    page_content +="<td>";
    page_content +=ds18tempstr[2];
    page_content +="</td>";
    if (alarmset == 1 or alarmset == 2 or alarmset == 5) {
        page_content +="<td>not set</td></tr></table></p>\n";
    } else if (a2status > 0) {
        page_content +="<td>ALARM</td></tr></table></p>\n";
    } else {
        page_content +="<td>OK</td></tr></table></p>\n";
    }

    page_content +="<p>&nbsp;</p>\n";

    // section to simply list the alarmset settings
    page_content +="<p><table border=\"1\" style=\"width: 300px; text-align: center;\" align=\"center\" cellpadding=\"3\" cellspacing=\"0\">\n";
    page_content +="<tr><td>alarm setting</td><td>sensor 1</td><td>sensor 2</td></tr>\n";
    page_content +="<tr><td>1</td><td>n/a</td><td>n/a</td></tr>\n";
    page_content +="<tr><td>2</td><td>ON</td><td>OFF</td></tr>\n";
    page_content +="<tr><td>3</td><td>OFF</td><td>ON</td></tr>\n";
    page_content +="<tr><td>4</td><td>ON</td><td>ON</td></tr>\n";
    page_content +="<tr><td>5</td><td>OFF</td><td>OFF</td></tr></table></p>\n";

    page_content +="<p>&nbsp;</p>\n";

    page_content +="<p><a class=\"button button-off\" href=\"/\"><button>back to main selection</button></a></p>\n";
    page_content +="</body>\n";
    page_content +="</html>\n";
    return page_content;
}


// ---------------------------------------------
// create the system information display web page
// ---------------------------------------------
String HTMLsysinfo(){
    String page_content = header_content;

    page_content +="<h3>System Information - software v";
    page_content +=version;
    page_content +="</h3>\n";

    // **** networking information ****
    page_content +="<div style=\" font-size: 18px; margin-bottom: 5px; margin-top: 10px;\"><b>Networking:</b></div>\n";
    page_content +="<table border=\"1\" style=\"width: 450px; text-align: left;\" align=\"center\" cellpadding=\"3\" cellspacing=\"0\">\n";
    page_content +="<tr><td style=\"width: 225px; \">connected to WiFi SSID:</td><td>" ;
    page_content +=ssid_selected;
    page_content +="</td></tr>\n";
    page_content +="<tr><td>host name:</td><td>" ;
    page_content +=WiFi.getHostname();
    page_content +="</td></tr>\n";
    page_content +="<tr><td>assigned IP address:</td><td>" ;
    page_content +=WiFi.localIP().toString();
    page_content +="</td></tr>\n";
    page_content +="<tr><td>WiFi MAC address:</td><td>" ;
    page_content +=WiFi.macAddress().c_str();
    page_content +="</td></tr>\n";
    page_content +="</table>\n";

    // **** file system (SPIFFS) ****
    page_content +="<div style=\" font-size: 18px; margin-bottom: 5px; margin-top: 10px;\"><b>File System (SPI Flash File System):</b></div>\n";
    page_content +="<table border=\"1\" style=\"width: 450px; text-align: left;\" align=\"center\" cellpadding=\"3\" cellspacing=\"0\">\n";
    page_content +="<tr><td style=\"width: 225px; \">Total KB:</td><td>" ;
    page_content +=String((float)SPIFFS.totalBytes() / 1024.0);
    page_content +="</td></tr>\n";
    page_content +="<tr><td style=\"width: 225px; \">Used KB:</td><td>" ;
    page_content +=String((float)SPIFFS.usedBytes() / 1024.0);
    page_content +="</td></tr>\n";
    page_content +="</table>\n";

    // **** memory information ****
    page_content +="<div style=\" font-size: 18px; margin-bottom: 5px; margin-top: 10px;\"><b>Memory information: Internal RAM</b></div>\n";
    page_content +="<table border=\"1\" style=\"width: 450px; text-align: left;\" align=\"center\" cellpadding=\"3\" cellspacing=\"0\">\n";

    //page_content +="<tr><td style=\"width: 225px; \">Total heap size:</td><td>" ;
    //page_content +=String(ESP.String(ESP.getHeapSize());
    //page_content +="</td></tr>\n";

    page_content +="<tr><td style=\"width: 225px; \">Available heap:</td><td>" ;
    page_content +=String(ESP.getFreeHeap());
    page_content +="</td></tr>\n";

    page_content +="<tr><td style=\"width: 225px; \">lowest level of free heap since boot:</td><td>" ;
    page_content +=String(ESP.getMinFreeHeap());
    page_content +="</td></tr>\n";

    page_content +="<tr><td style=\"width: 225px; \">largest block of heap that can be allocated at once:</td><td>" ;
    page_content +=String(ESP.getMaxAllocHeap());
    page_content +="</td></tr>\n";

    page_content +="</table>\n";

    page_content +="<div style=\" font-size: 18px; margin-bottom: 5px; margin-top: 10px;\"><b>Memory information: SPI RAM</b></div>\n";
    page_content +="<table border=\"1\" style=\"width: 450px; text-align: left;\" align=\"center\" cellpadding=\"3\" cellspacing=\"0\">\n";

    page_content +="<tr><td style=\"width: 225px; \">Total RAM size:</td><td>" ;
    page_content +=String(ESP.getPsramSize());
    page_content +="</td></tr>\n";

    page_content +="<tr><td style=\"width: 225px; \">Free RAM:</td><td>" ;
    page_content +=String(ESP.getFreePsram());
    page_content +="</td></tr>\n";

    page_content +="<tr><td style=\"width: 225px; \">Minimum free RAM:</td><td>" ;
    page_content +=String(ESP.getMinFreePsram());
    page_content +="</td></tr>\n";

    page_content +="<tr><td style=\"width: 225px; \">Maximum allocatable RAM:</td><td>" ;
    page_content +=String(ESP.getMaxAllocPsram());
    page_content +="</td></tr>\n";

    page_content +="</table>\n";

    // **** chip and firmware information ****
    page_content +="<div style=\" font-size: 18px; margin-bottom: 5px; margin-top: 10px;\"><b>Chip and Firmware information:</b></div>\n";
    page_content +="<table border=\"1\" style=\"width: 450px; text-align: left;\" align=\"center\" cellpadding=\"3\" cellspacing=\"0\">\n";

    page_content +="<tr><td style=\"width: 225px; \">chip revision::</td><td>" ;
    page_content +=String(ESP.getChipRevision());
    page_content +="</td></tr>\n";

    page_content +="<tr><td>Flash chip size:</td><td>" ;
    page_content +=ESP.getFlashChipSize();
    page_content +="</td></tr>\n";

    page_content +="<tr><td>SDK version::</td><td>" ;
    page_content +=String(ESP.getSdkVersion());
    page_content +="</td></tr>\n";

    page_content +="</table>\n";

    page_content +="<p>&nbsp;</p>\n";

    page_content +="<p><a class=\"button button-off\" href=\"/\"><button>back to main selection</button></a></p>\n";
    page_content +="</body>\n";
    page_content +="</html>\n";
    return page_content;
}


// ---------------------------------------------
// create the parameter type selection web page
// ---------------------------------------------
String HTMLparameter_selection(){
    String page_content = header_content;
;
    page_content +="<h3>parameter information</h3>\n";
    page_content +="<p><a class=\"button button-off\" href=\"WiFi_params\"><button>update WiFi parameters</button></a></p>\n";
    page_content +="<p><a class=\"button button-off\" href=\"system_updates\"><button>system parameter details</button></a></p>\n";
    page_content +="<p><a class=\"button button-off\" href=\"alarm_details\"><button>show alarm details</button></a></p>\n";

    page_content +="<p>&nbsp;</p>\n";
    page_content +="<p><a class=\"button button-off\" href=\"/\"><button>back to main selection</button></a></p>\n";

    page_content +="</body>\n";
    page_content +="</html>\n";
    return page_content;
}


// ---------------------------------------------
// create the WiFi parameter update web page
// ---------------------------------------------
String HTMLWiFi_params(){
    String page_content = header_content;

    page_content +="<h3>WiFi parameter update</h3>\n";
    page_content +="<h3>input/update SSID name and WEP key</h3>\n";

    // input sections of the web page

    page_content +="<form method=\"post\" action=\"/WiFi_updates1\"> \n";
    page_content +="SSID1: \n";
    page_content +="<input type=\"text\" name=\"ssid_1\" size=\"12\" value= ";
    page_content +=ssid1;
    page_content += "> &nbsp; &nbsp; <input type=\"text\" name=\"password_1\"  ";
    page_content += " size=\"12\" placeholder=\"********\" > ";
    page_content +=" &nbsp; &nbsp; <input type=\"submit\" value=\"Submit\">\n";
    page_content +="</form>\n";

    page_content +="<p>&nbsp;</p>\n";

    page_content +="<form method=\"post\" action=\"/WiFi_updates2\"> \n";
    page_content +="SSID2: \n";
    page_content +="<input type=\"text\" name=\"ssid_2\" size=\"12\" value= ";
    page_content +=ssid2;
    page_content += "> &nbsp; &nbsp; <input type=\"text\" name=\"password_2\"  ";
    page_content += " size=\"12\" placeholder=\"********\" > ";
    page_content +=" &nbsp; &nbsp; <input type=\"submit\" value=\"Submit\">\n";
    page_content +="</form>\n";

    page_content +="<p>&nbsp;</p>\n";

    page_content +="<form method=\"post\" action=\"/WiFi_updates3\"> \n";
    page_content +="SSID3: \n";
    page_content +="<input type=\"text\" name=\"ssid_3\" size=\"12\" value= ";
    page_content +=ssid3;
    page_content += "> &nbsp; &nbsp; <input type=\"text\" name=\"password_3\"  ";
    page_content += " size=\"12\" placeholder=\"********\" > ";
    page_content +=" &nbsp; &nbsp; <input type=\"submit\" value=\"Submit\">\n";
    page_content +="</form>\n";

    page_content +="<p>&nbsp;</p>\n";

    page_content +="<form method=\"post\" action=\"/WiFi_updates4\"> \n";
    page_content +="SSID4: \n";
    page_content +="<input type=\"text\" name=\"ssid_4\" size=\"12\" value= ";
    page_content +=ssid4;
    page_content += "> &nbsp; &nbsp; <input type=\"text\" name=\"password_4\"  ";
    page_content += " size=\"12\" placeholder=\"********\" > ";
    page_content +=" &nbsp; &nbsp; <input type=\"submit\" value=\"Submit\">\n";
    page_content +="</form>\n";

    page_content +="<p>&nbsp;</p>\n";

    page_content +="<form method=\"post\" action=\"/WiFi_updates5\"> \n";
    page_content +="SSID5: \n";
    page_content +="<input type=\"text\" name=\"ssid_5\" size=\"12\" value= ";
    page_content +=ssid5;
    page_content += "> &nbsp; &nbsp; <input type=\"text\" name=\"password_5\"  ";
    page_content += " size=\"12\" placeholder=\"********\" > ";
    page_content +=" &nbsp; &nbsp; <input type=\"submit\" value=\"Submit\">\n";
    page_content +="</form>\n";

    page_content +="<p>&nbsp;</p>\n";

    page_content +="<p><a class=\"button button-off\" href=\"/\"><button>back to main selection</button></a></p>\n";
    page_content +="<p>&nbsp;</p>\n";

    page_content +="</body>\n";
    page_content +="</html>\n";
    return page_content;
}


// ---------------------------------------------
// create the component testing web page
// ---------------------------------------------
String HTMLrun_tests(){
    String page_content = header_content;

    page_content +="<h3>component testing</h3>\n";

    page_content +="<p><a class=\"button btninline button-off\" href=\"buzzbeep\"><button>test buzzer</button></a>&nbsp; &nbsp; &nbsp; ";
    page_content +="<a class=\"button btninline button-off\" href=\"testOLED\"><button>test OLED</button></a></p>\n";

    page_content +="<p>Sensor data: </p>\n";

    page_content +="<table border=\"0\" style=\"width: 450px; text-align: left;\" align=\"center\" cellpadding=\"3\" cellspacing=\"0\">\n";

    page_content +="<tr><td style=\"width: 330px; text-align: right;\">DS1820B sensor 1 temperature (<sup>o</sup>C):</td>" ;
    page_content +="<td>";
    page_content +=ds18tempstr[1];
    page_content +="</td></tr>\n";

    page_content +="<tr><td style=\"width: 330px; text-align: right;\">DS1820B sensor 2 temperature (<sup>o</sup>C):</td>" ;
    page_content +="<td>";
    page_content +=ds18tempstr[2];
    page_content +="</td></tr>\n";

    page_content +="<tr><td style=\"width: 330px; text-align: right;\">DHT11 humidity (% RH):</td>" ;
    page_content +="<td>";
    page_content +=String(dhthumid);
    page_content +="</td></tr>\n";

    page_content +="<tr><td style=\"width: 330px; text-align: right;\">DHT11 temperature (<sup>o</sup>C):</td>" ;
    page_content +="<td>";
    page_content +=String(dhttemp);
    page_content +="</td></tr>\n";

    page_content +="</table>\n";
    page_content +="<p>&nbsp;</p>\n";

    page_content +="<p><a class=\"button btninline button-off\" href=\"sensors\"><button>update sensor data</button></a></p>\n";

    page_content +="<p>&nbsp;</p>\n";
    page_content +="<p><a class=\"button button-off\" href=\"/\"><button>back to main selection</button></a></p>\n";
    page_content +="<p>&nbsp;</p>\n";

    page_content +="</body>\n";
    page_content +="</html>\n";
    return page_content;

}


// -----------------------------------------------------
// create the system parameter update web page
// -----------------------------------------------------
String HTMLsystem_updates(){
    String page_content = header_content;

    page_content +="<h3>system parameters</h3>\n";
    page_content +="<h4>input/update the individual operational parameters</h4>\n";

    // input sections of the web page

    page_content +="<table border=\"0\" style=\"width: 550px; text-align: left;\" align=\"center\" cellpadding=\"3\" cellspacing=\"0\">\n";

    page_content +="<form method=\"post\" action=\"/system_updates\"> \n";
    page_content +="<tr><td style=\"width: 300px; text-align: right;\">host name:</td><td>" ;
    page_content +="<input type=\"text\" name=\"namehost_str\" size=\"12\" value= ";
    page_content +=namehost;
    page_content +=" ></td>";
    page_content +="<td><input type=\"submit\" value=\"Submit\"></td></tr>\n";
    page_content +="</form>\n";

    page_content +="<form method=\"post\" action=\"/system_updates\"> \n";
    page_content +="<tr><td style=\"text-align: right;\">location description:</td><td>" ;
    page_content +="<input type=\"text\" name=\"location_str\" size=\"12\" value=\"";
    page_content +=location;
    page_content +="\" ></td>";
    page_content +="<td><input type=\"submit\" value=\"Submit\"></td></tr>\n";
    page_content +="</form>\n";

    page_content +="<form method=\"post\" action=\"/system_updates\"> \n";
    page_content +="<tr><td style=\"text-align: right;\">measurement interval (secs):</td><td>" ;
    page_content +="<input type=\"text\" name=\"minterval_str\" size=\"12\" value= ";
    page_content +=String(minterval);
    page_content +=" ></td>";
    page_content +="<td><input type=\"submit\" value=\"Submit\"></td></tr>\n";
    page_content +="</form>\n";

    page_content +="<form method=\"post\" action=\"/system_updates\"> \n";
    page_content +="<tr><td style=\"text-align: right;\">alarm mix setting (1 to 5):</td><td>" ;
    page_content +="<input type=\"text\" name=\"alarmset_str\" size=\"12\" value= ";
    page_content +=String(alarmset);
    page_content +=" ></td>";
    page_content +="<td><input type=\"submit\" value=\"Submit\"></td></tr>\n";
    page_content +="</form>\n";

    page_content +="<form method=\"post\" action=\"/system_updates\"> \n";
    page_content +="<tr><td style=\"text-align: right;\">alarm 1 threshold (<sup>o</sup>C):</td><td>" ;
    page_content +="<input type=\"text\" name=\"a1threshold_str\" size=\"12\" value= ";
    page_content +=String(a1threshold);
    page_content +=" ></td>";
    page_content +="<td><input type=\"submit\" value=\"Submit\"></td></tr>\n";
    page_content +="</form>\n";

    page_content +="<form method=\"post\" action=\"/system_updates\"> \n";
    page_content +="<tr><td style=\"text-align: right;\">alarm 1 criteria ( lt or gt ):</td><td>" ;
    page_content +="<input type=\"text\" name=\"a1criteria_str\" size=\"12\" value= ";
    page_content +=a1criteria;
    page_content +=" ></td>";
    page_content +="<td><input type=\"submit\" value=\"Submit\"></td></tr>\n";
    page_content +="</form>\n";

    page_content +="<form method=\"post\" action=\"/system_updates\"> \n";
    page_content +="<tr><td style=\"text-align: right;\">alarm 2 threshold (<sup>o</sup>C):</td><td>" ;
    page_content +="<input type=\"text\" name=\"a2threshold_str\" size=\"12\" value= ";
    page_content +=String(a2threshold);
    page_content +=" ></td>";
    page_content +="<td><input type=\"submit\" value=\"Submit\"></td></tr>\n";
    page_content +="</form>\n";

    page_content +="<form method=\"post\" action=\"/system_updates\"> \n";
    page_content +="<tr><td style=\"text-align: right;\">alarm 2 criteria ( lt or gt ):</td><td>" ;
    page_content +="<input type=\"text\" name=\"a2criteria_str\" size=\"12\" value= ";
    page_content +=a2criteria;
    page_content +=" ></td>";
    page_content +="<td><input type=\"submit\" value=\"Submit\"></td></tr>\n";
    page_content +="</form>\n";

    page_content +="<form method=\"post\" action=\"/system_updates\"> \n";
    page_content +="<tr><td style=\"text-align: right;\">alarm 1: begin at the start of this hour</td><td>" ;
    page_content +="<input type=\"text\" name=\"a1open_str\" size=\"12\" value= ";
    page_content +=String(a1open);
    page_content +=" ></td>";
    page_content +="<td><input type=\"submit\" value=\"Submit\"></td></tr>\n";
    page_content +="</form>\n";

    page_content +="<form method=\"post\" action=\"/system_updates\"> \n";
    page_content +="<tr><td style=\"text-align: right;\">alarm 1: stop at the end of this hour</td><td>" ;
    page_content +="<input type=\"text\" name=\"a1close_str\" size=\"12\" value= ";
    page_content +=String(a1close);
    page_content +=" ></td>";
    page_content +="<td><input type=\"submit\" value=\"Submit\"></td></tr>\n";
    page_content +="</form>\n";

    page_content +="<form method=\"post\" action=\"/system_updates\"> \n";
    page_content +="<tr><td style=\"text-align: right;\">alarm 2: begin at the start of this hour</td><td>" ;
    page_content +="<input type=\"text\" name=\"a2open_str\" size=\"12\" value= ";
    page_content +=String(a2open);
    page_content +=" ></td>";
    page_content +="<td><input type=\"submit\" value=\"Submit\"></td></tr>\n";
    page_content +="</form>\n";

    page_content +="<form method=\"post\" action=\"/system_updates\"> \n";
    page_content +="<tr><td style=\"text-align: right;\">alarm 2: stop at the end of this hour</td><td>" ;
    page_content +="<input type=\"text\" name=\"a2close_str\" size=\"12\" value= ";
    page_content +=String(a2close);
    page_content +=" ></td>";
    page_content +="<td><input type=\"submit\" value=\"Submit\"></td></tr>\n";
    page_content +="</form>\n";

    page_content +="<form method=\"post\" action=\"/system_updates\"> \n";
    page_content +="<tr><td style=\"text-align: right;\">SMTP host</td><td>" ;
    page_content +="<input type=\"text\" name=\"smtp_host_str\" size=\"12\" value= ";
    page_content +=smtp_host;
    page_content +=" ></td>";
    page_content +="<td><input type=\"submit\" value=\"Submit\"></td></tr>\n";
    page_content +="</form>\n";

    page_content +="<form method=\"post\" action=\"/system_updates\"> \n";
    page_content +="<tr><td style=\"text-align: right;\">sending email address</td><td>" ;
    page_content +="<input type=\"text\" name=\"author_email_str\" size=\"12\" value= ";
    page_content +=author_email;
    page_content +=" ></td>";
    page_content +="<td><input type=\"submit\" value=\"Submit\"></td></tr>\n";
    page_content +="</form>\n";

    page_content +="<form method=\"post\" action=\"/system_updates\"> \n";
    page_content +="<tr><td style=\"text-align: right;\">sending email name</td><td>" ;
    page_content +="<input type=\"text\" name=\"msg_sendname_str\" size=\"12\" value=\"";
    page_content +=msg_sendname;
    page_content +="\" ></td>";
    page_content +="<td><input type=\"submit\" value=\"Submit\"></td></tr>\n";
    page_content +="</form>\n";

    page_content +="<form method=\"post\" action=\"/system_updates\"> \n";
    page_content +="<tr><td style=\"text-align: right;\">sending email address password</td><td>" ;
    page_content +="<input type=\"text\" name=\"author_password_str\" size=\"12\" value= ";
    page_content +=author_password;
    page_content +=" ></td>";
    page_content +="<td><input type=\"submit\" value=\"Submit\"></td></tr>\n";
    page_content +="</form>\n";

    page_content +="<form method=\"post\" action=\"/system_updates\"> \n";
    page_content +="<tr><td style=\"text-align: right;\">recipient email address</td><td>" ;
    page_content +="<input type=\"text\" name=\"recipient_email_str\" size=\"12\" value= ";
    page_content +=recipient_email;
    page_content +=" ></td>";
    page_content +="<td><input type=\"submit\" value=\"Submit\"></td></tr>\n";
    page_content +="</form>\n";

    page_content +="<form method=\"post\" action=\"/system_updates\"> \n";
    page_content +="<tr><td style=\"text-align: right;\">recipient email name</td><td>" ;
    page_content +="<input type=\"text\" name=\"recipient_name_str\" size=\"12\" value=\"";
    page_content +=recipient_name;
    page_content +="\" ></td>";
    page_content +="<td><input type=\"submit\" value=\"Submit\"></td></tr>\n";
    page_content +="</form>\n";

    page_content +="<form method=\"post\" action=\"/system_updates\"> \n";
    page_content +="<tr><td style=\"text-align: right;\">sent message footer text</td><td>" ;
    page_content +="<input type=\"text\" name=\"msg_senttext_str\" size=\"12\" value=\"";
    page_content +=msg_senttext;
    page_content +="\" ></td>";
    page_content +="<td><input type=\"submit\" value=\"Submit\"></td></tr>\n";
    page_content +="</form>\n";

    page_content +="</table>\n";
    page_content +="<p>&nbsp;</p>\n";
    

    page_content +="<p><a class=\"button button-off\" href=\"/parameters\"><button>back to parameter selection</button></a>&nbsp; &nbsp; &nbsp; ";
    page_content +="<a class=\"button button-off\" href=\"/\"><button>back to main selection</button></a></p>\n";
    page_content +="<p>&nbsp;</p>\n";

    page_content +="</body>\n";
    page_content +="</html>\n";
    return page_content;
}


// -----------------------------------------------------
// create the sensor data logging web page
// -----------------------------------------------------
String HTMLlogdata(){
    String page_content = header_content;

    page_content +="<h3>Logging sensor data</h3>\n";
    page_content +="<h4>input/update the individual operational parameters</h4>\n";
    page_content +="<h4> .. and START/STOP the logging process</h4>\n";

    // display the data error text if it has been populated
    if (dataerror != "") {
        page_content +="<p class=\"pred\">";
        page_content +=dataerror;
        page_content +="</p>\n";
    }

    // display the data logging status
    if (loggingstatus) {
        page_content +="<h4>Data logging is active</h4>\n";
    } else {
        page_content +="<h4>Data logging is NOT active</h4>\n";
    }

    // list the filtered SPIFFS file listing to show the available sensor data logging files
    if (datafilelist != "") {
        page_content +="<p>&nbsp;</p>\n";
        page_content +="<p>";
        page_content +=datafilelist;
        page_content +="</p>\n";
        page_content +="<p>&nbsp;</p>\n";
    }

    // input main data logging parameters sections of the web page
    page_content +="<form method=\"post\" action=\"/logging_updates\"> \n";

    page_content +="<table border=\"0\" style=\"width: 550px; text-align: left;\" align=\"center\" cellpadding=\"3\" cellspacing=\"0\">\n";

    page_content +="<tr><td style=\"width: 400px; text-align: right;\">logging begins at the start of this hour (00 to 23):</td><td>" ;
    page_content +="<input type=\"text\" name=\"starthrstr\" size=\"20\" value= ";
    page_content +=starthrstr;
    page_content +=" ></td></tr>\n";

    page_content +="<tr><td style=\"width: 300px; text-align: right;\">logging stops at the end of this hour (00 to 23):</td><td>" ;
    page_content +="<input type=\"text\" name=\"stophrstr\" size=\"20\" value= ";
    page_content +=stophrstr;
    page_content +=" ></td></tr>\n";

    page_content +="<tr><td style=\"width: 300px; text-align: right;\">data label text (must include the text <i>data</i>):</td><td>" ;
    page_content +="<input type=\"text\" name=\"datalabel_str\" size=\"20\" value= ";
    page_content +=datalabel;
    page_content +=" ></td></tr>\n";

    page_content +="<tr><td style=\"width: 300px; text-align: right;\">data logging file to be downloaded:</td><td>" ;
    page_content +="<input type=\"text\" name=\"downloadfile_str\" size=\"20\" value= ";
    page_content +=downloadfile;
    page_content +=" ></td></tr>\n";

    page_content +="<tr><td style=\"width: 300px; text-align: right;\">data logging file to be deleted:</td><td>" ;
    page_content +="<input type=\"text\" name=\"deletefile_str\" size=\"20\" value= ";
    page_content +=deletefile;
    page_content +=" ></td></tr>\n";
    page_content +="</table>\n";
    page_content +="<p>&nbsp;</p>\n";
    page_content +="<input type=\"submit\" value=\"Submit\">\n";
    page_content +="<p>&nbsp;</p>\n";

    page_content +="</form>\n";

    page_content +="<p><a class=\"button btninline button-off\" href=\"startlogging\"><button>Start logging</button></a>&nbsp; &nbsp; &nbsp; ";
    page_content +="<a class=\"button btninline button-off\" href=\"stoplogging\"><button>Stop logging</button></a></p>\n";

    page_content +="<p><a class=\"button btninline button-off\" href=\"datafilelist\"><button>List the data files stored in SPIFFS</button></a></p>\n";

    page_content +="<a class=\"button btninline button-off\" href=\"downloaddatafile\"><button>Download selected data file</button></a>&nbsp; &nbsp; &nbsp; ";
    page_content +="<a class=\"button btninline button-off\" href=\"deletedatafile\"><button>Delete selected data file</button></a></p>\n";

    page_content +="<p>&nbsp;</p>\n";
    page_content +="<p><a class=\"button button-off\" href=\"/\"><button>back to main selection</button></a></p>\n";
    page_content +="<p>&nbsp;</p>\n";

    page_content +="</body>\n";
    page_content +="</html>\n";
    return page_content;
}
