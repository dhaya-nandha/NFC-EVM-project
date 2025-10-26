/*
 * EVM with MicroSD Card + Master Card System - RESET FEATURE ADDED
 * Updated LED pins: Green->D5, Red->D6, Yellow->D7
 * SD Card: D10(CS), D11(MOSI), D12(MISO), D13(SCK)
 * Master Menu: Start Voting, Show Results, Stop Voting, Reset Voting
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_PN532.h>
#include <SD.h>
#include <SPI.h>

// Pins
#define CLK 2
#define DT 3
#define SW 4
#define BUZZER 8
#define LED_GREEN 5
#define LED_RED 6
#define LED_YELLOW 7
#define SD_CS 10

// Components
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_PN532 nfc(-1, -1);
File votesFile;
File uidsFile;

// Variables
int voteCounts[3] = {0, 0, 0};
int selection = 0;
int lastStateCLK;
uint8_t scannedUID[7];
uint8_t scannedUIDLength = 0;
boolean votingActive = false;

// Master card UID - CHANGE THIS TO YOUR MASTER CARD UID
uint8_t masterUID[7] = {0xB6, 0xF8, 0xFD, 0x03, 0x00, 0x00, 0x00};
uint8_t masterUIDLen = 4;

enum State { IDLE, CARD_DETECTED, SELECTING, VOTE_CONFIRMED, MASTER_MENU };
State currentState = IDLE;

void setup() {
  lcd.init();
  lcd.backlight();
  lcd.print(F("EVM Boot..."));
  delay(1000);
  
  // Initialize pins
  pinMode(CLK, INPUT);
  pinMode(DT, INPUT);
  pinMode(SW, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  
  lastStateCLK = digitalRead(CLK);
  
  // Init NFC
  lcd.clear();
  lcd.print(F("Starting NFC..."));
  nfc.begin();
  
  uint32_t ver = nfc.getFirmwareVersion();
  if (!ver) {
    lcd.clear();
    lcd.print(F("NFC Error!"));
    digitalWrite(LED_RED, HIGH);
    while (1) {
      errorBeep();
      delay(2000);
    }
  }
  nfc.SAMConfig();
  
  // Init SD Card
  lcd.clear();
  lcd.print(F("Init SD Card..."));
  
  if (!SD.begin(SD_CS)) {
    lcd.clear();
    lcd.print(F("SD Init Failed!"));
    digitalWrite(LED_RED, HIGH);
    while (1) {
      errorBeep();
      delay(2000);
    }
  }
  
  // Load vote counts from SD
  loadVotesFromSD();
  
  votingActive = false;
  resetToIdle();
  beep(100);
  delay(200);
  beep(100);
}

void loop() {
  switch (currentState) {
    case IDLE: handleIdle(); break;
    case CARD_DETECTED: handleCardDetected(); break;
    case SELECTING: handleSelecting(); break;
    case VOTE_CONFIRMED: handleVoteConfirmed(); break;
    case MASTER_MENU: handleMasterMenu(); break;
  }
}

void handleIdle() {
  uint8_t uid[7];
  uint8_t uidLen;
  
  boolean success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 100);
  
  if (success) {
    memcpy(scannedUID, uid, uidLen);
    scannedUIDLength = uidLen;
    
    // Check if master card
    if (isMasterCard(uid, uidLen)) {
      currentState = MASTER_MENU;
      digitalWrite(LED_GREEN, LOW);
      digitalWrite(LED_YELLOW, HIGH);
      confirmBeep();
      return;
    }
    
    // Check if voting is active
    if (!votingActive) {
      lcd.clear();
      lcd.print(F("Voting Inactive"));
      lcd.setCursor(0, 1);
      lcd.print(F("Ask admin"));
      digitalWrite(LED_RED, HIGH);
      errorBeep();
      delay(2000);
      digitalWrite(LED_RED, LOW);
      resetToIdle();
      return;
    }
    
    // Check if already voted
    if (hasVoted(uid, uidLen)) {
      lcd.clear();
      lcd.print(F("Already Voted!"));
      lcd.setCursor(0, 1);
      lcd.print(F("Access Denied"));
      digitalWrite(LED_GREEN, LOW);
      digitalWrite(LED_RED, HIGH);
      errorBeep();
      delay(3000);
      resetToIdle();
    } else {
      currentState = CARD_DETECTED;
      digitalWrite(LED_GREEN, LOW);
      beep(200);
    }
  }
}

void handleCardDetected() {
  lcd.clear();
  lcd.print(F("Card Accepted!"));
  delay(1000);
  
  lcd.clear();
  lcd.print(F("Select & Press"));
  delay(1500);
  
  selection = 0;
  currentState = SELECTING;
  displayCandidate();
}

void handleSelecting() {
  int currentStateCLK = digitalRead(CLK);
  
  if (currentStateCLK != lastStateCLK && currentStateCLK == 1) {
    if (digitalRead(DT) != currentStateCLK) {
      selection++;
      if (selection > 2) selection = 0;
    } else {
      selection--;
      if (selection < 0) selection = 2;
    }
    displayCandidate();
    beep(50);
  }
  lastStateCLK = currentStateCLK;
  
  if (digitalRead(SW) == LOW) {
    delay(50);
    if (digitalRead(SW) == LOW) {
      recordVote();
      currentState = VOTE_CONFIRMED;
      while (digitalRead(SW) == LOW);
    }
  }
}

void handleVoteConfirmed() {
  lcd.clear();
  lcd.print(F("Vote Recorded!"));
  lcd.setCursor(0, 1);
  lcd.print(F("Thank You!"));
  
  digitalWrite(LED_YELLOW, HIGH);
  confirmBeep();
  delay(2500);
  digitalWrite(LED_YELLOW, LOW);
  
  // DON'T show results here anymore - only master can see
  resetToIdle();
}

void handleMasterMenu() {
  int menuSelection = 0;
  boolean inMenu = true;
  
  // Display initial menu
  lcd.clear();
  lcd.print(F("MASTER MENU"));
  lcd.setCursor(0, 1);
  lcd.print(F(">Start Voting"));
  
  delay(500); // Small delay to stabilize
  
  while (inMenu) {
    int currentStateCLK = digitalRead(CLK);
    
    // Handle encoder rotation
    if (currentStateCLK != lastStateCLK && currentStateCLK == 1) {
      if (digitalRead(DT) != currentStateCLK) {
        menuSelection++;
        if (menuSelection > 3) menuSelection = 0;  // Now 4 options (0-3)
      } else {
        menuSelection--;
        if (menuSelection < 0) menuSelection = 3;  // Wrap to option 3
      }
      
      // Update display based on selection
      lcd.clear();
      lcd.print(F("MASTER MENU"));
      lcd.setCursor(0, 1);
      
      if (menuSelection == 0) {
        lcd.print(F(">Start Voting"));
      } else if (menuSelection == 1) {
        lcd.print(F(">Show Results"));
      } else if (menuSelection == 2) {
        lcd.print(F(">Stop Voting"));
      } else if (menuSelection == 3) {
        lcd.print(F(">Reset Voting"));
      }
      
      beep(50);
    }
    lastStateCLK = currentStateCLK;
    
    // Handle button press
    if (digitalRead(SW) == LOW) {
      delay(50);
      if (digitalRead(SW) == LOW) {
        
        if (menuSelection == 0) {
          // Start Voting
          votingActive = true;
          lcd.clear();
          lcd.print(F("Voting Started!"));
          lcd.setCursor(0, 1);
          lcd.print(F("System Active"));
          digitalWrite(LED_GREEN, HIGH);
          digitalWrite(LED_YELLOW, LOW);
          confirmBeep();
          delay(2000);
          
        } else if (menuSelection == 1) {
          // Show Results
          digitalWrite(LED_YELLOW, LOW);
          showVoteCounts();
          delay(4000); // Show results for 4 seconds
          
        } else if (menuSelection == 2) {
          // Stop Voting
          votingActive = false;
          lcd.clear();
          lcd.print(F("Voting Stopped!"));
          lcd.setCursor(0, 1);
          lcd.print(F("System Locked"));
          digitalWrite(LED_GREEN, LOW);
          digitalWrite(LED_RED, HIGH);
          confirmBeep();
          delay(2000);
          digitalWrite(LED_RED, LOW);
          
        } else if (menuSelection == 3) {
          // Reset Voting - NEW FEATURE
          resetVotingData();
          lcd.clear();
          lcd.print(F("Voting Reset!"));
          lcd.setCursor(0, 1);
          lcd.print(F("All Data Cleared"));
          
          // Flash all LEDs to indicate reset
          digitalWrite(LED_GREEN, HIGH);
          digitalWrite(LED_RED, HIGH);
          digitalWrite(LED_YELLOW, HIGH);
          confirmBeep();
          delay(500);
          digitalWrite(LED_GREEN, LOW);
          digitalWrite(LED_RED, LOW);
          digitalWrite(LED_YELLOW, LOW);
          delay(500);
          digitalWrite(LED_GREEN, HIGH);
          digitalWrite(LED_RED, HIGH);
          digitalWrite(LED_YELLOW, HIGH);
          delay(500);
          digitalWrite(LED_GREEN, LOW);
          digitalWrite(LED_RED, LOW);
          digitalWrite(LED_YELLOW, LOW);
          
          delay(2000);
        }
        
        inMenu = false;
        while (digitalRead(SW) == LOW);
      }
    }
    
    delay(10); // Small delay to prevent bouncing
  }
  
  currentState = IDLE;
  resetToIdle();
}

void displayCandidate() {
  lcd.clear();
  lcd.print(F("Vote for:"));
  lcd.setCursor(0, 1);
  lcd.print(F("Candidate "));
  lcd.print((char)('A' + selection));
}

void showVoteCounts() {
  lcd.clear();
  lcd.print(F("A:"));
  lcd.print(voteCounts[0]);
  lcd.print(F(" B:"));
  lcd.print(voteCounts[1]);
  lcd.print(F(" C:"));
  lcd.print(voteCounts[2]);
  
  lcd.setCursor(0, 1);
  lcd.print(F("Total: "));
  lcd.print(voteCounts[0] + voteCounts[1] + voteCounts[2]);
}

void resetToIdle() {
  currentState = IDLE;
  digitalWrite(LED_YELLOW, LOW);
  
  if (votingActive) {
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_RED, LOW);
  } else {
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_RED, LOW);
  }
  
  lcd.clear();
  if (votingActive) {
    lcd.print(F("Voting Active"));
    lcd.setCursor(0, 1);
    lcd.print(F("Scan Card"));
  } else {
    lcd.print(F("Ready"));
    lcd.setCursor(0, 1);
    lcd.print(F("Scan Master"));
  }
}

void recordVote() {
  voteCounts[selection]++;
  saveVotesToSD();
  storeUIDtoSD(scannedUID, scannedUIDLength);
}

// ========== SD CARD FUNCTIONS ==========

void saveVotesToSD() {
  SD.remove("votes.txt");
  votesFile = SD.open("votes.txt", FILE_WRITE);
  
  if (votesFile) {
    votesFile.print(voteCounts[0]);
    votesFile.print(",");
    votesFile.print(voteCounts[1]);
    votesFile.print(",");
    votesFile.println(voteCounts[2]);
    votesFile.close();
  }
}

void loadVotesFromSD() {
  if (SD.exists("votes.txt")) {
    votesFile = SD.open("votes.txt", FILE_READ);
    if (votesFile) {
      String data = "";
      while (votesFile.available()) {
        data += (char)votesFile.read();
      }
      votesFile.close();
      
      int a = 0, b = 0, c = 0;
      sscanf(data.c_str(), "%d,%d,%d", &a, &b, &c);
      voteCounts[0] = a;
      voteCounts[1] = b;
      voteCounts[2] = c;
    }
  }
}

void storeUIDtoSD(uint8_t* uid, uint8_t uidLen) {
  uidsFile = SD.open("uids.txt", FILE_WRITE);
  if (uidsFile) {
    for (int i = 0; i < uidLen; i++) {
      if (uid[i] < 0x10) uidsFile.print("0");
      uidsFile.print(uid[i], HEX);
      if (i < uidLen - 1) uidsFile.print(" ");
    }
    uidsFile.println();
    uidsFile.close();
  }
}

bool hasVoted(uint8_t* uid, uint8_t uidLen) {
  if (!SD.exists("uids.txt")) return false;
  
  uidsFile = SD.open("uids.txt", FILE_READ);
  if (!uidsFile) return false;
  
  uint8_t fileUID[7];
  
  while (uidsFile.available()) {
    String line = uidsFile.readStringUntil('\n');
    
    int idx = 0;
    for (int i = 0; i < uidLen && idx < line.length(); i++) {
      sscanf(line.c_str() + idx, "%2x", (unsigned int*)&fileUID[i]);
      idx += 3;
    }
    
    bool match = true;
    for (int i = 0; i < uidLen; i++) {
      if (fileUID[i] != uid[i]) {
        match = false;
        break;
      }
    }
    
    if (match) {
      uidsFile.close();
      return true;
    }
  }
  
  uidsFile.close();
  return false;
}

bool isMasterCard(uint8_t* uid, uint8_t uidLen) {
  if (uidLen != masterUIDLen) return false;
  for (int i = 0; i < uidLen; i++) {
    if (uid[i] != masterUID[i]) return false;
  }
  return true;
}

// ========== NEW RESET FUNCTION ==========

void resetVotingData() {
  // Reset vote counts in memory
  voteCounts[0] = 0;
  voteCounts[1] = 0;
  voteCounts[2] = 0;
  
  // Delete votes file from SD card
  if (SD.exists("votes.txt")) {
    SD.remove("votes.txt");
  }
  
  // Delete UIDs file from SD card (allows cards to vote again)
  if (SD.exists("uids.txt")) {
    SD.remove("uids.txt");
  }
  
  // Save empty vote counts to SD
  saveVotesToSD();
  
  // Automatically stop voting after reset
  votingActive = false;
}

// ========== UTILITY FUNCTIONS ==========

void beep(int duration) {
  digitalWrite(BUZZER, HIGH);
  delay(duration);
  digitalWrite(BUZZER, LOW);
}

void confirmBeep() {
  beep(100);
  delay(100);
  beep(100);
  delay(100);
  beep(200);
}

void errorBeep() {
  beep(500);
  delay(200);
  beep(500);
}