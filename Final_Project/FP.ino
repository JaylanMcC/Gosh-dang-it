#include <M5Core2.h>
#include <WiFi.h>
#include "secrets.h"
#include "ThingSpeak.h"
#include <HTTPClient.h> // unused; safe to remove

// ---------------- Pins ----------------
const int BUTTON_PIN1 = 33;  // External button 1 (long-press trigger)
const int BUTTON_PIN2 = 26;  // External button 2
const int LED1 = 27;
const int LED2 = 19;
const int LED3 = 25;

// ---------------- WiFi/ThingSpeak ----------------
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

WiFiClient client;
unsigned long myChannelNumber = SECRET_CH_ID;
const char * myWriteAPIKey = SECRET_WRITE_APIKEY;

// ThingSpeak rate limit (still respected)
unsigned long lastTSPostMs = 0;
const unsigned long TS_MIN_MS = 16000; // >=15s

// ---------------- App State ----------------
bool lastButtonState1 = HIGH;
bool lastButtonState2 = HIGH;

// Professors / phrases
const char* professors[] = { "ProfA", "ProfB", "ProfC" };
const size_t NUM_PROFS = sizeof(professors) / sizeof(professors[0]);
const char* phrases[]   = { "phrase1", "phrase2" };
const size_t NUM_PHRASES = sizeof(phrases) / sizeof(phrases[0]);

// Screen color per professor
const uint16_t profColors[] = { RED, BLUE, GREEN };

int currentProf = 0;
int phraseTally[NUM_PROFS][NUM_PHRASES] = {0};

// ---------- Long-press (5s) config ----------
const unsigned long HOLD_MS = 5000; // === Changed: 5 seconds (was 20s)
bool lpArmed = false;                
bool lpFired = false;                
unsigned long lpStartMs = 0;         

// ------------- Declarations -------------
void drawUI();
bool pushAllToThingSpeak(); 
void flashLedsSequence(uint8_t rounds=2, uint16_t onMs=120, uint16_t gapMs=80);
static uint16_t textColorFor(uint16_t bg);

// -------- Helpers --------
static uint16_t textColorFor(uint16_t bg) {
  uint8_t r = ((bg >> 11) & 0x1F) * 255 / 31;
  uint8_t g = ((bg >> 5)  & 0x3F) * 255 / 63;
  uint8_t b = ( bg        & 0x1F) * 255 / 31;
  uint16_t luma = (uint16_t)((30 * r + 59 * g + 11 * b) / 100);
  return (luma > 140) ? BLACK : WHITE;
}

// UI
void drawUI() {
  uint16_t bg = profColors[currentProf % (sizeof(profColors)/sizeof(profColors[0]))];
  uint16_t fg = textColorFor(bg);
  M5.Lcd.fillScreen(bg);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(fg, bg);

  M5.Lcd.setCursor(10, 10);
  M5.Lcd.printf("Prof: %s (idx %d)", professors[currentProf], currentProf);

  M5.Lcd.setCursor(10, 40);
  M5.Lcd.printf("%s: %d", phrases[0], phraseTally[currentProf][0]);

  M5.Lcd.setCursor(10, 70);
  M5.Lcd.printf("%s: %d", phrases[1], phraseTally[currentProf][1]);

  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(10, 110);
  M5.Lcd.println("A/B/C = switch prof");
  M5.Lcd.setCursor(10, 135);
  M5.Lcd.println("Btn1/Btn2 = +phrase tallies");

  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 170);
  M5.Lcd.println("Hold Btn1 5s = SEND ALL");
}

// ThingSpeak (ALL fields)
bool pushAllToThingSpeak() {
  if (millis() - lastTSPostMs < TS_MIN_MS) {
    Serial.println("ThingSpeak: waiting for rate-limit window...");
    return false;
  }

  // Field mapping:
  // 1..6: tally per professor/phrase
  // 7: currentProf, 8: grand total
  ThingSpeak.setField(1, phraseTally[0][0]);
  ThingSpeak.setField(2, phraseTally[0][1]);
  ThingSpeak.setField(3, phraseTally[1][0]);
  ThingSpeak.setField(4, phraseTally[1][1]);
  ThingSpeak.setField(5, phraseTally[2][0]);
  ThingSpeak.setField(6, phraseTally[2][1]);
  ThingSpeak.setField(7, currentProf);

  int grand = 0;
  for (size_t p = 0; p < NUM_PROFS; ++p)
    for (size_t ph = 0; ph < NUM_PHRASES; ++ph)
      grand += phraseTally[p][ph];
  ThingSpeak.setField(8, grand);

  String status = String("ALL→ P0(") + phraseTally[0][0] + "," + phraseTally[0][1] + ") "
                + "P1(" + phraseTally[1][0] + "," + phraseTally[1][1] + ") "
                + "P2(" + phraseTally[2][0] + "," + phraseTally[2][1] + ") "
                + "cur=" + currentProf + " total=" + grand;
  ThingSpeak.setStatus(status);

  int httpCode = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  if (httpCode == 200) {
    Serial.println("ThingSpeak: writeFields OK (ALL)");
    lastTSPostMs = millis();
    return true;
  } else {
    Serial.print("ThingSpeak error (ALL): ");
    Serial.println(httpCode);
    lastTSPostMs = millis(); // still wait before retrying
    return false;
  }
}

// LED flash (sequential)
void flashLedsSequence(uint8_t rounds, uint16_t onMs, uint16_t gapMs) {
  int s1 = digitalRead(LED1);
  int s2 = digitalRead(LED2);
  int s3 = digitalRead(LED3);
  for (uint8_t r = 0; r < rounds; ++r) {
    digitalWrite(LED1, HIGH); delay(onMs); digitalWrite(LED1, LOW); delay(gapMs);
    digitalWrite(LED2, HIGH); delay(onMs); digitalWrite(LED2, LOW); delay(gapMs);
    digitalWrite(LED3, HIGH); delay(onMs); digitalWrite(LED3, LOW); delay(gapMs);
  }
  digitalWrite(LED1, s1);
  digitalWrite(LED2, s2);
  digitalWrite(LED3, s3);
}

void setup() {
  M5.begin();
  Serial.begin(115200);
 // --- Startup Splash screen ---
  M5.Lcd.fillScreen(WHITE);
  M5.Lcd.setTextSize(4);
  M5.Lcd.setTextColor(BLUE, WHITE);
  M5.Lcd.setCursor(30, 100);
  M5.Lcd.println("GOSH");
  M5.Lcd.setCursor(30, 160);
  M5.Lcd.println("DARN IT");
  delay(5000); // show 5s, then continue

  M5.Lcd.fillScreen(BLACK);

  // LEDs
  pinMode(LED1, OUTPUT); pinMode(LED2, OUTPUT); pinMode(LED3, OUTPUT);
  digitalWrite(LED1, LOW); digitalWrite(LED2, LOW); digitalWrite(LED3, LOW);
  flashLedsSequence(1, 120, 80);

  // Buttons
  pinMode(BUTTON_PIN1, INPUT_PULLUP);
  pinMode(BUTTON_PIN2, INPUT_PULLUP);

  // WiFi
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.println("Connecting WiFi...");
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) { Serial.print("."); delay(500); }
  Serial.println("\nConnected.");
  M5.Lcd.println("WiFi connected.");

  ThingSpeak.begin(client);

  drawUI();
}

void loop() {
  M5.update();

  // WiFi auto-reconnect
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Reconnecting WiFi: "); Serial.println(ssid);
    while (WiFi.status() != WL_CONNECTED) { WiFi.begin(ssid, pass); Serial.print("."); delay(2000); }
    Serial.println("\nWiFi reconnected.");
  }

  // --- External button 1 (phrase1 tally + long-press detection) ---
  bool b1Pressed = (digitalRead(BUTTON_PIN1) == LOW);

  if (b1Pressed && lastButtonState1 == HIGH) {
    ++phraseTally[currentProf][0]; // short press = tally phrase1
    Serial.printf("Tally %s:%s = %d\n",
                  professors[currentProf], phrases[0], phraseTally[currentProf][0]);
    drawUI();

    lpArmed = true; lpFired = false; lpStartMs = millis();
  }

  if (b1Pressed && lpArmed && !lpFired) {
    unsigned long held = millis() - lpStartMs;

    if (held % 1000 < 50) { // small countdown overlay
      M5.Lcd.setCursor(10, 190);
      M5.Lcd.setTextColor(YELLOW, profColors[currentProf]);
      M5.Lcd.printf("Hold Btn1: %lus / 5s   ", held / 1000);
    }

    if (held >= HOLD_MS) {
      // === Added: Send ALL + show splash after 5s hold ===
      bool ok = pushAllToThingSpeak();
      if (ok) {
        flashLedsSequence(2, 120, 80);
        M5.Lcd.setCursor(10, 210);
        M5.Lcd.setTextColor(GREEN, profColors[currentProf]);
        M5.Lcd.println("Uploaded ALL");
      } else {
        flashLedsSequence(1, 40, 40);
        M5.Lcd.setCursor(10, 210);
        M5.Lcd.setTextColor(RED, profColors[currentProf]);
        M5.Lcd.println("WAIT (rate limit)");
      }

      // === Added: Show "GOSH DARN IT" after upload ===
      M5.Lcd.fillScreen(WHITE);
      M5.Lcd.setTextSize(4);
      M5.Lcd.setTextColor(BLUE, WHITE);
      M5.Lcd.setCursor(30, 100);
      M5.Lcd.println("GOSH");
      M5.Lcd.setCursor(30, 160);
      M5.Lcd.println("DARN IT");
      delay(5000); // show 5s
      drawUI();    // return to UI

      lpFired = true;
    }
  }

  if (!b1Pressed && lastButtonState1 == LOW) { lpArmed = false; lpFired = false; }
  lastButtonState1 = !b1Pressed; 

  // --- External button 2 (phrase2 tally) ---
  bool b2Pressed = (digitalRead(BUTTON_PIN2) == LOW);
  if (b2Pressed && lastButtonState2 == HIGH) {
    ++phraseTally[currentProf][1];
    Serial.printf("Tally %s:%s = %d\n",
                  professors[currentProf], phrases[1], phraseTally[currentProf][1]);
    drawUI();
    delay(200);
  }
  lastButtonState2 = !b2Pressed;

  // Built-in M5 buttons switch professor
  if (M5.BtnA.wasPressed()) { currentProf = 0; drawUI(); }
  if (M5.BtnB.wasPressed()) { currentProf = 1; drawUI(); }
  if (M5.BtnC.wasPressed()) { currentProf = 2; drawUI(); }
}
