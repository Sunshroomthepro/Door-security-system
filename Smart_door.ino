
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>

// =====================================================
// WIFI SETTINGS
// =====================================================

const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Malaysia: UTC+8
const long GMT_OFFSET = 8 * 3600;
const int DAYLIGHT_OFFSET = 0;

// =====================================================
// PIN CONFIGURATION
// =====================================================

// TFT
#define TFT_SCLK 12
#define TFT_MOSI 13
#define TFT_RST  10
#define TFT_DC   9
#define TFT_CS   14

// Buttons
#define BTN_START  11
#define BTN_NEXT   4
#define BTN_SELECT 5

// Servo
#define SERVO_PIN 6

// =====================================================
// HARDWARE OBJECTS
// =====================================================

Adafruit_ST7789 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
Servo doorServo;
Preferences preferences;

// =====================================================
// SERVO SETTINGS
// =====================================================

const int SERVO_STOP   = 1500;
const int SERVO_UNLOCK = 1800;
const int SERVO_LOCK   = 1200;

const unsigned long UNLOCK_TIME = 500;
const unsigned long LOCK_TIME = 500;
const unsigned long DOOR_OPEN_TIME = 5000;

// =====================================================
// SECURITY SETTINGS
// =====================================================

const int MAX_ATTEMPTS = 3;
const unsigned long LOCKOUT_TIME = 30000;

int failedAttempts = 0;

unsigned long lockoutUntil = 0;
unsigned long screenTimer = 0;
unsigned long servoTimer = 0;
unsigned long lastCountdownUpdate = 0;

// =====================================================
// PASSWORD
// =====================================================

String password = "123456";
String entered = "";
String newPassword = "";

int selectedDigit = 0;
int menuSelection = 0;

bool changingPassword = false;

// =====================================================
// COLORS
// =====================================================

#define BG       0x0841
#define PANEL    0x10A3
#define CYAN     0x05FF
#define WHITE    0xFFFF
#define MUTED    0x9CF3
#define GREEN    0x07E0
#define RED      0xF800
#define YELLOW   0xFFE0
#define DARKBLUE 0x020D

// =====================================================
// SCREEN STATES
// =====================================================

enum Screen {
  HOME,
  MENU,
  ENTER_CURRENT,
  ENTER_NEW,
  CONFIRM_NEW,
  ACCESS_DENIED,
  SAVED,
  LOCKOUT,
  HISTORY,
  DOOR_UNLOCKING,
  DOOR_OPEN,
  DOOR_LOCKING
};

Screen screen = HOME;

// =====================================================
// TIMERS
// =====================================================

unsigned long lastTopBarUpdate = 0;
unsigned long lastAnimationUpdate = 0;
unsigned long lastWifiCheck = 0;

bool animationState = false;
bool clockAvailable = false;

// =====================================================
// BUTTON DEBOUNCE
// =====================================================

const int buttonPins[3] = {
  BTN_START,
  BTN_NEXT,
  BTN_SELECT
};

bool lastReading[3] = {HIGH, HIGH, HIGH};
bool stableState[3] = {HIGH, HIGH, HIGH};

unsigned long lastDebounce[3] = {0, 0, 0};

bool buttonPressed(int pin) {

  int index = -1;

  for (int i = 0; i < 3; i++) {
    if (buttonPins[i] == pin) {
      index = i;
      break;
    }
  }

  if (index == -1) return false;

  bool reading = digitalRead(pin);

  if (reading != lastReading[index]) {
    lastDebounce[index] = millis();
    lastReading[index] = reading;
  }

  if (millis() - lastDebounce[index] > 35) {

    if (reading != stableState[index]) {

      stableState[index] = reading;

      if (stableState[index] == LOW) {
        return true;
      }
    }
  }

  return false;
}

// =====================================================
// CLOCK
// =====================================================

String getClockString() {

  struct tm timeinfo;

  if (getLocalTime(&timeinfo, 10)) {

    clockAvailable = true;

    char buffer[12];

    strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);

    return String(buffer);
  }

  clockAvailable = false;

  unsigned long seconds = millis() / 1000;

  unsigned long hours = seconds / 3600;
  unsigned long minutes = (seconds / 60) % 60;
  unsigned long secs = seconds % 60;

  char buffer[16];

  snprintf(
    buffer,
    sizeof(buffer),
    "UP %02lu:%02lu",
    hours,
    minutes
  );

  return String(buffer);
}

String getTimestamp() {

  struct tm timeinfo;

  if (getLocalTime(&timeinfo, 10)) {

    char buffer[20];

    strftime(
      buffer,
      sizeof(buffer),
      "%H:%M:%S",
      &timeinfo
    );

    return String(buffer);
  }

  unsigned long seconds = millis() / 1000;

  char buffer[20];

  snprintf(
    buffer,
    sizeof(buffer),
    "UP+%02lu:%02lu",
    (seconds / 3600),
    ((seconds / 60) % 60)
  );

  return String(buffer);
}

// =====================================================
// WIFI
// =====================================================

void startWiFi() {

  if (
    String(WIFI_SSID) == "MSYSDESTCC" ||
    String(WIFI_SSID).length() == 0
  ) {
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, );

  lastWifiCheck = millis();
}

void updateWiFi() {

  if (String(WIFI_SSID) == "MSYSDESTCC") {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {

    static bool timeRequested = false;

    if (!timeRequested) {

      configTime(
        GMT_OFFSET,
        DAYLIGHT_OFFSET,
        "pool.ntp.org",
        "time.google.com"
      );

      timeRequested = true;
    }

  } else if (millis() - lastWifiCheck >= 15000) {

    lastWifiCheck = millis();

    WiFi.reconnect();
  }
}

// =====================================================
// ANIMATED TOP BAR
// =====================================================

void drawTopBar() {

  tft.fillRect(185, 5, 135, 47, BG);

  bool connected = WiFi.status() == WL_CONNECTED;

  uint16_t indicatorColor =
    connected ? GREEN : YELLOW;

  if (animationState) {
    tft.fillCircle(196, 24, 5, indicatorColor);
  } else {
    tft.drawCircle(196, 24, 5, indicatorColor);
  }

  tft.setTextSize(1);
  tft.setTextColor(WHITE);

  tft.setCursor(207, 20);
  tft.print(getClockString());
}

void updateTopBar() {

  if (millis() - lastTopBarUpdate >= 1000) {

    lastTopBarUpdate = millis();

    drawTopBar();
  }

  if (millis() - lastAnimationUpdate >= 500) {

    lastAnimationUpdate = millis();

    animationState = !animationState;

    drawTopBar();
  }
}

// =====================================================
// DISPLAY HEADER
// =====================================================

void drawHeader(const char* title) {

  tft.fillScreen(BG);

  tft.setTextSize(2);
  tft.setTextColor(CYAN);

  tft.setCursor(16, 13);
  tft.print("NEXUS");

  tft.setTextSize(1);
  tft.setTextColor(MUTED);

  tft.setCursor(16, 39);
  tft.print(title);

  tft.drawFastHLine(16, 57, 288, PANEL);

  drawTopBar();
}

// =====================================================
// HOME SCREEN
// =====================================================

void drawHome() {

  screen = HOME;

  drawHeader("SMART ACCESS SYSTEM");

  tft.fillRoundRect(16, 76, 288, 112, 12, PANEL);

  tft.setTextSize(2);
  tft.setTextColor(WHITE);

  tft.setCursor(34, 99);
  tft.print("DOOR SECURITY");

  tft.setTextSize(1);
  tft.setTextColor(MUTED);

  tft.setCursor(34, 130);
  tft.print("System is ready.");

  tft.fillRoundRect(34, 151, 252, 25, 7, DARKBLUE);

  tft.setTextColor(CYAN);
  tft.setCursor(65, 160);
  tft.print("PRESS START TO CONTINUE");

  tft.setTextColor(MUTED);
  tft.setCursor(16, 216);
  tft.print("ESP32-S3  |  SECURE ENTRY");
}

// =====================================================
// MAIN MENU
// =====================================================

void drawMenu() {

  screen = MENU;

  drawHeader("MAIN MENU");

  tft.setTextSize(2);
  tft.setTextColor(WHITE);
  tft.setCursor(20, 68);
  tft.print("Select an option");

  const char* options[3] = {
    "Unlock door",
    "Change password",
    "Access history"
  };

  for (int i = 0; i < 3; i++) {

    int y = 96 + i * 43;

    uint16_t fill =
      (i == menuSelection) ? CYAN : PANEL;

    uint16_t text =
      (i == menuSelection) ? BG : WHITE;

    tft.fillRoundRect(20, y, 280, 35, 8, fill);

    tft.setTextSize(2);
    tft.setTextColor(text);
    tft.setCursor(34, y + 9);
    tft.print(options[i]);

    if (i == menuSelection) {
      tft.fillCircle(281, y + 17, 5, BG);
    }
  }

  tft.setTextSize(1);
  tft.setTextColor(MUTED);
  tft.setCursor(16, 225);
  tft.print("NEXT: MOVE  SELECT: OPEN  START: BACK");
}

// =====================================================
// ACCESS HISTORY
// =====================================================

void saveHistory(const String& eventName) {

  for (int i = 4; i > 0; i--) {

    String previous =
      preferences.getString(("h" + String(i - 1)).c_str(), "");

    preferences.putString(
      ("h" + String(i)).c_str(),
      previous
    );
  }

  String entry = eventName + " " + getTimestamp();

  preferences.putString("h0", entry);

  Serial.println("HISTORY: " + entry);
}

void drawHistory() {

  screen = HISTORY;

  drawHeader("RECENT ACCESS ACTIVITY");

  tft.setTextSize(1);
  tft.setTextColor(MUTED);
  tft.setCursor(20, 70);
  tft.print("LATEST 5 EVENTS");

  for (int i = 0; i < 5; i++) {

    int y = 91 + i * 27;

    String entry =
      preferences.getString(("h" + String(i)).c_str(), "");

    tft.fillRoundRect(16, y, 288, 23, 5, PANEL);

    if (entry.length() == 0) {

      tft.setTextColor(MUTED);
      tft.setCursor(27, y + 8);
      tft.print("No recorded activity");

    } else {

      bool success = entry.startsWith("SUCCESS");

      tft.fillCircle(
        28,
        y + 11,
        4,
        success ? GREEN : RED
      );

      tft.setTextSize(1);
      tft.setTextColor(WHITE);
      tft.setCursor(41, y + 8);
      tft.print(entry);
    }
  }

  tft.setTextSize(1);
  tft.setTextColor(CYAN);
  tft.setCursor(16, 229);
  tft.print("START: BACK");
}

// =====================================================
// PASSWORD DISPLAY
// =====================================================

void drawPasswordDots() {

  tft.fillRect(16, 69, 288, 29, BG);

  for (int i = 0; i < 6; i++) {

    int x = 57 + i * 41;

    if (i < entered.length()) {
      tft.fillCircle(x, 82, 7, CYAN);
    } else {
      tft.drawCircle(x, 82, 7, MUTED);
    }
  }
}

void drawKey(int digit) {

  int col = digit % 5;
  int row = digit / 5;

  int x = 16 + col * 59;
  int y = 116 + row * 51;

  bool selected = (digit == selectedDigit);

  uint16_t fill = selected ? CYAN : PANEL;
  uint16_t text = selected ? BG : WHITE;

  tft.fillRoundRect(x, y, 51, 42, 7, fill);

  tft.setTextSize(2);
  tft.setTextColor(text);
  tft.setCursor(x + 18, y + 12);
  tft.print(digit);
}

void drawAllKeys() {

  for (int i = 0; i < 10; i++) {
    drawKey(i);
  }
}

void drawPasswordScreen(const char* title) {

  drawHeader(title);

  tft.setTextSize(1);
  tft.setTextColor(MUTED);
  tft.setCursor(16, 69);
  tft.print("PASSWORD");

  drawPasswordDots();
  drawAllKeys();

  tft.setTextSize(1);
  tft.setTextColor(MUTED);
  tft.setCursor(16, 222);
  tft.print("NEXT: MOVE  SELECT: ENTER  START: DELETE");
}

void refreshPasswordScreen() {

  drawPasswordDots();
  drawAllKeys();
}

void beginPasswordEntry(Screen target, const char* title) {

  entered = "";
  selectedDigit = 0;

  screen = target;

  drawPasswordScreen(title);
}

// =====================================================
// STATUS SCREENS
// =====================================================

void drawDoorUnlocking() {

  drawHeader("ACCESS GRANTED");

  tft.fillCircle(160, 115, 28, GREEN);

  tft.setTextSize(3);
  tft.setTextColor(BG);
  tft.setCursor(150, 101);
  tft.print("!");

  tft.setTextSize(2);
  tft.setTextColor(GREEN);
  tft.setCursor(83, 164);
  tft.print("UNLOCKING...");
}

void drawDoorOpen() {

  drawHeader("ACCESS GRANTED");

  tft.fillCircle(160, 111, 29, GREEN);

  tft.setTextSize(2);
  tft.setTextColor(BG);
  tft.setCursor(141, 103);
  tft.print("OK");

  tft.setTextSize(2);
  tft.setTextColor(GREEN);
  tft.setCursor(94, 155);
  tft.print("DOOR OPEN");

  tft.setTextSize(1);
  tft.setTextColor(MUTED);
  tft.setCursor(81, 184);
  tft.print("Automatic locking enabled");
}

void drawDenied() {

  screen = ACCESS_DENIED;

  drawHeader("SECURITY ALERT");

  tft.setTextSize(2);
  tft.setTextColor(RED);
  tft.setCursor(86, 105);
  tft.print("ACCESS DENIED");

  tft.setTextSize(1);
  tft.setTextColor(WHITE);
  tft.setCursor(91, 143);
  tft.print("Incorrect password");

  tft.setTextColor(YELLOW);
  tft.setCursor(91, 170);
  tft.print("Attempts left: ");
  tft.print(MAX_ATTEMPTS - failedAttempts);

  screenTimer = millis();
}

void drawSaved() {

  screen = SAVED;

  drawHeader("SETTINGS UPDATED");

  tft.setTextSize(2);
  tft.setTextColor(GREEN);
  tft.setCursor(65, 111);
  tft.print("PASSWORD SAVED");

  tft.setTextSize(1);
  tft.setTextColor(MUTED);
  tft.setCursor(75, 145);
  tft.print("Your new password is active.");

  screenTimer = millis();
}

void drawLockout() {

  screen = LOCKOUT;

  drawHeader("SECURITY LOCKOUT");

  tft.setTextSize(2);
  tft.setTextColor(RED);
  tft.setCursor(80, 99);
  tft.print("TOO MANY TRIES");

  updateLockoutCountdown();
}

void updateLockoutCountdown() {

  tft.fillRect(70, 143, 180, 30, BG);

  unsigned long remaining =
    (lockoutUntil > millis()) ?
    (lockoutUntil - millis() + 999) / 1000 : 0;

  tft.setTextSize(2);
  tft.setTextColor(YELLOW);
  tft.setCursor(105, 150);
  tft.print("Wait ");
  tft.print(remaining);
  tft.print("s");
}

// =====================================================
// SERVO CONTROL
// =====================================================

void stopServo() {
  doorServo.writeMicroseconds(SERVO_STOP);
}

void startUnlock() {

  doorServo.writeMicroseconds(SERVO_UNLOCK);

  servoTimer = millis();

  screen = DOOR_UNLOCKING;

  saveHistory("SUCCESS");

  drawDoorUnlocking();
}

void startLock() {

  doorServo.writeMicroseconds(SERVO_LOCK);

  servoTimer = millis();

  screen = DOOR_LOCKING;

  drawHeader("AUTOMATIC LOCKING");

  tft.setTextSize(2);
  tft.setTextColor(YELLOW);
  tft.setCursor(83, 115);
  tft.print("LOCKING...");
}

void updateServo() {

  if (screen == DOOR_UNLOCKING) {

    if (millis() - servoTimer >= UNLOCK_TIME) {

      stopServo();

      screen = DOOR_OPEN;
      screenTimer = millis();

      drawDoorOpen();
    }
  }

  else if (screen == DOOR_OPEN) {

    if (millis() - screenTimer >= DOOR_OPEN_TIME) {
      startLock();
    }
  }

  else if (screen == DOOR_LOCKING) {

    if (millis() - servoTimer >= LOCK_TIME) {

      stopServo();

      failedAttempts = 0;

      drawHome();
    }
  }
}

// =====================================================
// SECURITY LOGIC
// =====================================================

void registerFailedAttempt() {

  failedAttempts++;

  saveHistory("FAILED");

  if (failedAttempts >= MAX_ATTEMPTS) {

    lockoutUntil = millis() + LOCKOUT_TIME;

    drawLockout();

  } else {

    drawDenied();
  }
}

void checkCurrentPassword() {

  if (entered == password) {

    failedAttempts = 0;

    if (changingPassword) {

      beginPasswordEntry(
        ENTER_NEW,
        "CREATE NEW PASSWORD"
      );

    } else {

      startUnlock();
    }

  } else {

    registerFailedAttempt();
  }
}

void finishNewPassword() {

  if (entered == newPassword) {

    password = newPassword;

    preferences.putString("password", password);

    failedAttempts = 0;
    changingPassword = false;

    drawSaved();

  } else {

    newPassword = "";

    beginPasswordEntry(
      ENTER_NEW,
      "PASSWORDS DID NOT MATCH"
    );
  }
}

// =====================================================
// DIGIT SELECTION
// =====================================================

void selectDigit() {

  if (entered.length() >= 6) return;

  entered += char('0' + selectedDigit);

  drawPasswordDots();

  if (entered.length() == 6) {

    if (screen == ENTER_CURRENT) {

      checkCurrentPassword();

    }

    else if (screen == ENTER_NEW) {

      newPassword = entered;

      beginPasswordEntry(
        CONFIRM_NEW,
        "CONFIRM NEW PASSWORD"
      );
    }

    else if (screen == CONFIRM_NEW) {

      finishNewPassword();
    }
  }
}

// =====================================================
// PASSWORD BUTTONS
// =====================================================

void handlePasswordButtons() {

  if (buttonPressed(BTN_NEXT)) {

    selectedDigit++;

    if (selectedDigit > 9) {
      selectedDigit = 0;
    }

    drawAllKeys();
  }

  if (buttonPressed(BTN_SELECT)) {
    selectDigit();
  }

  if (buttonPressed(BTN_START)) {

    if (entered.length() > 0) {

      entered.remove(entered.length() - 1);

      refreshPasswordScreen();

    } else {

      changingPassword = false;

      drawMenu();
    }
  }
}

// =====================================================
// MAIN BUTTON HANDLING
// =====================================================

void handleButtons() {

  if (screen == HOME) {

    if (buttonPressed(BTN_START)) {

      menuSelection = 0;

      drawMenu();
    }
  }

  else if (screen == MENU) {

    if (buttonPressed(BTN_NEXT)) {

      menuSelection++;

      if (menuSelection > 2) {
        menuSelection = 0;
      }

      drawMenu();
    }

    if (buttonPressed(BTN_SELECT)) {

      if (menuSelection == 0) {

        changingPassword = false;

        beginPasswordEntry(
          ENTER_CURRENT,
          "ENTER PASSWORD"
        );

      }

      else if (menuSelection == 1) {

        changingPassword = true;

        beginPasswordEntry(
          ENTER_CURRENT,
          "VERIFY CURRENT PASSWORD"
        );

      }

      else if (menuSelection == 2) {

        drawHistory();
      }
    }

    if (buttonPressed(BTN_START)) {
      drawHome();
    }
  }

  else if (screen == HISTORY) {

    if (buttonPressed(BTN_START)) {
      drawMenu();
    }
  }

  else if (
    screen == ENTER_CURRENT ||
    screen == ENTER_NEW ||
    screen == CONFIRM_NEW
  ) {

    handlePasswordButtons();
  }
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  pinMode(BTN_START, INPUT_PULLUP);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);

  SPI.begin(
    TFT_SCLK,
    -1,
    TFT_MOSI,
    TFT_CS
  );

  tft.init(240, 320);
  tft.setRotation(1);
  tft.setSPISpeed(40000000);

  preferences.begin("smartlock", false);

  password = preferences.getString(
    "password",
    "123456"
  );

  if (password.length() != 6) {

    password = "123456";

    preferences.putString(
      "password",
      password
    );
  }

  doorServo.setPeriodHertz(50);

  doorServo.attach(
    SERVO_PIN,
    500,
    2400
  );

  stopServo();

  startWiFi();

  drawHome();
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {

  updateWiFi();

  updateTopBar();

  // Security lockout
  if (screen == LOCKOUT) {

    if (millis() >= lockoutUntil) {

      failedAttempts = 0;

      drawHome();

    } else if (
      millis() - lastCountdownUpdate >= 250
    ) {

      lastCountdownUpdate = millis();

      updateLockoutCountdown();
    }

    return;
  }

  // Wrong password screen
  if (screen == ACCESS_DENIED) {

    if (millis() - screenTimer >= 1800) {

      changingPassword = false;

      drawMenu();
    }

    return;
  }

  // Password saved screen
  if (screen == SAVED) {

    if (millis() - screenTimer >= 1800) {
      drawHome();
    }

    return;
  }

  updateServo();

  // Ignore button input during door movement
  if (
    screen == DOOR_OPEN ||
    screen == DOOR_UNLOCKING ||
    screen == DOOR_LOCKING
  ) {
    return;
  }

  handleButtons();
}
