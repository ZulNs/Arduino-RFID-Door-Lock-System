/*
 RFID Door Lock System
 
 
 Designed for Amelia Ipango's Final Project
 
 Created 13 February 2018
 @Gorontalo, Indonesia
 by ZulNs
 */

#include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal_I2C.h>

#define RST_PIN                 9    // Configurable, see typical pin layout above
#define SS_PIN                  10   // Configurable, see typical pin layout above
#define KNOWN_CARDS             3
#define ID_LENGTH               4
#define DOOR_CLOSE_SWITCH_PIN   2    // Sense by INT0
#define DOOR_UNLOCK_SWITCH_PIN  3    // Sense by INT1
#define DOOR_LOCKER_PIN         4
#define BUZZER_PIN              5
#define DOOR_STATE_LOCKED       0
#define DOOR_STATE_UNLOCKED     1
#define DOOR_STATE_OPENED       2
#define UNLOCKED_PERIOD         30000
#define SLEEP_PERIOD            60000

byte cardIds[][4] =
{
  { 0x05, 0xA0, 0x18, 0x48 },
  { 0xA5, 0x7A, 0x18, 0x48 },
  { 0xD5, 0xB8, 0x1F, 0x48 }
};

char * persons[] = { "ZulNs", "Ellen", "Amel" };

volatile boolean isInt0 = false;
volatile boolean isInt1 = false;
boolean isWaitingForSleep = false;
byte doorState;
long unlockedTimeout;
long sleepTimeout;

MFRC522 rfid(SS_PIN, RST_PIN);   // Create MFRC522 instance
MFRC522::MIFARE_Key key;
// creates lcd as LiquidCrystal object
LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup()
{ 
  SPI.begin(); // Init SPI bus
  rfid.PCD_Init(); // Init MFRC522
  lcd.init();
  lcd.backlight();

  PORTD |= bit(DOOR_CLOSE_SWITCH_PIN) | bit(DOOR_UNLOCK_SWITCH_PIN); // sets PD2 & PD3 as input pull-up
  bitSet(DDRD, DOOR_LOCKER_PIN); // sets PD4 as output
  EICRA |= bit(ISC00) | bit(ISC10); // INT0 & INT1 set on change state

  lcd.print(F("Door "));
  if (bitRead(PIND, DOOR_CLOSE_SWITCH_PIN))
  {
    lcd.print(F("opened..."));
    doorState = DOOR_STATE_OPENED;
    enableInt0();
    doorOpenedTone();
  }
  else
  {
    lcd.print(F("locked..."));
    lcd.setCursor(0, 1);
    lcd.print(F("Scan your card.."));
    doorState = DOOR_STATE_LOCKED;
    enableInt0();
    enableInt1();
    doorLockedTone();
  }

  for (byte i = 0; i < 6; i++)
  {
    key.keyByte[i] = 0xFF;
  }
  
  enableSleepLCD();
}

void loop()
{
  if (isWaitingForSleep && millis() >= sleepTimeout)
  {
    sleepLCD();
  }
  if (doorState == DOOR_STATE_LOCKED)
  {
    if (isInt0)
    {
      disableInt0();
      if (!ensureSwitchState(DOOR_CLOSE_SWITCH_PIN))
      {
        enableInt0();
        return;
      }
      disableInt1();
      lcd.clear();
      lcd.print(F("Door breaking"));
      lcd.setCursor(0, 1);
      lcd.print(F("detected!!!"));
      alertToneS(0);
    }
    
    if (isInt1)
    {
      disableInt1();
      if (isInt1 && ensureSwitchState(DOOR_UNLOCK_SWITCH_PIN))
      {
        enableInt1();
        return;
      }
      lcd.clear();
      lcd.backlight();
      lcd.print(F("Door unlocked..."));
      unlockDoor();
    }

    else if (detectCard())
    {
      disableInt1();
      unlockDoor();
    }
  }

  else if (doorState == DOOR_STATE_UNLOCKED)
  {
    if (millis() > unlockedTimeout)
    {
      bitClear(PORTD, DOOR_LOCKER_PIN);
      doorState = DOOR_STATE_LOCKED;
      enableInt1();
      lcd.clear();
      lcd.backlight();
      lcd.print(F("Door locked..."));
      lcd.setCursor(0, 1);
      lcd.print(F("Scan your card.."));
      doorLockedTone();
      enableSleepLCD();
      return;
    }
    
    if (isInt0)
    {
      disableInt0();
      if (!ensureSwitchState(DOOR_CLOSE_SWITCH_PIN))
      {
        enableInt0();
        return;
      }
      bitClear(PORTD, DOOR_LOCKER_PIN);
      doorState = DOOR_STATE_OPENED;
      enableInt0();
      lcd.clear();
      lcd.backlight();
      lcd.print(F("Door opened..."));
      doorOpenedTone();
      enableSleepLCD();
    }
  }

  else if (doorState == DOOR_STATE_OPENED)
  {
    if (isInt0)
    {
      disableInt0();
      if (ensureSwitchState(DOOR_CLOSE_SWITCH_PIN))
      {
        enableInt0();
        return;
      }
      doorState = DOOR_STATE_LOCKED;
      enableInt0();
      enableInt1();
      lcd.clear();
      lcd.backlight();
      lcd.print(F("Door locked..."));
      lcd.setCursor(0, 1);
      lcd.print(F("Scan your card.."));
      doorLockedTone();
      enableSleepLCD();
    }
  }
}

void unlockDoor()
{
  bitSet(PORTD, DOOR_LOCKER_PIN);
  doorState = DOOR_STATE_UNLOCKED;
  unlockedTimeout = millis() + UNLOCKED_PERIOD;
  doorUnlockedTone();
  enableSleepLCD();
}
 
ISR(INT0_vect)
{
  isInt0 = true;
}

ISR(INT1_vect)
{
  isInt1 = true;
}

void enableInt0()
{
  cli();
  bitSet(EIFR, INTF0); // clears any outstanding INT0 interrupt
  bitSet(EIMSK, INT0); // enables INT0 interrupt
  sei();
}

void disableInt0()
{
  cli();
  bitClear(EIMSK, INT0); // disables INT0 interrupt
  sei();
  isInt0 = false;
}

void enableInt1()
{
  cli();
  bitSet(EIFR, INTF1); // clears any outstanding INT1 interrupt
  bitSet(EIMSK, INT1); // enables INT1 interrupt
  sei();
}

void disableInt1()
{
  cli();
  bitClear(EIMSK, INT1); // disables INT1 interrupt
  sei();
  isInt1 = false;
}

boolean ensureSwitchState(byte switchPin)
{
  boolean state0 = bitRead(PIND, switchPin);
  boolean state1;
  while (true)
  {
    delay(50);
    state1 = bitRead(PIND, switchPin);
    if (state0 == state1);
    {
      return state0;
    }
    state0 = state1;
  }
}

void enableSleepLCD()
{
  sleepTimeout = millis() + SLEEP_PERIOD;
  isWaitingForSleep = true;
}

void sleepLCD()
{
  lcd.noBacklight();
  lcd.clear();
  isWaitingForSleep = false;
}

void alertToneS(byte ctr)
{
  while (ctr == 0)
  {
    alertTone();
  }
  while (ctr > 0 && !isInt0 && !isInt1)
  {
    alertTone();
    ctr--;
  }
  lcd.backlight();
}

void alertTone()
{
  float sinVal;
  int toneVal;
  lcd.backlight();
  for (byte i = 0; i < 180; i++)
  {
    sinVal = sin(i * 3.14159 / 180);
    toneVal = 2000 + int(sinVal * 2000);
    tone(BUZZER_PIN, toneVal);
    delay(2);
    if (i == 90)
    {
      lcd.noBacklight();
    }
  }
  noTone(BUZZER_PIN);
}

void doorOpenedTone()
{
  for (int i = 1700; i < 1944; i *= 1.01)
  {
    tone(BUZZER_PIN, i);
    delay(30);
  }
  noTone(BUZZER_PIN);
  delay(100);
  for (int i = 1944; i > 1808; i *= 0.99)
  {
    tone(BUZZER_PIN, i);
    delay(30);
  }
  noTone(BUZZER_PIN);
}

void doorLockedTone()
{
  for (int i = 1000; i < 2000; i *= 1.02)
  {
    tone(BUZZER_PIN, i);
    delay(10);
  }
  for (int i = 2000; i > 1000; i *= 0.98)
  {
    tone(BUZZER_PIN, i);
    delay(10);
  }
  noTone(BUZZER_PIN);
}

void doorUnlockedTone()
{
  tone(BUZZER_PIN, 1568);
  delay(200);
  tone(BUZZER_PIN, 1318);
  delay(200);
  tone(BUZZER_PIN, 1046, 200);
}

boolean detectCard()
{
  // Look for new cards
  if (!rfid.PICC_IsNewCardPresent())
  {
    return false;
  }
  
  // Verify if the NUID has been readed
  if (!rfid.PICC_ReadCardSerial())
  {
    return false;
  }
  
  MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);
  
  lcd.backlight();
  lcd.clear();
  
  // Check is the PICC of Classic MIFARE type
  if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&  
      piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
      piccType != MFRC522::PICC_TYPE_MIFARE_4K)
  {
    lcd.print(F("Unsupported card"));
    lcd.setCursor(0, 1);
    lcd.print(F("type!!!"));
    alertToneS(10);
    enableSleepLCD();
    return false;
  }

  boolean isKnownCard;
  byte person;
  for (byte i = 0; i < KNOWN_CARDS; i++)
  {
    isKnownCard = true;
    for (byte j = 0; j < ID_LENGTH; j++)
    {
      if (cardIds[i][j] != rfid.uid.uidByte[j])
      {
        isKnownCard = false;
        break;
      }
    }
    if (isKnownCard)
    {
      person = i;
      break;
    }
  }

  if (isKnownCard)
  {
    lcd.print(F("Hi "));
    lcd.print(persons[person]);
    lcd.print(F(","));
    lcd.setCursor(0, 1);
    lcd.print(F("door unlocked..."));
  }
  else
  {
    lcd.print(F("Unrecognized"));
    lcd.setCursor(0, 1);
    lcd.print(F("card!!!"));
    alertToneS(30);
    enableSleepLCD();
  }

  // Halt PICC
  rfid.PICC_HaltA();

  // Stop encryption on PCD
  rfid.PCD_StopCrypto1();

  return isKnownCard;
}

