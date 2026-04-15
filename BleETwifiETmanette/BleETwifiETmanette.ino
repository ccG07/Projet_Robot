#include <WiFi.h>
#include <AsyncUDP.h>
#include <ArduinoJson.h>

// --- Pins Moteurs ---
#define M_GAUCHE_1 4
#define M_GAUCHE_2 5
#define M_DROIT_1 6
#define M_DROIT_2 7

// --- Paramètres Réseau ---
const char* ssid = "ROBOT_ADAM_PEAK";
const char* password = "pass1234";
const int portUDP = 1234;

AsyncUDP udp;
unsigned long lastPacketTime = 0; // <<< DÉPLACÉ ICI (Global)

void stopperRobot() {
  digitalWrite(M_GAUCHE_1, LOW); digitalWrite(M_GAUCHE_2, LOW);
  digitalWrite(M_DROIT_1, LOW); digitalWrite(M_DROIT_2, LOW);
}

void setup() {
  Serial.begin(115200);
  pinMode(M_GAUCHE_1, OUTPUT); pinMode(M_GAUCHE_2, OUTPUT);
  pinMode(M_DROIT_1, OUTPUT); pinMode(M_DROIT_2, OUTPUT);
  stopperRobot();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password, 1, 0, 4);
  
  Serial.println(">>>> Wi-Fi PEAK ACTIF");

  if(udp.listen(portUDP)) {
    udp.onPacket([](AsyncUDPPacket packet) {
      lastPacketTime = millis(); // <<< ON MET À JOUR LE TEMPS ICI
      
      StaticJsonDocument<128> doc;
      deserializeJson(doc, packet.data(), packet.length());
      
      const char* cmd = doc["cmd"];
      int v = doc["v"] ? doc["v"] : 255; 

      if      (strcmp(cmd, "AVANCE") == 0)  { analogWrite(M_GAUCHE_1, v); digitalWrite(M_GAUCHE_2, LOW); analogWrite(M_DROIT_1, v); digitalWrite(M_DROIT_2, LOW); }
      else if (strcmp(cmd, "RECULER") == 0) { digitalWrite(M_GAUCHE_1, LOW); analogWrite(M_GAUCHE_2, v); digitalWrite(M_DROIT_1, LOW); analogWrite(M_DROIT_2, v); }
      else if (strcmp(cmd, "GAUCHE") == 0)  { digitalWrite(M_GAUCHE_1, LOW); digitalWrite(M_GAUCHE_2, LOW); analogWrite(M_DROIT_1, v); digitalWrite(M_DROIT_2, LOW); }
      else if (strcmp(cmd, "DROITE") == 0)  { analogWrite(M_GAUCHE_1, v); digitalWrite(M_GAUCHE_2, LOW); digitalWrite(M_DROIT_1, LOW); digitalWrite(M_DROIT_2, LOW); }
      else if (strcmp(cmd, "STOP") == 0)    { stopperRobot(); }
    });
  }

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info){
    if(event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
       Serial.println(">>> WIFI: PI CONNECTÉ AU RÉSEAU !");
    }
  });
}

void loop() {
  // --- 1. RÉPONSE AU PI (SERIAL HANDSHAKE) ---
  if (Serial.available() > 0) {
    String msg = Serial.readStringUntil('\n');
    if (msg.indexOf("CONNECT") >= 0) {
      Serial.println("ACK_PI"); // C'est ça que ton Pi attend !
    }
  }

  // --- 2. SÉCURITÉ (SI PERTE DE SIGNAL UDP) ---
  if (millis() - lastPacketTime > 500 && lastPacketTime > 0) { 
    stopperRobot(); 
  }
}
