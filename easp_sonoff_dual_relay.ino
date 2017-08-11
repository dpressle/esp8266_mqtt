// derived from the Basic MQTT example https://github.com/knolleary/pubsubclient/blob/master/examples/mqtt_basic/mqtt_basic.ino

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Update these with values suitable for your network.
String MQTT_SUBSCRIPTION = "sonoff/2/#";
String MQTT_PUBLISH = "sonoff/2";


const char* ssid = "pressler";
const char* password = "0524286529";
const char* mqtt_server = "10.0.0.3";


boolean relay1 = false;
boolean relay2 = false;
int incomingByte = 0;
int iStep = 0;
  int iNewState = 0;

WiFiClient espClient;
PubSubClient client(espClient);

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  //Serial.println();
  //Serial.print("Connecting to ");
  //Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    //Serial.print(".");
  }

  randomSeed(micros());

  //Serial.println("");
  //Serial.println("WiFi connected");
  //Serial.println("IP address: ");
  //Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  boolean bset = false;
  payload[length] = '\0';
  String sPayload = String((char *)payload);
  String sTopic = String(topic);
  if (sTopic == MQTT_PUBLISH + "/relay1/set") {
    if (sPayload == "1") {
      if (relay1 == false) bset = true;
      relay1 = true;
    } else {
      if (relay1) bset = true;
      relay1 = false;
    }
  }
  if (sTopic == MQTT_PUBLISH + "/relay2/set") {
    if (sPayload == "1") {
      if (relay2 == false) bset = true;
      relay2 = true;
    } else {
      if (relay2) bset = true;
      relay2 = false;
    }
  }
  if (bset) setrelays();
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    //Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "sonoff-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      // Once connected, publish an announcement...
      client.publish(MQTT_PUBLISH.c_str() , "connected");
      setrelays();
      // ... and resubscribe
      client.subscribe(MQTT_SUBSCRIPTION.c_str());
    } else {
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(19200);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  client.loop();
}

void loop() {

  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  if (Serial.available() > 0) {
    // read the incoming byte:
    incomingByte = Serial.read();

    if (incomingByte == 0xA0) {
      iStep = 1;
    }
    else if ((iStep == 1) && (incomingByte == 0x04)) {
      iStep = 2;
    }
    else if ((iStep == 2) && (incomingByte >= 0) && (incomingByte <= 3)) {
      iStep = 3;
      iNewState = incomingByte;
    } else if ((iStep == 3) && (incomingByte == 0xA1)) {
      iStep = 0;
      if (iNewState == 0) {
        relay1 = false;
        relay2 = false;
      }
      if (iNewState == 1) {
        relay1 = true;
        relay2 = false;
      }
      if (iNewState == 2) {
        relay1 = false;
        relay2 = true;
      }
      if (iNewState == 3) {
        relay1 = true;
        relay2 = true;
      }
      // client.publish(MQTT_PUBLISH.c_str(),String(iNewState).c_str());
      if (relay1) client.publish((MQTT_PUBLISH + "/relay1").c_str(), "1");
      else client.publish((MQTT_PUBLISH + "/relay1").c_str(), "0");

      if (relay2) client.publish((MQTT_PUBLISH + "/relay2").c_str(), "1");
      else client.publish((MQTT_PUBLISH + "/relay2").c_str(), "0");
      
    } else iStep = 0;
  }
}

void setrelays() {
  byte b = 0;
  if (relay1) b++;
  if (relay2) b += 2;
  Serial.write(0xA0);
  Serial.write(0x04);
  Serial.write(b);
  Serial.write(0xA1);
  Serial.flush();
  if (relay1) client.publish((MQTT_PUBLISH + "/relay1").c_str(), "1");
  else client.publish((MQTT_PUBLISH + "/relay1").c_str(), "0");

  if (relay2) client.publish((MQTT_PUBLISH + "/relay2").c_str(), "1");
  else client.publish((MQTT_PUBLISH + "/relay2").c_str(), "0");
}