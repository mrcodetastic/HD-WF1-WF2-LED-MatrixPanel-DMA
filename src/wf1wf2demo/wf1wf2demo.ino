// Custom LED Matrix Firmware (leveraging the HUB75 DMA library) for the Huidu HUB75 Series Control Cards.
// Example shop link: https://www.aliexpress.com/item/1005005038544582.html -> WF1
// Example shop link: https://www.aliexpress.com/item/1005002271988988.html -> WF2

//#define WF1

#if defined(WF1)
  #include "hd-wf1-esp32s2-config.h"
#elif defined(WF2)
  #include "hd-wf2-esp32s3-config.h"
#else
  #error "Please define either WF1 or WF2"
#endif  


#include <esp_err.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include "debug.h"
#include "littlefs_core.h"
#include <ctime>
#include "driver/ledc.h"
#include "driver/rtc_io.h"

#include <Arduino.h>
#include <new>
#include <WiFi.h>
#include <WiFiMulti.h>

#include <WebServer.h>
#include <ESPmDNS.h>
#include <I2C_BM8563.h>   // https://github.com/tanakamasayuki/I2C_BM8563

// WiFi credentials are kept out of source control. Copy
// src/secrets.h.example to src/secrets.h and fill in your own values.
#if __has_include("secrets.h")
  #include "secrets.h"
#endif

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
// OTA is optional. To enable it, uncomment the include and the
// ElegantOTA.begin() call below and add the ElegantOTA dependency.
//#include <ElegantOTA.h> // upload firmware by going to http://<ipaddress>/update

#include <Bounce2.h>

#ifndef PI
#define PI 3.14159265358979323846
#endif

/*----------------------------- Wifi Configuration -------------------------------*/

// Supplied by the git-ignored secrets.h. The fallbacks keep the sketch
// compiling out of the box and make it obvious that configuration is missing.
#ifndef WIFI_SSID
  #define WIFI_SSID "xxxx"
#endif
#ifndef WIFI_PASS
  #define WIFI_PASS "yyyy"
#endif

/*----------------------------- RTC and NTP -------------------------------*/

I2C_BM8563 rtc(I2C_BM8563_DEFAULT_ADDRESS, Wire1);
const char* ntpServer         = "time.cloudflare.com";
const char* ntpLastUpdate     = "/ntp_last_update.txt";

// POSIX timezone string. UK: GMT with British Summer Time.
// Change this for your region, e.g. "CET-1CEST,M3.5.0,M10.5.0/3".
const char* clockTz = "GMT0BST,M3.5.0/1,M10.5.0";

/*-------------------------- HUB75E DMA Setup -----------------------------*/
#define PANEL_RES_X 64      // Number of pixels wide of each INDIVIDUAL panel module. 
#define PANEL_RES_Y 32     // Number of pixels tall of each INDIVIDUAL panel module.
#define PANEL_CHAIN 1      // Total number of panels chained one to another


#if defined(WF1)

HUB75_I2S_CFG::i2s_pins _pins_x1 = {WF1_R1_PIN, WF1_G1_PIN, WF1_B1_PIN, WF1_R2_PIN, WF1_G2_PIN, WF1_B2_PIN, WF1_A_PIN, WF1_B_PIN, WF1_C_PIN, WF1_D_PIN, WF1_E_PIN, WF1_LAT_PIN, WF1_OE_PIN, WF1_CLK_PIN};

#else

HUB75_I2S_CFG::i2s_pins _pins_x1 = {WF2_X1_R1_PIN, WF2_X1_G1_PIN, WF2_X1_B1_PIN, WF2_X1_R2_PIN, WF2_X1_G2_PIN, WF2_X1_B2_PIN, WF2_A_PIN, WF2_B_PIN, WF2_C_PIN, WF2_D_PIN, WF2_X1_E_PIN, WF2_LAT_PIN, WF2_OE_PIN, WF2_CLK_PIN};
HUB75_I2S_CFG::i2s_pins _pins_x2 = {WF2_X2_R1_PIN, WF2_X2_G1_PIN, WF2_X2_B1_PIN, WF2_X2_R2_PIN, WF2_X2_G2_PIN, WF2_X2_B2_PIN, WF2_A_PIN, WF2_B_PIN, WF2_C_PIN, WF2_D_PIN, WF2_X2_E_PIN, WF2_LAT_PIN, WF2_OE_PIN, WF2_CLK_PIN};

#endif


/*-------------------------- Class Instances ------------------------------*/
// Routing in the root page and webcamview.html natively uses the request
// handlers of the ESP32 WebServer class, so it explicitly instantiates the
// ESP32 WebServer.
WebServer           webServer;
WiFiMulti           wifiMulti;
MatrixPanel_I2S_DMA *dma_display = nullptr;

// INSTANTIATE A Button OBJECT FROM THE Bounce2 NAMESPACE
Bounce2::Button button = Bounce2::Button();

// FreeRTOS task management
TaskHandle_t Task1;

#include "led_pwm_handler.h"

RTC_DATA_ATTR int bootCount = 0;

// Display modes
enum DisplayMode {
  MODE_CLOCK_WITH_ANIMATION = 0,
  MODE_CLOCK_ONLY = 1,
  MODE_BOUNCING_SQUARES = 2,
  MODE_COUNT = 3
};

DisplayMode currentDisplayMode = MODE_CLOCK_WITH_ANIMATION;
unsigned long buttonPressStartTime = 0;
bool buttonPressHandled = false;

const unsigned long LONG_PRESS_MS = 2000;

// Bouncing squares animation variables
struct BouncingSquare {
  float x, y;
  float vx, vy;
  uint16_t color;
  int size;
};

const int NUM_SQUARES = 3;
BouncingSquare squares[NUM_SQUARES];

// Frame rate limit for the bouncing-squares mode.
//
// loop() runs as fast as the display refresh allows (hundreds of frames per
// second), and the squares move by a fixed amount per frame, so without a cap
// the animation is far too fast. Rendering at a fixed ~25 fps makes the speed
// predictable and independent of how quickly the rest of the loop runs.
const unsigned long BOUNCING_FRAME_MS = 40;   // 1000 ms / 40 ms = 25 fps
unsigned long lastBouncingFrameMs = 0;

// Cached clock overlay for bouncing-squares mode
char overlayBuffer[8] = "--:--";
unsigned long lastOverlayUpdate = 0;
bool overlayValid = false;

// Initialize bouncing squares
void initBouncingSquares() {
  for (int i = 0; i < NUM_SQUARES; i++) {
    squares[i].x = random(0, PANEL_RES_X - 8);
    squares[i].y = random(0, PANEL_RES_Y - 8);
    squares[i].vx = random(1, 4) * (random(0, 2) ? 1 : -1);
    squares[i].vy = random(1, 4) * (random(0, 2) ? 1 : -1);
    squares[i].size = random(4, 8);
    squares[i].color = dma_display->color565(random(100, 255), random(100, 255), random(100, 255));
  }
}

// Update and draw bouncing squares
void updateBouncingSquares() {
  // Throttle the animation to ~25 fps; skip this frame if not due yet.
  const unsigned long now = millis();
  if (lastBouncingFrameMs != 0 && (now - lastBouncingFrameMs) < BOUNCING_FRAME_MS) {
    return;
  }
  lastBouncingFrameMs = now;

  dma_display->clearScreen();
  
  for (int i = 0; i < NUM_SQUARES; i++) {
    // Update position
    squares[i].x += squares[i].vx * 0.5;
    squares[i].y += squares[i].vy * 0.5;
    
    // Bounce off walls
    if (squares[i].x <= 0 || squares[i].x >= PANEL_RES_X - squares[i].size) {
      squares[i].vx = -squares[i].vx;
      squares[i].x = constrain(squares[i].x, 0, PANEL_RES_X - squares[i].size);
    }
    if (squares[i].y <= 0 || squares[i].y >= PANEL_RES_Y - squares[i].size) {
      squares[i].vy = -squares[i].vy;
      squares[i].y = constrain(squares[i].y, 0, PANEL_RES_Y - squares[i].size);
    }
    
    // Draw square
    dma_display->fillRect((int)squares[i].x, (int)squares[i].y, squares[i].size, squares[i].size, squares[i].color);
  }
}



/*
Method to print the reason by which ESP32
has been awaken from sleep
*/
void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}

// Block until the push button has been physically released and the Bounce2
// state machine has settled on the released state.
//
// This is required before entering deep sleep: the EXT0 wake is LEVEL
// triggered on LOW, so if the chip is put to sleep while the button is still
// held, the wake condition is already active and the chip wakes again
// immediately (the observed "releasing the button wakes it up" behaviour).
//
// Returns true if the button was released within timeoutMs.
bool waitForButtonRelease(unsigned long timeoutMs) {
  const unsigned long holdStart = millis();
  while (digitalRead(PUSH_BUTTON_PIN) == LOW) {
    button.update();                 // keep Bounce2 in sync while waiting
    if (millis() - holdStart >= timeoutMs) {
      Serial.printf("[sleep] button still held after %lu ms\n", timeoutMs);
      return false;
    }
    delay(5);
  }

  // Let the debouncer confirm the released state before continuing.
  const unsigned long settleStart = millis();
  while (millis() - settleStart < 50) {
    button.update();
    delay(5);
  }
  return true;
}

// Prepare the hardware and enter deep sleep.
void enterDeepSleep() {
  Serial.println("Preparing for deep sleep...");

  // Blank the panel and turn the brightness off.
  dma_display->clearScreen();
  dma_display->setBrightness8(0);

  // Do not sleep while the button that triggers sleep is still held: the EXT0
  // wake level would already be active and the chip would wake immediately.
  const unsigned long releaseTimeoutMs = 10000;
  if (!waitForButtonRelease(releaseTimeoutMs)) {
    Serial.println("[sleep] aborting sleep - the long press will not retrigger"
                   " until the button is released");
    dma_display->setBrightness8(128);
    dma_display->clearScreen();
    buttonPressHandled = true;   // consume this press; loop() re-arms on release
    return;
  }

  // Keep the button pin pulled up while the RTC domain is powered.
  rtc_gpio_pullup_en((gpio_num_t)PUSH_BUTTON_PIN);
  rtc_gpio_pulldown_dis((gpio_num_t)PUSH_BUTTON_PIN);

  // Final safety check: the wake level must be inactive, otherwise the chip
  // would wake up as soon as it sleeps.
  const int pinLevel = digitalRead(PUSH_BUTTON_PIN);
  Serial.printf("[sleep] millis=%lu bootCount=%d pin=%d button.isPressed()=%d\n",
                (unsigned long)millis(), bootCount, pinLevel,
                (int)button.isPressed());
  if (pinLevel == LOW) {
    Serial.println("[sleep] wake level still active - aborting sleep");
    dma_display->setBrightness8(128);
    dma_display->clearScreen();
    buttonPressHandled = true;
    return;
  }

  // Release network and filesystem resources.
  MDNS.end();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  LittleFS.end();

  delay(100);
  Serial.println("[sleep] entering deep sleep - press the button to wake");
  Serial.flush();
  esp_deep_sleep_start();
}


// Returns true if the system clock has a plausible date (after 2020).
bool isSystemTimeValid() {
  return time(nullptr) > 1600000000;
}

// Convert a broken-down *UTC* time to a Unix epoch.
//
// The external RTC stores UTC (see the NTP write path). Using mktime() here
// would interpret the fields as local time and apply the timezone offset
// twice, so the conversion is done explicitly instead.
time_t utcTmToEpoch(const struct tm* tmUtc) {
  const int year  = tmUtc->tm_year + 1900;
  const int month = tmUtc->tm_mon + 1; // 1-12
  const int y     = year - (month <= 2 ? 1 : 0);
  const int era   = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153u * (unsigned)(month + (month > 2 ? -3 : 9)) + 2u) / 5u
                       + (unsigned)tmUtc->tm_mday - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  const long long days = (long long)era * 146097LL + (long long)doe - 719468LL;

  return (time_t)(days * 86400LL +
                  (long long)tmUtc->tm_hour * 3600LL +
                  (long long)tmUtc->tm_min * 60LL +
                  (long long)tmUtc->tm_sec);
}

// Seed the system clock from a UTC epoch value.
bool setSystemTimeFromEpoch(time_t epoch) {
  if (epoch <= 0) {
    return false;
  }
  struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
  return settimeofday(&tv, nullptr) == 0;
}

// Function that gets current epoch time without blocking.
unsigned long getEpochTime() {
  if (!isSystemTimeValid()) {
    return 0;
  }
  return (unsigned long)time(nullptr);
}

// Check if RTC has valid date/time
bool isRTCTimeValid(const I2C_BM8563_DateTypeDef &date, const I2C_BM8563_TimeTypeDef &time) {
  // Check reasonable year range (2020-2099)
  if (date.year < 2020 || date.year > 2099) {
    return false;
  }
  // Check month range
  if (date.month < 1 || date.month > 12) {
    return false;
  }
  // Check day range
  if (date.date < 1 || date.date > 31) {
    return false;
  }
  // Check hour range
  if (time.hours > 23) {
    return false;
  }
  // Check minute/second range
  if (time.minutes > 59 || time.seconds > 59) {
    return false;
  }
  // Weekday may be 0-6 or 1-7 depending on the RTC library.
  if (date.weekDay > 7) {
    return false;
  }

  // Validate the day of month against the month and leap year.
  static const uint8_t daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  uint8_t maxDay = daysInMonth[date.month - 1];
  bool leapYear = ((date.year % 4 == 0 && date.year % 100 != 0) || (date.year % 400 == 0));
  if (date.month == 2 && leapYear) {
    maxDay = 29;
  }
  if (date.date > maxDay) {
    return false;
  }

  return true;
}

// Get time from external RTC as fallback.
// The BM8563 holds UTC; the value is converted to local time for display so
// DST changes never corrupt the stored time.
bool getRTCTime(struct tm* timeinfo) {
  I2C_BM8563_DateTypeDef rtcDate;
  I2C_BM8563_TimeTypeDef rtcTime;

  // Get date and time from RTC (these are void functions)
  rtc.getDate(&rtcDate);
  rtc.getTime(&rtcTime);

  if (!isRTCTimeValid(rtcDate, rtcTime)) {
    return false;
  }

  struct tm utcTm = {};
  utcTm.tm_year  = rtcDate.year - 1900;
  utcTm.tm_mon   = rtcDate.month - 1;
  utcTm.tm_mday  = rtcDate.date;
  utcTm.tm_hour  = rtcTime.hours;
  utcTm.tm_min   = rtcTime.minutes;
  utcTm.tm_sec   = rtcTime.seconds;
  utcTm.tm_wday  = rtcDate.weekDay % 7;
  utcTm.tm_isdst = 0; // RTC values are UTC

  const time_t epoch = utcTmToEpoch(&utcTm);
  localtime_r(&epoch, timeinfo);
  return true;
}

// Cache of the last successfully read local time. If every time source fails
// temporarily the display keeps counting from this value instead of showing an
// error.
static struct tm      lastValidLocalTime = {};
static unsigned long  lastValidLocalMillis = 0;
static bool           haveLastValidLocalTime = false;

// Minimum interval between external RTC polls when the system clock is not
// valid. The RTC only has one-second resolution, so polling it on every display
// refresh bought nothing, and its blocking I2C reads stalled the animation.
static const unsigned long RTC_POLL_INTERVAL_MS = 60000;

static void cacheLocalTime(const struct tm* timeinfo) {
  lastValidLocalTime = *timeinfo;
  lastValidLocalMillis = millis();
  haveLastValidLocalTime = true;
}

// Enhanced time getter with RTC and cached-time fallbacks.
//
// This runs from the display refresh, so it must not block. Reading the
// external RTC over I2C is the only slow operation here, so it is polled at a
// limited rate and the cached time is advanced locally in between.
bool getTimeWithFallback(struct tm* timeinfo) {
  // Read the system clock directly instead of using getLocalTime().
  //
  // getLocalTime() is a polling loop that calls delay(10) while the clock is
  // not valid yet. Even with a zero timeout it therefore blocked ~10 ms every
  // second until NTP (or the boot-time RTC seed) had set the clock, which is
  // what made the animation hitch once per second at startup.
  if (isSystemTimeValid()) {
    const time_t now = time(nullptr);
    localtime_r(&now, timeinfo);
    cacheLocalTime(timeinfo);
    return true;
  }

  // Fallback to the external RTC, polled at most once per interval.
  static bool rtcPolled = false;
  static unsigned long lastRtcPollMs = 0;
  if (!rtcPolled || (millis() - lastRtcPollMs) >= RTC_POLL_INTERVAL_MS) {
    rtcPolled = true;
    lastRtcPollMs = millis();

    const unsigned long pollStartUs = micros();
    const bool rtcOk = getRTCTime(timeinfo);
    const unsigned long pollUs = micros() - pollStartUs;
    if (pollUs > 5000) {
      Serial.printf("[time] external RTC poll blocked for %lu us\n", pollUs);
    }

    if (rtcOk) {
      cacheLocalTime(timeinfo);
      return true;
    }
  }

  // Last resort: advance the cached time by the elapsed milliseconds so the
  // clock keeps running until a time source recovers.
  if (haveLastValidLocalTime) {
    struct tm cached = lastValidLocalTime;
    const unsigned long elapsedSec = (millis() - lastValidLocalMillis) / 1000UL;
    const time_t cachedEpoch = mktime(&cached);
    if (cachedEpoch > 0) {
      const time_t advanced = cachedEpoch + (time_t)elapsedSec;
      localtime_r(&advanced, timeinfo);
      return true;
    }
  }

  return false;
}

// Function declarations
void updateClockWithAnimation();
void updateClockOnly();
void updateClockOverlay();
void initBouncingSquares();
void updateBouncingSquares();

//
// Arduino Setup Task
//
void setup() {

  // Init Serial
  // if ARDUINO_USB_CDC_ON_BOOT is defined then the debug will go out via the USB port
  Serial.begin(115200);

  /*-------------------- START THE HUB75E DISPLAY --------------------*/
    
    // Module configuration
    HUB75_I2S_CFG mxconfig(
      PANEL_RES_X,   // module width
      PANEL_RES_Y,   // module height
      PANEL_CHAIN,   // Chain length
      _pins_x1       // pin mapping for port X1
    );
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_20M;  
    mxconfig.latch_blanking = 4;
    //mxconfig.clkphase = false;
    //mxconfig.driver = HUB75_I2S_CFG::FM6126A;
    //mxconfig.double_buff = false;  
    //mxconfig.min_refresh_rate = 30;


    // Display Setup. Check both the allocation and the panel initialisation
    // so a hardware or memory failure is reported instead of silently drawing
    // into an unusable panel.
    dma_display = new (std::nothrow) MatrixPanel_I2S_DMA(mxconfig);
    if (dma_display == nullptr) {
      Serial.println("Failed to allocate the HUB75 display object - halting");
      while (true) {
        delay(1000);
      }
    }
    if (!dma_display->begin()) {
      Serial.println("HUB75 panel initialisation failed - halting");
      while (true) {
        delay(1000);
      }
    }
    dma_display->setBrightness8(128); //0-255
    dma_display->clearScreen();

    dma_display->fillScreenRGB888(255,0,0);
    delay(1000);
    dma_display->fillScreenRGB888(0,255,0);
    delay(1000);    
    dma_display->fillScreenRGB888(0,0,255);
    delay(1000);       
    dma_display->clearScreen();
    dma_display->print("Connecting");     


  /*-------------------- START THE NETWORKING --------------------*/
  WiFi.mode(WIFI_STA);
  // Keep the radio awake so the web server stays responsive.
  WiFi.setSleep(false);
  // Credentials come from the git-ignored secrets.h.
  wifiMulti.addAP(WIFI_SSID, WIFI_PASS);

  // Wait for WiFi connection, but do not block forever. The clock can run
  // from the RTC even when the network is unavailable.
  Serial.print("Waiting for WiFi to connect...");
  unsigned long wifiStart = millis();
  while (wifiMulti.run() != WL_CONNECTED && (millis() - wifiStart) < 15000) {
    Serial.print(".");
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" connected");
  } else {
    Serial.println(" failed - continuing offline");
  }
    

  /*-------------------- --------------- --------------------*/
  //Increment boot number and print it every reboot
  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));

  //Print the wakeup reason for ESP32
  print_wakeup_reason();

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();    

  if ( wakeup_reason == ESP_SLEEP_WAKEUP_EXT0)
  {
    dma_display->setCursor(3,6);
    dma_display->print("Wake up!");
    delay(1000);
  }
  else
  {
    dma_display->clearScreen();
    dma_display->setCursor(0, 0);
    dma_display->print("Starting.");
  }


  /*
    We set our ESP32 to wake up for an external trigger.
    There are two types for ESP32, ext0 and ext1 .

    NOTE: EXT0 wake is LEVEL triggered, not edge triggered. Arming it for LOW
    means the chip wakes as soon as the pin is LOW during deep sleep, so
    enterDeepSleep() waits for the button to be released before sleeping.
  */
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PUSH_BUTTON_PIN, 0); //1 = High, 0 = Low

  /*-------------------- --------------- --------------------*/
  // BUTTON SETUP
  // INPUT_PULLUP is safe even when the board has an external pull-up.
  button.attach( PUSH_BUTTON_PIN, INPUT_PULLUP );
  button.interval(5);   // DEBOUNCE INTERVAL IN MILLISECONDS
  button.setPressedState(LOW); // INDICATE THAT THE LOW STATE CORRESPONDS TO PHYSICALLY PRESSING THE BUTTON

  // If the button is already held at boot (for example, when waking from
  // deep sleep, where the wake is caused by the press itself), ignore it until
  // it has been released.
  //
  // A single update() immediately after attach() is not enough: Bounce2 must
  // observe a stable reading for the full debounce interval before its state
  // is trustworthy, so keep updating for longer than that interval.
  Serial.printf("[boot] millis=%lu bootCount=%d digitalRead(button)=%d\n",
                (unsigned long)millis(), bootCount, digitalRead(PUSH_BUTTON_PIN));
  const unsigned long bootSampleStart = millis();
  while (millis() - bootSampleStart < 50) {
    button.update();
    delay(5);
  }
  Serial.printf("[boot] after settle : isPressed=%d read()=%d\n",
                (int)button.isPressed(), (int)button.read());
  if (button.isPressed()) {
    buttonPressHandled = true;
    buttonPressStartTime = millis();
    Serial.printf("[boot] button already held - press ignored until released"
                  " (start=%lu)\n", buttonPressStartTime);
  } else {
    Serial.println("[boot] button not held at boot");
  }


  /*-------------------- LEDC Controller --------------------*/
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .duty_resolution  = LEDC_TIMER_13_BIT ,
        .timer_num        = LEDC_TIMER_0,
        .freq_hz          = 4000,  // Set output frequency at 4 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .gpio_num       = RUN_LED_PIN,
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = LEDC_TIMER_0,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));  


    // Start fading that LED
    xTaskCreatePinnedToCore(
      ledFadeTask,            /* Task function. */
      "ledFadeTask",                 /* name of task. */
      2048,                    /* Stack size of task */
      NULL,                     /* parameter of the task */
      1,                        /* priority of the task */
      &Task1,                   /* Task handle to keep track of created task */
      0);                       /* Core */   
    

  /*-------------------- INIT LITTLE FS --------------------*/
  if(!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)){
      Serial.println("LittleFS Mount Failed");
      return;
  }
  listDir(LittleFS, "/", 1);    
 
  /*-------------------- --------------- --------------------*/
  // Init I2C for RTC
  Wire1.begin(BM8563_I2C_SDA, BM8563_I2C_SCL);
  rtc.begin(); // returns void, so probe the bus for the chip instead

  // Confirm the BM8563 actually answers on the I2C bus.
  Wire1.beginTransmission(I2C_BM8563_DEFAULT_ADDRESS);
  const bool rtcPresent = (Wire1.endTransmission() == 0);
  if (!rtcPresent) {
    Serial.println("BM8563 RTC not detected on the I2C bus");
  }

  // Configure the timezone and start SNTP. This sets the TZ environment
  // variable so that getLocalTime() and mktime() use local time.
  configTzTime(clockTz, ntpServer);

  // Get RTC date and time
  I2C_BM8563_DateTypeDef rtcDate;
  I2C_BM8563_TimeTypeDef rtcTime;
  
  // Try to read RTC - these functions return void, so we can't check their return values
  rtc.getDate(&rtcDate);
  rtc.getTime(&rtcTime);
  
  // Validate RTC data (and that the chip is actually present).
  bool rtcDataValid = rtcPresent && isRTCTimeValid(rtcDate, rtcTime);
  
  if (rtcDataValid) {
    Serial.printf("RTC Date: %04d-%02d-%02d %02d:%02d:%02d\n", 
                  rtcDate.year, rtcDate.month, rtcDate.date,
                  rtcTime.hours, rtcTime.minutes, rtcTime.seconds);
  } else {
    Serial.println("RTC data is invalid or corrupted");
  }
  
  time_t ntp_last_update_ts = 0;
  // Explicitly pass create = false: opening the timestamp file for reading
  // must not create it, otherwise the missing-file branch is never taken.
  File file = LittleFS.open(ntpLastUpdate, FILE_READ, false);
  if (!file) {
    Serial.println("NTP last update file not found - will perform NTP sync");
  } else {
    if (file.read((uint8_t*)&ntp_last_update_ts, sizeof(ntp_last_update_ts)) != sizeof(ntp_last_update_ts)) {
      ntp_last_update_ts = 0;
    }
    Serial.print("NTP last update epoch: ");
    Serial.println((long)ntp_last_update_ts);
    file.close();
  }

  // Current RTC time (include time components for better comparison)
  time_t curr_rtc_ts = 0;
  bool needNTPUpdate = true;
  
  if (rtcDataValid) {
    struct tm curr_rtc_tm = {};
    curr_rtc_tm.tm_year = rtcDate.year - 1900;  // years since 1900
    curr_rtc_tm.tm_mon = rtcDate.month - 1;     // months since January (0-11)
    curr_rtc_tm.tm_mday = rtcDate.date;         // day of the month (1-31)
    curr_rtc_tm.tm_hour = rtcTime.hours;        // hours since midnight (0-23)
    curr_rtc_tm.tm_min = rtcTime.minutes;       // minutes after the hour (0-59)
    curr_rtc_tm.tm_sec = rtcTime.seconds;       // seconds after the minute (0-59)
    curr_rtc_tm.tm_isdst = 0;                   // the RTC stores UTC

    // Convert as UTC so the freshness check does not depend on the local
    // timezone. Use 64-bit math for the difference.
    curr_rtc_ts = utcTmToEpoch(&curr_rtc_tm);

    // Check if we need NTP update (more than 7 days old, or no previous NTP sync)
    if (ntp_last_update_ts > 0) {
      long long timeDiff = llabs((long long)curr_rtc_ts - (long long)ntp_last_update_ts);
      needNTPUpdate = (timeDiff > (60LL * 60LL * 24LL * 7LL)); // 7 days
      Serial.printf("Time difference: %lld seconds (%lld days)\n",
                    timeDiff, timeDiff / (60LL * 60LL * 24LL));
    }
  }

  if (!rtcDataValid || needNTPUpdate)
  {
      Serial.println("Performing NTP time synchronization...");
      ESP_LOGI("ntp_update", "Updating time from NTP server");    
  
      // Wait for SNTP to complete, with a timeout. Do not use getLocalTime()
      // here because it can block and because the system clock may already be
      // valid from a previous sync.
      int ntpRetries = 0;
      while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && ntpRetries < 150) {
        delay(100);
        ntpRetries++;
        if ((ntpRetries % 10) == 0) {
          Serial.print(".");
        }
      }

      struct tm timeInfo;
      if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED && getLocalTime(&timeInfo, 0)) {
        Serial.println("\nNTP sync successful");

        // Keep the RTC in UTC. Storing local time would make the clock wrong
        // for an hour after every DST transition until the next NTP sync.
        const time_t nowEpoch = time(nullptr);
        struct tm utcTime = {};
        gmtime_r(&nowEpoch, &utcTime);

        // Set RTC time - setTime returns void, so we can't check its return value
        I2C_BM8563_TimeTypeDef timeStruct;
        timeStruct.hours   = utcTime.tm_hour;
        timeStruct.minutes = utcTime.tm_min;
        timeStruct.seconds = utcTime.tm_sec;

        rtc.setTime(&timeStruct);
        Serial.println("RTC time set successfully");

        // Set RTC Date - setDate returns void, so we can't check its return value
        I2C_BM8563_DateTypeDef dateStruct;
        dateStruct.weekDay = utcTime.tm_wday;
        dateStruct.month   = utcTime.tm_mon + 1;
        dateStruct.date    = utcTime.tm_mday;
        dateStruct.year    = utcTime.tm_year + 1900;

        rtc.setDate(&dateStruct);
        Serial.printf("RTC updated (UTC) to: %04d-%02d-%02d %02d:%02d:%02d\n",
                      dateStruct.year, dateStruct.month, dateStruct.date,
                      timeStruct.hours, timeStruct.minutes, timeStruct.seconds);
        Serial.printf("Local time is: %04d-%02d-%02d %02d:%02d:%02d\n",
                      timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday,
                      timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);

        // Keep the cached RTC values in sync with what is now stored (UTC).
        rtcDate = dateStruct;
        rtcTime = timeStruct;
        rtcDataValid = true;
      } else {
        Serial.println("\nNTP sync failed - will use existing RTC time if valid");
      }

      // Save NTP update timestamp
      ntp_last_update_ts = getEpochTime();
      if (ntp_last_update_ts > 0) {
        File tsFile = LittleFS.open(ntpLastUpdate, FILE_WRITE);
        if (!tsFile) {
          Serial.println("Failed to open NTP timestamp file for writing");
        } else {
          tsFile.write((uint8_t*)&ntp_last_update_ts, sizeof(ntp_last_update_ts));
          tsFile.close();
          Serial.print("Saved NTP update timestamp: ");
          Serial.println((long)ntp_last_update_ts);
        }
      }

  }
  
  // If the system clock is still not valid (NTP skipped or failed), seed it
  // from the external RTC so that getLocalTime() works without blocking.
  if (rtcDataValid && !isSystemTimeValid()) {
    struct tm rtcTm = {};
    rtcTm.tm_year = rtcDate.year - 1900;
    rtcTm.tm_mon  = rtcDate.month - 1;
    rtcTm.tm_mday = rtcDate.date;
    rtcTm.tm_hour = rtcTime.hours;
    rtcTm.tm_min  = rtcTime.minutes;
    rtcTm.tm_sec  = rtcTime.seconds;
    rtcTm.tm_isdst = 0; // the RTC stores UTC
    if (setSystemTimeFromEpoch(utcTmToEpoch(&rtcTm))) {
      Serial.println("System clock seeded from external RTC (UTC)");
    } else {
      Serial.println("Failed to seed system clock from external RTC");
    }
  }

  // Report the local time currently in use (the system clock is the single
  // source of truth; no separate ESP32Time instance is maintained).
  if (isSystemTimeValid()) {
    const time_t nowEpoch = time(nullptr);
    struct tm localTm = {};
    localtime_r(&nowEpoch, &localTm);
    char timeStr[40];
    strftime(timeStr, sizeof(timeStr), "%A, %B %d %Y %H:%M:%S", &localTm);
    Serial.printf("Local time in use: %s\n", timeStr);
  } else {
    Serial.println("Warning: No valid time source available!");
  }

   /*-------------------- --------------- --------------------*/

    webServer.on("/", []() {
      webServer.send(200, "text/plain", "Hi! I am here.");
    });

    //ElegantOTA.begin(&webServer);    // Start ElegantOTA (see README)
    webServer.begin();
    Serial.println("Web server started");

    // Advertise the device as ledclock.local on the local network.
    if (MDNS.begin("ledclock")) {
      MDNS.addService("http", "tcp", 80);
      Serial.println("mDNS started: http://ledclock.local/");
    } else {
      Serial.println("mDNS failed to start");
    }

    /*-------------------- --------------- --------------------*/
    const bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    Serial.print("IP address: ");
    Serial.println(wifiConnected ? WiFi.localIP().toString() : String("offline"));

    // Show the address (or an offline notice) and only clear it afterwards.
    dma_display->clearScreen();
    dma_display->setCursor(0, 0);
    dma_display->setTextColor(dma_display->color565(255, 255, 255));
    if (wifiConnected) {
      dma_display->print(WiFi.localIP());
    } else {
      dma_display->print("Offline");
    }
    delay(3000);
    dma_display->clearScreen();

    // Initialize bouncing squares for animation mode
    initBouncingSquares();

}

unsigned long last_update = 0;
char buffer[64];

void loop()
{
    // YOU MUST CALL THIS EVERY LOOP
    button.update();

    // Handle button press logic.
    //
    // The flag is cleared on every new press and re-armed on every release so
    // that the button works repeatedly. A button that is already held when the
    // device boots (e.g. waking from deep sleep) is marked as handled in
    // setup(), so it cannot immediately trigger another long press/sleep until
    // it has been released.
    if (button.pressed()) {
        buttonPressStartTime = millis();
        buttonPressHandled = false;
        Serial.printf("[btn] pressed at millis=%lu (handled reset)\n",
                      (unsigned long)millis());
    }

    // Long press: enter deep sleep.
    if (button.isPressed() && !buttonPressHandled &&
        (millis() - buttonPressStartTime >= LONG_PRESS_MS)) {
        buttonPressHandled = true;
        Serial.printf("[btn] long press at millis=%lu (held %lu ms), pin=%d"
                      " - blanking display and waiting for release before sleeping\n",
                      (unsigned long)millis(),
                      (unsigned long)(millis() - buttonPressStartTime),
                      digitalRead(PUSH_BUTTON_PIN));
        enterDeepSleep();
    }

    if (button.released()) {
        Serial.printf("[btn] released at millis=%lu (buttonPressHandled=%d)"
                      " -> %s\n",
                      (unsigned long)millis(), (int)buttonPressHandled,
                      buttonPressHandled ? "long press already handled"
                                         : "treated as SHORT press (mode change)");
        if (!buttonPressHandled) {
            // Short press: cycle display mode
            buttonPressHandled = true;
            currentDisplayMode = (DisplayMode)((currentDisplayMode + 1) % MODE_COUNT);
            Serial.print("Switched to display mode: ");
            Serial.println(currentDisplayMode);

            // Clear screen when switching modes
            dma_display->clearScreen();
            last_update = 0;
            lastOverlayUpdate = 0;

            // If switching to bouncing squares, reinitialize them
            if (currentDisplayMode == MODE_BOUNCING_SQUARES) {
                initBouncingSquares();
            }
        }

        // Re-arm the state machine for the next press.
        buttonPressHandled = false;
    }

    // Periodically try to reconnect if the network drops.
    static unsigned long lastWifiCheck = 0;
    if (millis() - lastWifiCheck > 10000) {
        lastWifiCheck = millis();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi disconnected - reconnecting...");
            wifiMulti.run();
        }
    }

    webServer.handleClient();
    delay(1);

    // Update display based on current mode
    switch (currentDisplayMode) {
        case MODE_CLOCK_WITH_ANIMATION:
            updateClockWithAnimation();
            break;
            
        case MODE_CLOCK_ONLY:
            updateClockOnly();
            break;
            
        case MODE_BOUNCING_SQUARES:
            updateBouncingSquares();
            updateClockOverlay(); // Still show time on bouncing squares
            break;
    }
}

// Clock update with animated background (original behavior)
void updateClockWithAnimation() {
    // Update time display every second
    if ((millis() - last_update) > 1000) {
        struct tm timeinfo;
        if (getTimeWithFallback(&timeinfo)) {
            memset(buffer, 0, 64);
            snprintf(buffer, 64, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

            dma_display->fillRect(0, 9, PANEL_RES_X * PANEL_CHAIN, 9, 0x00); // wipe the section of the screen just used for the time
            dma_display->setCursor(8, 10);
            dma_display->setTextColor(dma_display->color565(255, 255, 255));
            dma_display->print(buffer);
        } else {
            // Rate-limited: a blocking USB CDC write every second on the display
            // refresh path causes a visible hitch.
            static unsigned long lastNoTimeLogMs = 0;
            if ((millis() - lastNoTimeLogMs) >= RTC_POLL_INTERVAL_MS) {
                lastNoTimeLogMs = millis();
                Serial.println("Failed to get time from all sources.");
            }
            // Display error indicator
            dma_display->fillRect(0, 9, PANEL_RES_X, 9, 0x00);
            dma_display->setCursor(8, 10);
            dma_display->setTextColor(dma_display->color565(255, 0, 0)); // Red for error
            dma_display->print("NO TIME");
        }
        last_update = millis();
    }

    // Canvas loop (original animation)
    float t = (float)((millis() % 4000) / 4000.f);
    float tt = (float)((millis() % 16000) / 16000.f);

    for (int x = 0; x < (PANEL_RES_X * PANEL_CHAIN); x++) {
        // calculate the overal shade
        float f = (((sin(tt - (float)x / PANEL_RES_Y / 32.) * 2.f * PI) + 1) / 2) * 255;
        // calculate hue spectrum into rgb
        float r = max(min(cosf(2.f * PI * (t + ((float)x / PANEL_RES_Y + 0.f) / 3.f)) + 0.5f, 1.f), 0.f);
        float g = max(min(cosf(2.f * PI * (t + ((float)x / PANEL_RES_Y + 1.f) / 3.f)) + 0.5f, 1.f), 0.f);
        float b = max(min(cosf(2.f * PI * (t + ((float)x / PANEL_RES_Y + 2.f) / 3.f)) + 0.5f, 1.f), 0.f);

        // iterate pixels for every row
        for (int y = 0; y < PANEL_RES_Y; y++) {
            // Keep this bit clear for the clock
            if (y > 8 && y < 18) {
                continue; // leave a black bar for the time, don't touch, this part of display is updated by the code in the clock update bit above
            }

            if (y * 2 < PANEL_RES_Y) {
                // top-middle part of screen, transition of value
                float t = (2.f * y + 1) / PANEL_RES_Y;
                dma_display->drawPixelRGB888(x, y,
                    (r * t) * f,
                    (g * t) * f,
                    (b * t) * f);
            } else {
                // middle to bottom of screen, transition of saturation
                float t = (2.f * (PANEL_RES_Y - y) - 1) / PANEL_RES_Y;
                dma_display->drawPixelRGB888(x, y,
                    (r * t + 1 - t) * f,
                    (g * t + 1 - t) * f,
                    (b * t + 1 - t) * f);
            }
        }
    }
}

// Clock only mode (no animation background)
void updateClockOnly() {
    static bool lastValid = false;

    if ((millis() - last_update) > 1000) {
        struct tm timeinfo;
        bool valid = getTimeWithFallback(&timeinfo);

        if (valid) {
            // Clear only when recovering from an error, to avoid flicker.
            if (!lastValid) {
                dma_display->clearScreen();
            }

            // Display time
            snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            dma_display->setCursor(8, 4);
            dma_display->setTextColor(dma_display->color565(255, 255, 255));
            dma_display->print(buffer);

            // Display date
            snprintf(buffer, sizeof(buffer), "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
            dma_display->setCursor(2, 16);
            dma_display->setTextColor(dma_display->color565(200, 200, 200));
            dma_display->print(buffer);

            // Display day of week
            const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
            int wday = timeinfo.tm_wday % 7;
            dma_display->setCursor(2, 24);
            dma_display->setTextColor(dma_display->color565(150, 150, 255));
            dma_display->print(days[wday]);

            // No per-second Serial output here: a blocking USB CDC write on the
            // display refresh path causes a visible hitch.
        } else {
            static unsigned long lastNoTimeLogMs = 0;
            if ((millis() - lastNoTimeLogMs) >= RTC_POLL_INTERVAL_MS) {
                lastNoTimeLogMs = millis();
                Serial.println("Failed to get time from all sources.");
            }
            dma_display->clearScreen();
            dma_display->setCursor(8, 10);
            dma_display->setTextColor(dma_display->color565(255, 0, 0));
            dma_display->print("NO TIME");
        }

        lastValid = valid;
        last_update = millis();
    }
}

// Update clock overlay for bouncing squares mode
void updateClockOverlay() {
    // Refresh the cached time string once per second.
    if ((millis() - lastOverlayUpdate) > 1000) {
        struct tm timeinfo;
        if (getTimeWithFallback(&timeinfo)) {
            snprintf(overlayBuffer, sizeof(overlayBuffer), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
            overlayValid = true;
        } else {
            snprintf(overlayBuffer, sizeof(overlayBuffer), "ERR");
            overlayValid = false;
        }
        lastOverlayUpdate = millis();
    }

    // Redraw every frame so the bouncing-squares clear does not erase it.
    dma_display->fillRect(17, 12, 30, 8, 0x0000);
    dma_display->setCursor(17, 12);
    dma_display->setTextColor(overlayValid ? dma_display->color565(255, 255, 255)
                                           : dma_display->color565(255, 0, 0));
    dma_display->print(overlayBuffer);
}