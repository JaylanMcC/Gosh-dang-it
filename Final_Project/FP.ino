//  I need to add commoemts to the code show whatI have done for the day. 
//  I need to also know how to add the  information about how we wull be changing up. 






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

// ---------- Long-press (20s) config ----------
const unsigned long HOLD_MS = 5000; // 5 seconds .. this means that you hold it 10 sec
bool lpArmed = false;                // are we timing a hold?
bool lpFired = false;                // did we already trigger during this hold?
unsigned long lpStartMs = 0;         // when the hold started

// ------------- Declarations -------------
void drawUI();
bool pushAllToThingSpeak(); // send all prof tallies (fields 1..6) + current + total
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
  // 1: P0-phrase1, 2: P0-phrase2, 3: P1-phrase1, 4: P1-phrase2,
  // 5: P2-phrase1, 6: P2-phrase2, 7: currentProf, 8: grand total
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
 // --- Splash screen ---
  M5.Lcd.fillScreen(WHITE);                // background color
  M5.Lcd.setTextSize(4);                   // make text big
  M5.Lcd.setTextColor(BLUE, WHITE);        // text color (blue on white)
  M5.Lcd.setCursor(30, 100);               // position text (x=30, y=100)
  M5.Lcd.println("GOSH");
  M5.Lcd.setCursor(30, 160);
  M5.Lcd.println("DARN IT");
  delay(5000);                             // show for 5 seconds

  // --- After splash, clear and continue as normal ---
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

  // Edge: button1 pressed now (LOW) and previously not pressed
  if (b1Pressed && lastButtonState1 == HIGH) {
    // Increment phrase1 for current professor on initial press
    ++phraseTally[currentProf][0];
    Serial.printf("Tally %s:%s = %d\n",
                  professors[currentProf], phrases[0], phraseTally[currentProf][0]);
    drawUI();

    // Start long-press timing
    lpArmed = true;
    lpFired = false;
    lpStartMs = millis();
  }

  // While holding, check duration
  if (b1Pressed && lpArmed && !lpFired) {
    unsigned long held = millis() - lpStartMs;
    // Optional: show countdown (small overlay)
    if (held % 1000 < 50) {
      M5.Lcd.setCursor(10, 190);
      M5.Lcd.setTextColor(YELLOW, profColors[currentProf]);
      M5.Lcd.printf("Hold Btn1: %lus / 5s   ", held / 1000);
    }
    if (held >= HOLD_MS) {
      // 20s reached: send all data to ThingSpeak, then flash LEDs
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
      lpFired = true; // don’t retrigger until button released
    }
  }

  // Release: clear long-press state
  if (!b1Pressed && lastButtonState1 == LOW) {
    lpArmed = false;
    lpFired = false;
  }
  lastButtonState1 = !b1Pressed; // store inverted

  // --- External button 2 -> phrase2 tally (no long-press on this one) ---
  bool b2Pressed = (digitalRead(BUTTON_PIN2) == LOW);
  if (b2Pressed && lastButtonState2 == HIGH) {
    ++phraseTally[currentProf][1];
    Serial.printf("Tally %s:%s = %d\n",
                  professors[currentProf], phrases[1], phraseTally[currentProf][1]);
    drawUI();
    delay(200); // debounce
  }
  lastButtonState2 = !b2Pressed;

  // Built-in M5 buttons switch professor (and color)
  if (M5.BtnA.wasPressed()) { currentProf = 0; drawUI(); }
  if (M5.BtnB.wasPressed()) { currentProf = 1; drawUI(); }
  if (M5.BtnC.wasPressed()) { currentProf = 2; drawUI(); }

  // NOTE: No auto-publish here anymore. Upload happens only on 20s hold of Btn1.
}
