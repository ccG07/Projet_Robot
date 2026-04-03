#include <WiFi.h>
#include <AsyncUDP.h>
#include <ArduinoJson.h>


// --- Configuration des Pins Moteurs (Session 6) ---
#define M_GAUCHE_1 4
#define M_GAUCHE_2 5
#define M_DROIT_1 6
#define M_DROIT_2 7

// --- Paramètres Réseau ---
const char* ssid = "ROBOT_ADAM_PEAK";
const char* password = "pass1234";
const int portUDP = 1234;

AsyncUDP udp;

void stopperRobot() {
  digitalWrite(M_GAUCHE_1, LOW); digitalWrite(M_GAUCHE_2, LOW);
  digitalWrite(M_DROIT_1, LOW); digitalWrite(M_DROIT_2, LOW);
}

void setup() {
  Serial.begin(115200);
  
  // Setup des pins moteurs
  pinMode(M_GAUCHE_1, OUTPUT); pinMode(M_GAUCHE_2, OUTPUT);
  pinMode(M_DROIT_1, OUTPUT); pinMode(M_DROIT_2, OUTPUT);
  stopperRobot();

  // On enlève TOUTE la config BLE ici.
  // On se concentre uniquement sur le Wi-Fi AP.
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password, 1, 0, 4);
  
  Serial.println(">>>> Wi-Fi PEAK ACTIF : " + String(ssid));
  Serial.print("IP : "); Serial.println(WiFi.softAPIP());

  // Écoute UDP
  if(udp.listen(portUDP)) {
    Serial.println(">>> Serveur UDP à l'écoute sur le port 1234");
    udp.onPacket([](AsyncUDPPacket packet) {
      Serial.print("Paquet reçu de: "); Serial.println(packet.remoteIP());
      Serial.print("Data: "); Serial.write(packet.data(), packet.length()); Serial.println();
      StaticJsonDocument<128> doc;
      DeserializationError error = deserializeJson(doc, packet.data(), packet.length());
      
      if (!error) {
        const char* cmd = doc["cmd"];
        int v = doc["v"] ? doc["v"] : 255; 

        // Logique de pilotage simplifiée
        if      (strcmp(cmd, "AVANCE") == 0)  { analogWrite(M_GAUCHE_1, v); digitalWrite(M_GAUCHE_2, LOW); analogWrite(M_DROIT_1, v); digitalWrite(M_DROIT_2, LOW); }
        else if (strcmp(cmd, "RECULER") == 0) { digitalWrite(M_GAUCHE_1, LOW); analogWrite(M_GAUCHE_2, v); digitalWrite(M_DROIT_1, LOW); analogWrite(M_DROIT_2, v); }
        else if (strcmp(cmd, "GAUCHE") == 0)  { digitalWrite(M_GAUCHE_1, LOW); digitalWrite(M_GAUCHE_2, LOW); analogWrite(M_DROIT_1, v); digitalWrite(M_DROIT_2, LOW); }
        else if (strcmp(cmd, "DROITE") == 0)  { analogWrite(M_GAUCHE_1, v); digitalWrite(M_GAUCHE_2, LOW); digitalWrite(M_DROIT_1, LOW); digitalWrite(M_DROIT_2, LOW); }
        else if (strcmp(cmd, "STOP") == 0)    { stopperRobot(); }
      }
    });
  }
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info){
    Serial.printf("Événement WiFi: %d\n", event);
    if(event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
       Serial.println(">>> PI CONNECTÉ !");
    }
});
}




// Dans ton loop() :
void loop() {
  unsigned long lastPacketTime = 0;
  // Dans ton udp.onPacket :
lastPacketTime = millis();

  if (millis() - lastPacketTime > 500) { // Si rien reçu depuis 0.5s
    stopperRobot(); // Sécurité : on arrête tout
  }
}