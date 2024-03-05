/*
 * VIN - 5V
 * GND - GND
 * SDA - G21
 * SCL - G22
 */

/*
 * Verwendete Quellen:
 * - Example "NeoTrellis/Basic" der Adafruit seesaw Library
 * - Example "mqtt_basic" der PubSubClient Library
 */

#include "Adafruit_NeoTrellis.h"
#include <WiFi.h>
#include <PubSubClient.h>

// Farbdefinitionen fuer Rot und Gruen
#define RED 0xFF00000
#define GREEN 0x00FF000

// Definiert die Anzahl an maximaler Runden
#define MAX_DIFFICULTY 5

const String SSID = "gameartifacts";
const String PASSWORD = "blablabla";

// Wird gesetzt, falls Verbindung mit WLAN fehlschlaegt 
bool offlineMode = false;
// Wurde RFID geloest und dieses Raetsel dadurch freigeschaltet
bool rfidSolved = false;
// Wurde dieses Raetsel geloest
bool solved = false;

WiFiClient espClient;
PubSubClient mqttClient(espClient);
const char* MQTT_BROKER = "192.168.22.205";
const char* TOPIC_SOLVED = "/artifakt/matrix/solved";
const char* TOPIC_RESET = "/artifakt/matrix/reset";
const char* TOPIC_RFID_SOLVED = "/artifakt/rfid/solved";

// Speichert die korrekte Reihenfolge
int target_sequence[MAX_DIFFICULTY] = {};
// Speichert die eingegebene Reihenfolge
int input_sequence[MAX_DIFFICULTY] = {};
// Speichert das aktuelle Level und definiert dadurch die Anzahl der Buttonpresses fuer diese Stufe
int currentLevel = 0;
// Countervariable fuer Eingabe
int currentInput = 0;
Adafruit_NeoTrellis trellis;

// Behandelt MQTT Input
void handleMqtt(char* topic, byte* payload, unsigned int length) {
  Serial.print("Neue Nachricht empfangen auf Thema: ");
  Serial.println(topic);
  Serial.print("Nachricht:");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if(strcmp(topic, TOPIC_RESET) == 0) {
    Serial.println("Received RESET message!");
    reset();
  }
  else if(strcmp(topic, TOPIC_SOLVED) == 0)
  {
    Serial.println("Received SOLVED message!");
    success();
  }
  else if(strcmp(topic, TOPIC_RFID_SOLVED) == 0)
  {
    rfidSolved = true;
    doAnimation();
    delay(1000);
    showTargetSequence();
  }
  else
  {
    Serial.print("Received message on unknown topic: ");
    Serial.println(topic);
    Serial.print("Message:");
    for (int i = 0; i < length; i++) {
      Serial.print((char)payload[i]);
    }
    Serial.println();
  }
}

// Zuruecksetzen des Raetsels
void reset ()
{
  rfidSolved = false;
  solved = false;
  resetInput();
  currentInput = 0;
  currentLevel = 0;
}

// Raetsel wurde geloest
void success ()
{
  solved = true;
}

// Mit WLAN verbinden
void setupWifi ()
{
  Serial.print("Connecting to ");
  Serial.println(SSID);
  WiFi.begin(SSID, PASSWORD);
  int i = 0;
  while(WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    i++;
    if (i >= 20)
    {
      WiFi.disconnect(true);
      offlineMode = true;
      Serial.println("\nConnection failed, activated offlineMode.");
      return;
    }
  }

  Serial.println("\nWiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

// MQTT konfigurieren
void setupMqtt()
{
  mqttClient.setServer(MQTT_BROKER, 1883);
  mqttClient.setCallback(handleMqtt);
}

// Eingabearray zuruecksetzen
void resetInput ()
{
  for(int i = 0; i < MAX_DIFFICULTY; i++)
  {
    input_sequence[i] = -1;
  }
}

// Pruefen, ob Raetsel geloest wurde
void checkSolved ()
{
  Serial.println("Checking...");

  // Nur bis zum aktuellen Level pruefen
  for(int i = 0; i <= currentLevel; i++)
  {
    if(input_sequence[i] != target_sequence[i])
    {
      currentLevel = 0;
	  // Muss -1 sein, da Variable gleich wieder inkrementiert wird
      currentInput = -1;
      resetInput();
      flash(RED, 500);
      generateTargetSequence();
      showTargetSequence();
      Serial.println("Not solved!");
      return;
    }
  }

  Serial.println("Current Level solved");

  // Pruefen, ob Ende des Raetsels erreicht wurde
  if(currentLevel+1 == MAX_DIFFICULTY)
  {
    flash(GREEN, 2500);
    Serial.println("Solved!");
    solved = true;
    mqttClient.publish(TOPIC_SOLVED, "Matrix solved!");
    currentLevel = 0;
    currentInput = 0;
  }
  else
  {
    flash(GREEN);
    currentLevel++;
    currentInput = -1;
    resetInput();
    generateTargetSequence();
    delay(500);
    showTargetSequence();
  }
}

// Pruefen, ob genug Buttonpresses getaetigt wurden
void checkSequence ()
{
  if(currentInput >= currentLevel)
  {
    checkSolved();
  }
}

// Verarbeitet Buttonpresses
TrellisCallback handleKeypress(keyEvent evt){
  // Pruefen, ob Raetsel aktiv ist
  if(!(!solved && (rfidSolved || offlineMode)))
    return 0;

  if (evt.bit.EDGE == SEESAW_KEYPAD_EDGE_RISING) {
	// Button wurde gedrueckt
    trellis.pixels.setPixelColor(evt.bit.NUM, Wheel(map(evt.bit.NUM, 0, trellis.pixels.numPixels(), 0, 255)));
    input_sequence[currentInput] = evt.bit.NUM;
    checkSequence();
    currentInput++;
  } else if (evt.bit.EDGE == SEESAW_KEYPAD_EDGE_FALLING) {
	// Button wurde losgelassen
    trellis.pixels.setPixelColor(evt.bit.NUM, 0);
  }
  
  trellis.pixels.show();

  return 0;
}

// Setzt die Farbe fuer einen Pixel
void setColorForOnePixel(uint16_t index, uint16_t color)
{
  trellis.pixels.setPixelColor(index, color);
  trellis.pixels.show();
}

// Setzt die Farbe fuer alle Pixel
void setColorForAllPixels (uint32_t color)
{
  for (uint16_t i=0; i<trellis.pixels.numPixels(); i++) 
  {
    trellis.pixels.setPixelColor(i, color);
  }
  trellis.pixels.show();
}

// Laesst alle Pixel aufblinken
void flash (uint32_t color, long delayTime)
{
  setColorForAllPixels(color);
  delay(delayTime);
  setColorForAllPixels(0x000000);
}

// Laesst alle Pixel fuer 200ms aufblinken
void flash (uint32_t color)
{
  flash(color, 200);
}

// Laesst einen Pixel aufblinken
void flashOnePixel (uint16_t index, uint32_t color, long delayTime)
{
  setColorForOnePixel(index, color);
  delay(delayTime);
  setColorForOnePixel(index, 0x000000);
}

// Laesst einen Pixel fuer 200ms aufblinken
void flashOnePixel (uint16_t index, uint32_t color)
{
  flashOnePixel(index, color, 200);
}

// Generiert eine neue Zielsequenz
void generateTargetSequence()
{
  Serial.print("Target Sequence: ");
  for(int i = 0; i < MAX_DIFFICULTY; i++)
  {
    target_sequence[i] = random(0, 16);
    Serial.print(target_sequence[i]);
    Serial.print(" ");
  }
  Serial.println();
}

// Zeigt die Zielsequenz auf der Matrix an
void showTargetSequence ()
{
  for(int i = 0; i <= currentLevel; i++)
  {
    flashOnePixel(target_sequence[i], Wheel(i), 500);
    delay(200);
  }
}

// Einrichten des Raetsels
void setup() {
  Serial.begin(9600);
  
  if (!trellis.begin()) {
    Serial.println("Could not start trellis, check wiring?");
    while(1) delay(1);
  } else {
    Serial.println("NeoPixel Trellis started");
  }

  setupWifi();

  if(!offlineMode)
    setupMqtt();

  for(int i=0; i<NEO_TRELLIS_NUM_KEYS; i++){
    trellis.activateKey(i, SEESAW_KEYPAD_EDGE_RISING);
    trellis.activateKey(i, SEESAW_KEYPAD_EDGE_FALLING);
    trellis.registerCallback(i, handleKeypress);
  }

  doAnimation();
}

// Animation auf der Matrix abspielen
void doAnimation ()
{
  for (uint16_t i=0; i<trellis.pixels.numPixels(); i++) {
    trellis.pixels.setPixelColor(i, Wheel(map(i, 0, trellis.pixels.numPixels(), 0, 255)));
    trellis.pixels.show();
    delay(50);
  }

  for (uint16_t i=0; i<trellis.pixels.numPixels(); i++) {
    trellis.pixels.setPixelColor(i, 0x000000);
    trellis.pixels.show();
    delay(50);
  }
}

// Erneut mit MQTT verbinden, falls Verbindung verloren wurde
void reconnect() {
	while (!mqttClient.connected()) {
		Serial.print("Reconnecting...");
		if (!mqttClient.connect("Matrix_Artifact1")) {
			Serial.print("failed, rc=");
			Serial.print(mqttClient.state());
			Serial.println(" retrying in 5 seconds");
			delay(5000);
		}
	}

  Serial.println("\nConnected!");

  mqttClient.subscribe(TOPIC_SOLVED);
  mqttClient.subscribe(TOPIC_RFID_SOLVED);
  mqttClient.subscribe(TOPIC_RESET);
}

void loop() {
  // MQTT ausfuehren, falls Raetsel mit WLAN verbunden ist
  if(!offlineMode)
  {
    if (!mqttClient.connected()) {
      reconnect();
    }

    mqttClient.loop();
  }

  // Keypresses von Matrix lesen
  trellis.read();
  
  // Frequenz der Matrix entsprechen
  delay(20);
}

// Wird zum generieren von Farben verwendet
uint32_t Wheel(byte WheelPos) {
  if(WheelPos < 85) {
   return trellis.pixels.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  } else if(WheelPos < 170) {
   WheelPos -= 85;
   return trellis.pixels.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } else {
   WheelPos -= 170;
   return trellis.pixels.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  return 0;
}
