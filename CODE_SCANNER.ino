#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
 
BLEScan* pBLEScan;
 
// UUID recherché
BLEUUID targetUUID("12345678-1234-1234-1234-1234567890ab");
 
void setup() {
  Serial.begin(115200);
 
  BLEDevice::init("");              // Initialise le bluetooth de l'ESP32
  pBLEScan = BLEDevice::getScan();  // Crée le scanner BLE
 
  pBLEScan->setActiveScan(true);    //Scan actif → permet de récupérer plus d’infos (nom, etc.)
}
 
void loop() {
  // lance un scan BLE pendant 3 secondes
  BLEScanResults* results = pBLEScan->start(3); 
 
  // Boucle sur tous les appareils détectés
  for (int i = 0; i < results->getCount(); i++) {

     // Récupère un appareil détecté
    BLEAdvertisedDevice device = results->getDevice(i);
 
    // Vérifie si l'appareil a le bon UUID
    if (device.haveServiceUUID() && device.getServiceUUID().equals(targetUUID)) {

      // Récupère la puissance du signal (RSSI)
      int rssi = device.getRSSI();
 
      // Vérifie si l’appareil a un nom
      if (device.haveName()) {
        Serial.print("Nom: ");
        Serial.println(device.getName().c_str()); // Affiche le nom
      } else {
        Serial.println("Nom: inconnu"); // Sinon nom inconnu
      }
 
      Serial.print("RSSI: ");
      Serial.println(rssi); // Affiche la force du signal
 
      Serial.print("MAC: ");
      Serial.println(device.getAddress().toString().c_str()); // Affiche l’adresse MAC
      Serial.println("-------------------"); // Séparation visuelle
    }
  }
 
  pBLEScan->clearResults(); // Libère la mémoire
  delay(1000); // Pause de 1 ms avant de recommencer
}