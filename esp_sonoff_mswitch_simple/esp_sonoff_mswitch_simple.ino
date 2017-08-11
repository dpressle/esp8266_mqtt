// derived from the Basic MQTT example https://github.com/knolleary/pubsubclient/blob/master/examples/mqtt_basic/mqtt_basic.ino

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

const int outPin = 12; //output pin
const int wifiLed = 13; //led light indicator pin for wifi connected status

// Update these with values suitable for your network.
String MQTT_SUBSCRIPTION = "sonoff/living/#";
String MQTT_PUBLISH = "sonoff/living";

const char* ssid = "HOME-2.4G";
const char* password = "1967197320002003";
const char* mqtt_server = "192.168.0.100";

WiFiClient espClient;
PubSubClient client(espClient);

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(wifiLed, HIGH);
    delay(500);
    digitalWrite(wifiLed, LOW);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  boolean bset = false;
  payload[length] = '\0';
  String sPayload = String((char *)payload);
  String sTopic = String(topic);
  if (sTopic == MQTT_PUBLISH + "/relay/set") {
    if (sPayload == "true") {
      digitalWrite(outPin, HIGH);
      delay(250);
      digitalWrite(outPin, LOW);
    } 
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "sonoff-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      // Once connected, publish an announcement...
      //client.publish(MQTT_PUBLISH.c_str() , "connected");
      // ... and resubscribe
      client.subscribe(MQTT_SUBSCRIPTION.c_str());
      digitalWrite(wifiLed, LOW);
    } else {
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(9600);
  digitalWrite(outPin, LOW);
  pinMode(outPin, OUTPUT);
  digitalWrite(wifiLed, HIGH);
  pinMode(wifiLed, OUTPUT);

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
}
