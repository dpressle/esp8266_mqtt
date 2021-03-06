/*
 *  This sketch is running a web server for configuring WiFI if can't connect or for controlling of one GPIO to switch a light/LED
 *  Also it supports to change the state of the light via MQTT message and gives back the state after change.
 *  The push button has to switch to ground. It has following functions: Normal press less than 1 sec but more than 50ms-> Switch light. Restart press: 3 sec -> Restart the module. Reset press: 20 sec -> Clear the settings in EEPROM
 *  While a WiFi config is not set or can't connect:
 *    http://server_ip will give a config page with
 *  While a WiFi config is set:
 *    http://server_ip/gpio -> Will display the GIPIO state and a switch form for it
 *    http://server_ip/gpio?state=0 -> Will change the GPIO directly and display the above aswell
 *    http://server_ip/cleareeprom -> Will reset the WiFi setting and rest to configure mode as AP
 *  server_ip is the IP address of the ESP8266 module, will be
 *  printed to Serial when the module is connected. (most likly it will be 192.168.4.1)
 * To force AP config mode, press button 20 Secs!
 *  For several snippets used, the credit goes to:
 *  - https://github.com/esp8266
 *  - https://github.com/chriscook8/esp-arduino-apboot
 *  - https://github.com/knolleary/pubsubclient
 *  - https://github.com/vicatcu/pubsubclient <- Currently this needs to be used instead of the origin
 *  - https://gist.github.com/igrr/7f7e7973366fc01d6393
 *  - http://www.esp8266.com/viewforum.php?f=25
 *  - http://www.esp8266.com/viewtopic.php?f=29&t=2745
 *  - And the whole Arduino and ESP8266 comunity
 */

#include <ESP8266WiFi.h>
//#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <Ticker.h>
#include <PubSubClient.h>
//#include <RCSwitch.h>

extern "C" {
#include "user_interface.h" //Needed for the reset command
}

#define MQTT_PORT 1883
//#define PULSE_LENGTH 292
//#define RC_BITS 24
#define RELAY_DELAY_TIME 250

//***** Settings declare *********************************************************************************************************
String ssid = "ESP"; //The ssid when in AP mode
String clientName = "ESP"; //The MQTT ID -> MAC adress will be added to make it kind of unique
//String FQDN ="Esp8266.local"; //The DNS hostname - Does not work yet?
//int iotMode=1; //IOT mode: 0 = Web control, 1 = MQTT (No const since it can change during runtime)
//select GPIO's
const int outPin = 12; //output pin
const int wifiLed = 13; //led light indicator pin for wifi connected status
//const int mqttLed = 14; //led light indicator pin for mqtt connected status
const int inPin = 0;  // input pin (push button)

const int restartDelay = 3; //minimal time for button press to reset in sec
const int humanpressDelay = 50; // the delay in ms untill the press should be handled as a normal push by human. Button debouce. !!! Needs to be less than restartDelay & resetDelay!!!
const int resetDelay = 5; //Minimal time for button press to reset all settings and boot to config mode in sec
//const int configDelay = 10;

const int debug = 0; //Set to 1 to get more log to serial
//##### Object instances #####
//MDNSResponder mdns;
ESP8266WebServer server(80);
WiFiClient wifiClient;
PubSubClient mqttClient;
//RCSwitch mySwitch = RCSwitch();
Ticker btn_timer;
Ticker led_timer;

//##### Flags ##### They are needed because the loop needs to continue and cant wait for long tasks!
int rstNeed = 0; // Restart needed to apply new settings
//int toPub=0; // determine if state should be published.
int eepromToClear = 0; // determine if EEPROM should be cleared.

//##### Global vars #####
int restartFlag = 0;
int webtypeGlob;
int current; //Current state of the button
unsigned long count = 0; //Button press time counter
String st; //WiFi Stations HTML list
char buf[40]; //For MQTT data recieve

//To be read from EEPROM Config
String esid = "";
String epass = "";
String mqttServer = "";
String mqttServerUserName = "";
String mqttServerPassword = "";
String subTopic;
//String pubTopic;
//String pulseLength;

//-------------- void's -------------------------------------------------------------------------------------
void setup() {
  Serial.begin(9600);
  delay(10);
  // prepare OUTPUT pins
  digitalWrite(outPin, LOW);
  pinMode(outPin, OUTPUT);
  digitalWrite(wifiLed, LOW);
  pinMode(wifiLed, OUTPUT);
  //digitalWrite(mqttLed, LOW);
  //pinMode(mqttLed, OUTPUT);
  digitalWrite(inPin, HIGH);
  pinMode(inPin, INPUT_PULLUP);

  btn_timer.attach(0.05, btn_handle);
  //mySwitch.enableTransmit(outPin);
 // mySwitch.setPulseLength(PULSE_LENGTH);

  // Load the configuration from the eeprom
  loadConfig();
  // Connect to WiFi network
  initWiFi();
}

void loadConfig() {
  EEPROM.begin(512);
  delay(10);

  //len: 32
  Serial.print("SSID: ");
  esid = readEeprom(0, 32);
  Serial.println(esid);

  //len: 64+32=96
  Serial.print("PASS: ");
  epass = readEeprom(32, 64);
  Serial.println(epass);

  //len: 32+96=128
  Serial.print("Broker IP: ");
  mqttServer = readEeprom(96, 32);
  Serial.println(mqttServer);

  //len: 32+128=160
  Serial.print("Broker user name: ");
  mqttServerUserName = readEeprom(128, 32);
  Serial.println(mqttServerUserName);

  //len: 64+160=224
  Serial.print("MQTT Broker password: ");
  mqttServerPassword = readEeprom(160, 64);
  Serial.println(mqttServerPassword);

  //len: 64+224=288
  Serial.print("MQTT subscribe topic: ");
  subTopic = readEeprom(224, 64);
  Serial.println(subTopic);

  EEPROM.end();
}

void initWiFi() {
  led_timer.attach(2, led_handle);
  Serial.println();
  Serial.println("Startup");
  esid.trim();
  if ( esid != "" ) {
    // test esid
    WiFi.disconnect();
    delay(100);
    WiFi.mode(WIFI_STA);
    Serial.print("Connecting to WiFi ");
    Serial.println(esid);
    WiFi.begin(esid.c_str(), epass.c_str());
    if ( testWifi() == 20 ) {
      led_timer.attach(0.5, led_handle);//chnage the led blink speed to indicate we are connected to wifi
      launchWeb(0);
      return;
    }
  } else {
    Serial.println("SSID is empty Opening AP");
    setupAP();
  }
}

int testWifi(void) {
  int c = 0;
  Serial.println("Wifi test...");
  while ( c < 40 ) {
    if (WiFi.status() == WL_CONNECTED) {
      return (20);
    }
    delay(500);
    Serial.print(".");
    c++;
  }
  Serial.println("WiFi Connect timed out");
  return (10);
}

void launchWeb(int webtype) {
  Serial.println("");
  Serial.println("WiFi connected");
  webtypeGlob = webtype;
  //Start the web server or MQTT
  if (webtype == 1) {
    Serial.println(WiFi.softAPIP());
    server.on("/", webHandleConfig);
    server.on("/a", webHandleConfigSave);
    server.begin();
    Serial.println("HTTP server started");
  } else {
    //PubSubClient mqttClient((char*)mqttServer.c_str(), 1883, mqtt_arrived, wifiClient);
    //mqttClient.setBrokerDomain((char*)mqttServer.c_str());
     mqttClient.setServer((char*)mqttServer.c_str(), MQTT_PORT);
   // mqttClient.setPort(MQTT_PORT);
    mqttClient.setCallback(mqtt_arrived);
    mqttClient.setClient(wifiClient);
    connectMQTT();
  }
}

void webHandleConfig() {
  IPAddress ip = WiFi.softAPIP();
  String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
  String s;

  uint8_t mac[6];
  WiFi.macAddress(mac);
  String ssidClintName = ssid + "-" + macToStr(mac);

  s = "Configuration of " + ssidClintName + " at ";
  s += ipStr;
  s += "<p>";
  s += st;
  s += "<form method='get' action='a'>";
  s += "<label>SSID: </label><input name='ssid' length=32><label>Password: </label><input name='pass' type='password' length=64></br>";
  s += "MQTT parameters:</br>";
  s += "<label>Broker IP/DNS: </label><input name='host' length=32></br>";
  s += "<label>Broker user name: </label><input name='mqttuser' length=32></br>";
  s += "<label>Broker password: </label><input name='mqttpass' type='password' length=64></br>";
  s += "<label>Subscribe topic: </label><input name='subtop' length=64></br>";
  s += "<input type='submit'></form></p>";
  s += "\r\n\r\n";
  Serial.println("Sending 200");
  server.send(200, "text/html", s);
}

void webHandleConfigSave() {
  // /a?ssid=blahhhh&pass=poooo
  String s;
  s = "<p>Settings saved to eeprom and reset to boot into new settings</p>\r\n\r\n";
  server.send(200, "text/html", s);

  Serial.println("clearing EEPROM.");
  clearEEPROM();

  String qsid = server.arg("ssid");
  qsid = replaceSpecialChars(qsid);
  Serial.println(qsid);
  Serial.println("");

  String qpass = server.arg("pass");
  qpass = replaceSpecialChars(qpass);
  Serial.println(qpass);
  Serial.println("");

  String qsubTop = server.arg("subtop");
  qsubTop = replaceSpecialChars(qsubTop);
  Serial.println(qsubTop);
  Serial.println("");

  String qmqttip = server.arg("host");
  qmqttip = replaceSpecialChars(qmqttip);
  Serial.println(qmqttip);
  Serial.println("");

  String qmqttuser = server.arg("mqttuser");
  qmqttuser = replaceSpecialChars(qmqttuser);
  Serial.println(qmqttuser);
  Serial.println("");

  String qmqttpass = server.arg("mqttpass");
  qmqttpass = replaceSpecialChars(qmqttpass);
  Serial.println(qmqttpass);
  Serial.println("");

  EEPROM.begin(512);
  delay(10);

  Serial.println("writing eeprom ssid.");
  writeEeprom(0, qsid);
  Serial.println("");

  Serial.println("writing eeprom pass.");
  writeEeprom(32, qpass);
  Serial.println("");

  Serial.println("writing eeprom MQTT IP.");
  writeEeprom(96, qmqttip);
  Serial.println("");

  Serial.println("writing eeprom MQTT user.");
  writeEeprom(128, qmqttuser);
  Serial.println("");

  Serial.println("writing eeprom MQTT password.");
  writeEeprom(160, qmqttpass);
  Serial.println("");

  Serial.println("writing eeprom subTop.");
  writeEeprom(224, qsubTop);
  Serial.println("");

  EEPROM.commit();
  delay(1000);
  EEPROM.end();
  Serial.println("Settings written, restarting!");
  system_restart();
}

void setupAP(void) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n == 0) {
    Serial.println("no networks found");
    st = "<b>No networks found:</b>";
  } else {
    Serial.print(n);
    Serial.println(" Networks found");
    st = "<ul>";
    for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " (OPEN)" : "*");

      // Print to web SSID and RSSI for each network found
      st += "<li>";
      st += i + 1;
      st += ": ";
      st += WiFi.SSID(i);
      st += " (";
      st += WiFi.RSSI(i);
      st += ")";
      st += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " (OPEN)" : "*";
      st += "</li>";
      delay(10);
    }
    st += "</ul>";
  }
  Serial.println("");
  WiFi.disconnect();
  delay(100);
  WiFi.mode(WIFI_AP);
  //Build SSID
  //uint8_t mac[6];
  //WiFi.macAddress(mac);
  //ssid += "-";
  //ssid += macToStr(mac);
  uint8_t mac[6];
  WiFi.macAddress(mac);
  String ssidClintName = ssid + "-" + macToStr(mac);

  WiFi.softAP((char*) ssidClintName.c_str());
  WiFi.begin((char*) ssidClintName.c_str()); // not sure if need but works
  Serial.print("Access point started with name ");
  Serial.println(ssidClintName);
  launchWeb(1);
}

void led_handle() {
  digitalWrite(wifiLed, !digitalRead(wifiLed));
}

void btn_handle() {
  if (!digitalRead(inPin)) {
    ++count; // one count is 50ms
  } else {
    if (count > 1 && count < humanpressDelay / 5) { //push between 50 ms and 1 sec
      Serial.print("button pressed ");
      Serial.print(count * 0.05);
      Serial.println(" Sec.");
      changeRelayState();
    } else if (count > (restartDelay / 0.05) && count <= (resetDelay / 0.05)) { //pressed 3 secs (60*0.05s)
      Serial.print("button pressed ");
      Serial.print(count * 0.05);
      Serial.println(" Sec. Restarting!");
      restartFlag = 1;
    } else if (count > (resetDelay / 0.05)) { //pressed 5 secs
      Serial.print("button pressed ");
      Serial.print(count * 0.05);
      Serial.println(" Sec.");
      Serial.println(" Clearing EEPROM and resetting!");
      eepromToClear = 1;
    }

    count = 0; //reset since we are at high
  }
}

//-------------------------------- Help functions ---------------------------
String readEeprom(int startIndex, int length) {
  String output = "";
  String temp = "";
  for (int i = startIndex; i < startIndex + length; ++i)
  {
    temp = EEPROM.read(i);
    if (temp != "0") output += char(EEPROM.read(i)); // Ignore spaces
  }
  return output;
}

void writeEeprom(int offset, String value) {
  for (int i = 0; i < value.length(); ++i)
  {
    EEPROM.write(offset + i, value[i]);
    Serial.print(value[i]);
  }
}

void clearEEPROM() {
  EEPROM.begin(512);
  // write a 0 to all 512 bytes of the EEPROM
  for (int i = 0; i < 512; i++) {
    EEPROM.write(i, 0);
  }
  delay(200);
  EEPROM.commit();
  EEPROM.end();
}

String macToStr(const uint8_t* mac) {
  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
    if (i < 5)
      result += ':';
  }
  return result;
}

String replaceSpecialChars(String str) {
  str.replace("%40", "@");
  str.replace("%20", " ");
  str.replace("%2F", "/");
  return str;
}

//-------------------------------- MQTT functions ---------------------------
boolean connectMQTT() {
  if (mqttClient.connected()) {
    return true;
  }

  uint8_t mac[6];
  WiFi.macAddress(mac);
  String mqttClintName = ssid + "-" + macToStr(mac);

  Serial.print("Connecting to MQTT server ");
  Serial.print(mqttServer);
  Serial.print(" as ");
  Serial.println(mqttClintName);

  if (mqttClient.connect((char*) mqttClintName.c_str(), (char*) mqttServerUserName.c_str(), (char*) mqttServerPassword.c_str())) {
    Serial.println("Connected to MQTT broker");
    if (mqttClient.subscribe((char*)subTopic.c_str())) {
      Serial.println("Subsribed to topic.");
    } else {
      Serial.println("NOT subsribed to topic!");
    }
    led_timer.detach();
    digitalWrite(wifiLed, HIGH);
    return true;
  } else {
    Serial.println("MQTT connect failed! ");
    led_timer.attach(0.5, led_handle);
    return false;
  }
}

void disconnectMQTT() {
  mqttClient.disconnect();
}

void mqtt_arrived(char* subTopic, byte* payload, unsigned int length) { // handle messages arrived
  int i = 0;
  Serial.print("MQTT message arrived:  topic: " + String(subTopic));
  // create character buffer with ending null terminator (string) and flash the led to indicate incoming message
  for (i = 0; i < length; i++) {
    buf[i] = payload[i];
  }

  buf[i] = '\0';
  String msgString = String(buf);
  Serial.println(" message: " + msgString);
  
  if (msgString == "1" || msgString == "on" || msgString == "true")
  {
    digitalWrite(outPin, HIGH);
  } else if (msgString == "0" || msgString == "off" || msgString == "false") {
    digitalWrite(outPin, LOW);
  }

	// blink the led few times so we know data arrived
  for (i = 0; i < 10; i++) {
    digitalWrite(wifiLed, !digitalRead(wifiLed));
    delay(100);
  }
  digitalWrite(wifiLed, HIGH);
} 

void changeRelayState() {
    Serial.println("changing relay state");
    digitalWrite(outPin, HIGH);
	delay(250);
    digitalWrite(outPin, LOW);
}

//-------------------------------- Main loop ---------------------------
void loop() {
  if (eepromToClear == 1) {
    clearEEPROM();
    delay(1000);
    system_restart();
  }
  if (restartFlag == 1) {
    system_restart();
  }
  if (webtypeGlob == 1) {
    server.handleClient();
    //mdns.update(); we get problems with this.
    delay(10);
  } else if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected())
      connectMQTT();
    mqttClient.loop();
  } else {
    delay(1000);
    initWiFi(); //Try to connect again
  }
}
