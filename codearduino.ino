//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Bibliothèques
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

//Broches
#define SS_PIN 10
#define RST_PIN 9
#define relais_pompe A0

//Configurations
LiquidCrystal_I2C lcd(0x27, 16, 2);

const byte ROWS = 4;
const byte COLS = 3;
char keys[ROWS][COLS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};
byte rowPins[ROWS] = {2, 3, 4, 5};
byte colPins[COLS] = {6, 7, 8};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

MFRC522 mfrc522(SS_PIN, RST_PIN);

//Variables globales
String input = "", uid = "", carte = "", card = "", toucheCombo = "";
int nombreCartes;
bool adminMode = false, waitingPass = false;
int currentIndex = -1;
String enteredPass = "", adminPass = "";
#define MAX_CARTES 10
#define UID_SIZE 4
#define PASS_ADDR 100

enum AdminState { NONE, ADD_CARD, DELETE_CARD, CHANGE_PASS, CHANGE_QTE };
AdminState adminState = NONE;

//Fonction de recharge du mot de passe admin
void loadAdminPass() {
  adminPass = "";
  for (int i = 0; i < 4; i++) {
    char c = EEPROM.read(PASS_ADDR + i);
    if (c >= 32 && c <= 126) adminPass += c;
  }
  if (adminPass == "") adminPass = "1234"; // par défaut
}

//Fonction d'enregistrement du mot de passe admin
void saveAdminPass(String newPass) {
  for (int i = 0; i < 4; i++) {
    EEPROM.write(PASS_ADDR + i, (i < newPass.length()) ? newPass[i] : 0);
  }
  adminPass = newPass;
}

//Fonction de lecture de la carte RFID
String lireRfid() {
  if (!mfrc522.PICC_IsNewCardPresent()) return "";
  if (!mfrc522.PICC_ReadCardSerial()) return "";
  String content = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    content += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
    content += String(mfrc522.uid.uidByte[i], HEX);
  }
  content.toUpperCase();
  mfrc522.PICC_HaltA();
  return content;
}

//Fonction d'enregistrement d'une carte
void enregistrerCarte(int index, String uid, int quantite) {
  for (int i = 0; i < UID_SIZE; i++) {
    EEPROM.write(1 + index * 6 + i, strtol(uid.substring(i * 2, i * 2 + 2).c_str(), NULL, 16));
  }
  EEPROM.write(1 + index * 6 + 4, highByte(quantite));
  EEPROM.write(1 + index * 6 + 5, lowByte(quantite));
  nombreCartes++;
}

//Fonction de suppression d'une carte
void supprimerCarte(int index) {
  for (int i = index; i < nombreCartes - 1; i++) {
    String uid; int qte;
    lireCarteEEPROM(i + 1, uid, qte);
    enregistrerCarte(i, uid, qte);
  }
  nombreCartes--;
  EEPROM.write(0, nombreCartes);
}

//Fonction de verification d'une carte dans la mémoire
bool lireCarteEEPROM(int index, String &uid, int &quantite) {
  uid = "";
  for (int i = 0; i < UID_SIZE; i++) {
    byte val = EEPROM.read(1 + index * 6 + i);
    if (val < 16) uid += "0";
    uid += String(val, HEX);
  }
  uid.toUpperCase();
  byte h = EEPROM.read(1 + index * 6 + 4);
  byte l = EEPROM.read(1 + index * 6 + 5);
  quantite = word(h, l);
  return true;
}

int trouverCarte(String uid) {
  for (int i = 0; i < nombreCartes; i++) {
    String savedUID; int qte;
    lireCarteEEPROM(i, savedUID, qte);
    if (uid == savedUID) return i;
  }
  return -1;
}

//Fonction d'activation de la pompe
void activerPompe(int duree) {
  digitalWrite(relais_pompe, HIGH);
  delay(duree);
  digitalWrite(relais_pompe, LOW);
}

//Fonction d'affichage sur l'écran
void afficherLCD(String l1, String l2 = "") {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(l1);
  lcd.setCursor(0, 1); lcd.print(l2);
}

//Fonction de formatage de la mémoire
void resetEEPROM() {
  for (int i = 0; i < EEPROM.length(); i++) {
    EEPROM.write(i, 0);
  }
}

//Fonction setup
void setup() {
  Serial.begin(9600);
  //resetEEPROM();
  lcd.init(); lcd.backlight();

  SPI.begin();
  mfrc522.PCD_Init();

  pinMode(relais_pompe, OUTPUT);
  digitalWrite(relais_pompe, LOW);

  nombreCartes = EEPROM.read(0);
  if (nombreCartes == 0) {
    enregistrerCarte(0, "33612CDA", 200);
    EEPROM.write(0, 1); // mets à jour le compteur de cartes
    nombreCartes = 1;
  }

  loadAdminPass();
  afficherLCD("Scannez RFID");
}

//Fonction loop
void loop() {
  uid = lireRfid();
  char key = keypad.getKey();
  if (key && key == '#'){
    if (toucheCombo.length() < 2){
      toucheCombo += key; 
    }else if(toucheCombo.length() == 2){
      toucheCombo = toucheCombo;
    }
  }

  if (uid!=""){
    card = uid;
  }

  // Authentification admin
  if (waitingPass && key) {
    if (key == '#') {
      if (enteredPass == adminPass) {
        adminMode = true;
        waitingPass = false;
        afficherLCD("Admin mode", "1:Ajout 2:Sup");
        adminState = NONE;
      } else {
        afficherLCD("Mot de passe", "incorrect!");
        delay(2000);
        afficherLCD("Scannez RFID");
        waitingPass = false;
        enteredPass = "";
      }
    } else if (key == '*') {
      if (enteredPass.length() > 0) enteredPass.remove(enteredPass.length() - 1);
      afficherLCD("Code: " + enteredPass);
    } else {
      enteredPass += key;
      afficherLCD("Code: " + enteredPass);
    }
    return;
  }

  // Activation admin mode
  if (key == '#' && !adminMode && currentIndex == -1) {
    waitingPass = true;
    enteredPass = "";
    afficherLCD("Entrez MDP admin");
    return;
  }

  // Sélection admin
  if (adminMode && adminState == NONE && key) {
    if (key == '1') {
      adminState = ADD_CARD;
      afficherLCD("Scan nouvelle", "carte RFID");
    } else if (key == '2') {
      adminState = DELETE_CARD;
      afficherLCD("Scan carte", "a supprimer");
    } else if (key == '3') {
      adminState = CHANGE_PASS;
      input = "";
      afficherLCD("Nouv MDP (4)", "");
    } else if (key == '4') {
      adminState = CHANGE_QTE;
      input = "";
      afficherLCD("Scan carte", "a gerer");
    }
    return;
  }

  // Changement mot de passe
  if (adminState == CHANGE_PASS && key) {
    if (key == '#') {
      if (input.length() == 4) {
        saveAdminPass(input);
        afficherLCD("MDP change!", "");
      } else {
        afficherLCD("Erreur MDP (4)", "");
      }
      delay(2000);
      adminMode = false;
      adminState = NONE;
      afficherLCD("Scannez RFID");
    } else if (key == '*') {
      if (input.length() > 0) input.remove(input.length() - 1);
    } else {
      input += key;
    }
    afficherLCD("MDP: " + input);
    return;
  }

  // Modification quantité d'une carte existante
  if (adminMode && adminState == CHANGE_QTE && uid != "") {
    int index = trouverCarte(uid);
    if (index != -1) {
      input = "";
      afficherLCD("Quantite actuelle", "Modifiez svp:");
      while (true) {
        char k = keypad.getKey();
        if (k == '#') {
          int newQte = input.toInt();
          String savedUID;
          int oldQte;
          lireCarteEEPROM(index, savedUID, oldQte);
          enregistrerCarte(index, savedUID, newQte); // Mise à jour de la quantité
          afficherLCD("Quantite modifiee", "Qte: " + String(newQte));
          delay(2000);
          break;
        } else if (k == '*') {
          if (input.length() > 0) input.remove(input.length() - 1);
        } else if (k) {
          input += k;
        }
        afficherLCD("Qte: " + input);
      }
    } else {
      afficherLCD("Carte inconnue", "");
      delay(2000);
    }
    adminMode = false;
    adminState = NONE;
    afficherLCD("Scannez RFID");
    return;
  }

  // Ajout carte
  if (adminMode && adminState == ADD_CARD && uid != "") {
    if (nombreCartes < MAX_CARTES) {
      input = "";
      afficherLCD("Quantite pour", "nouvelle carte:");
      while (true) {
        char k = keypad.getKey();
        if (k == '#') {
          int q = input.toInt();
          enregistrerCarte(nombreCartes, uid, q);
          EEPROM.write(0, nombreCartes);
          afficherLCD("Carte ajoutee!", "Qte: " + String(q));
          delay(2000);
          break;
        } else if (k == '*') {
          if (input.length() > 0) input.remove(input.length() - 1);
        } else if (k) {
          input += k;
        }
        afficherLCD("Qte: " + input);
      }
    } else {
      afficherLCD("Memoire pleine", "");
      delay(2000);
    }
    adminMode = false;
    adminState = NONE;
    afficherLCD("Scannez RFID");
    return;
  }

  // Suppression carte
  if (adminMode && adminState == DELETE_CARD && uid != "") {
    int index = trouverCarte(uid);
    if (index != -1) {
      supprimerCarte(index);
      afficherLCD("Carte suppr.", "");
    } else {
      afficherLCD("Carte inconnue", "");
    }
    delay(2000);
    adminMode = false;
    adminState = NONE;
    afficherLCD("Scannez RFID");
    return;
  }

  // Utilisateur scan carte
  if (uid != "") {
    int index = trouverCarte(uid);
    if (index == -1) {
      afficherLCD("Carte inconnue", "");
      delay(2000);
    } else {
      currentIndex = index;
      input = "";
      afficherLCD("Quantite a", "distribuer?");
    }
    return;
  }

  // Saisie quantité utilisateur
  if (key && currentIndex != -1 ) {
    if (key == '*') input.remove(input.length() - 1);
    else if (key == '#') {
      int qte = input.toInt();
      String savedUID; int dispo;
      lireCarteEEPROM(currentIndex, savedUID, dispo);
      if (qte <= dispo) {
        afficherLCD("Distribution...", "");
        activerPompe(qte * 1000);
        dispo -= qte;
        enregistrerCarte(currentIndex, savedUID, dispo);
        afficherLCD("Servi!", "");
      } else {
        afficherLCD("Credit", "insuffisant");
      }
      delay(2000);
      currentIndex = -1;
      input = "";
      afficherLCD("Scannez RFID");
      return;
    } else input += key;
    afficherLCD("Qte: " + input);
  }
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
