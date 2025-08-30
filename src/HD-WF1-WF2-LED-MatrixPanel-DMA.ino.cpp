// Custom LED Matrix Firmware (leveraging the HUB75 DMA library) for the Huidu HUB75 Series Control Cards.
// Example shop link: https://www.aliexpress.com/item/1005005038544582.html -> WF1
// Example shop link: https://www.aliexpress.com/item/1005002271988988.html -> WF2

#if defined(WF1)
  #include "hd-wf1-esp32s2-config.h"
#elif defined(WF2)
  #include "hd-wf2-esp32s3-config.h"
#else
  #error "Please define either WF1 or WF2"
#endif  


#include <esp_err.h>
#include <esp_log.h>
#include "debug.h"
#include "littlefs_core.h"
#include <ctime>
#include "driver/ledc.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>

#include <WebServer.h>
#include <ESPmDNS.h>
#include <I2C_BM8563.h>   // https://github.com/tanakamasayuki/I2C_BM8563

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
//#include <ElegantOTA.h> // upload firmware by going to http://<ipaddress>/update

#include <ESP32Time.h>
#include <Bounce2.h>

#define fs LittleFS

#ifndef PI
#define PI 3.14159265358979323846
#endif

/*----------------------------- Wifi Configuration -------------------------------*/

const char *wifi_ssid = "xxxx";
const char *wifi_pass = "yyyy";

/*----------------------------- RTC and NTP -------------------------------*/

I2C_BM8563 rtc(I2C_BM8563_DEFAULT_ADDRESS, Wire1);
const char* ntpServer         = "time.cloudflare.com";
const char* ntpLastUpdate     = "/ntp_last_update.txt";

// NTP Clock Offset / Timezone
#define CLOCK_GMT_OFFSET 1

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
ESP32Time           esp32rtc;  // offset in seconds GMT+1
MatrixPanel_I2S_DMA *dma_display = nullptr;

// INSTANTIATE A Button OBJECT FROM THE Bounce2 NAMESPACE
Bounce2::Button button = Bounce2::Button();

// ROS Task management
TaskHandle_t Task1;
TaskHandle_t Task2;

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
volatile bool buttonPressed = false;

// Bouncing squares animation variables
struct BouncingSquare {
  float x, y;
  float vx, vy;
  uint16_t color;
  int size;
};

const int NUM_SQUARES = 3;
BouncingSquare squares[NUM_SQUARES];

IRAM_ATTR void toggleButtonPressed() {
  // This function will be called when the interrupt occurs on pin PUSH_BUTTON_PIN
  buttonPressed = true;
  ESP_LOGI("toggleButtonPressed", "Interrupt Triggered.");
}

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


// Function that gets current epoch time
unsigned long getEpochTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    //Serial.println("Failed to obtain time");
    return(0);
  }
  time(&now);
  return now;
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
  return true;
}

// Get time from external RTC as fallback
bool getRTCTime(struct tm* timeinfo) {
  I2C_BM8563_DateTypeDef rtcDate;
  I2C_BM8563_TimeTypeDef rtcTime;
  
  // Get date and time from RTC (these are void functions)
  rtc.getDate(&rtcDate);
  rtc.getTime(&rtcTime);
  
  if (!isRTCTimeValid(rtcDate, rtcTime)) {
    return false;
  }
  
  timeinfo->tm_year = rtcDate.year - 1900;
  timeinfo->tm_mon = rtcDate.month - 1;
  timeinfo->tm_mday = rtcDate.date;
  timeinfo->tm_hour = rtcTime.hours;
  timeinfo->tm_min = rtcTime.minutes;
  timeinfo->tm_sec = rtcTime.seconds;
  timeinfo->tm_wday = rtcDate.weekDay;
  timeinfo->tm_isdst = -1;
  
  return true;
}

// Enhanced time getter with RTC fallback
bool getTimeWithFallback(struct tm* timeinfo) {
  // Try ESP32 internal RTC first
  if (getLocalTime(timeinfo)) {
    return true;
  }
  
  // Fallback to external RTC
  Serial.println("ESP32 RTC failed, trying external RTC...");
  return getRTCTime(timeinfo);
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


    // Display Setup
    dma_display = new MatrixPanel_I2S_DMA(mxconfig);
    dma_display->begin();
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
  wifiMulti.addAP(wifi_ssid, wifi_pass); // configure in the *-config.h file

  // wait for WiFi connection
  Serial.print("Waiting for WiFi to connect...");
  while (wifiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
  }
  Serial.println(" connected");
    

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
    dma_display->print("Starting.");

  }


  /*
    We set our ESP32 to wake up for an external trigger.
    There are two types for ESP32, ext0 and ext1 .
  */
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PUSH_BUTTON_PIN, 0); //1 = High, 0 = Low  

  /*-------------------- --------------- --------------------*/
  // BUTTON SETUP 
  button.attach( PUSH_BUTTON_PIN, INPUT ); // USE EXTERNAL PULL-UP
  button.interval(5);   // DEBOUNCE INTERVAL IN MILLISECONDS
  button.setPressedState(LOW); // INDICATE THAT THE LOW STATE CORRESPONDS TO PHYSICALLY PRESSING THE BUTTON


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
      1000,                    /* Stack size of task */
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
  rtc.begin();

  // Get RTC date and time
  I2C_BM8563_DateTypeDef rtcDate;
  I2C_BM8563_TimeTypeDef rtcTime;
  
  // Try to read RTC - these functions return void, so we can't check their return values
  rtc.getDate(&rtcDate);
  rtc.getTime(&rtcTime);
  
  // Validate RTC data
  bool rtcDataValid = isRTCTimeValid(rtcDate, rtcTime);
  
  if (rtcDataValid) {
    Serial.printf("RTC Date: %04d-%02d-%02d %02d:%02d:%02d\n", 
                  rtcDate.year, rtcDate.month, rtcDate.date,
                  rtcTime.hours, rtcTime.minutes, rtcTime.seconds);
  } else {
    Serial.println("RTC data is invalid or corrupted");
  }
  
  time_t ntp_last_update_ts = 0;
  File file = fs.open(ntpLastUpdate, FILE_READ, true);
  if(!file) {
      Serial.println("NTP last update file not found - will perform NTP sync");
  } else  {
      file.read( (uint8_t*) &ntp_last_update_ts, sizeof(ntp_last_update_ts));          
      Serial.print("NTP last update epoch: ");
      Serial.println(ntp_last_update_ts);
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
    curr_rtc_tm.tm_isdst = -1;                  // daylight saving time flag
    
    curr_rtc_ts = mktime(&curr_rtc_tm);
    
    // Check if we need NTP update (more than 7 days old, or no previous NTP sync)
    if (ntp_last_update_ts > 0) {
      long timeDiff = abs((long int)(curr_rtc_ts - ntp_last_update_ts));
      needNTPUpdate = (timeDiff > (60*60*24*7)); // 7 days instead of 30
      Serial.printf("Time difference: %ld seconds (%ld days)\n", timeDiff, timeDiff/(60*60*24));
    }
  }

  if (!rtcDataValid || needNTPUpdate)
  {
      Serial.println("Performing NTP time synchronization...");
      ESP_LOGI("ntp_update", "Updating time from NTP server");    
  
      // Set ntp time to local
      configTime(CLOCK_GMT_OFFSET * 3600, 0, ntpServer);

      // Wait for NTP sync with timeout
      int ntpRetries = 0;
      struct tm timeInfo;
      while (!getLocalTime(&timeInfo) && ntpRetries < 10) {
        delay(1000);
        ntpRetries++;
        Serial.print(".");
      }
      
      if (getLocalTime(&timeInfo)) {
        Serial.println("\nNTP sync successful");
        
        // Set RTC time - setTime returns void, so we can't check its return value
        I2C_BM8563_TimeTypeDef timeStruct;
        timeStruct.hours   = timeInfo.tm_hour;
        timeStruct.minutes = timeInfo.tm_min;
        timeStruct.seconds = timeInfo.tm_sec;
        
        rtc.setTime(&timeStruct);
        Serial.println("RTC time set successfully");

        // Set RTC Date - setDate returns void, so we can't check its return value
        I2C_BM8563_DateTypeDef dateStruct;
        dateStruct.weekDay = timeInfo.tm_wday;
        dateStruct.month   = timeInfo.tm_mon + 1;
        dateStruct.date    = timeInfo.tm_mday;
        dateStruct.year    = timeInfo.tm_year + 1900;
        
        rtc.setDate(&dateStruct);
        Serial.printf("RTC updated to: %04d-%02d-%02d %02d:%02d:%02d\n",
                      dateStruct.year, dateStruct.month, dateStruct.date,
                      timeStruct.hours, timeStruct.minutes, timeStruct.seconds);
        
        // Update local variables with new time
        rtcDate = dateStruct;
        rtcTime = timeStruct;
        rtcDataValid = true;
      } else {
        Serial.println("\nNTP sync failed - will use existing RTC time if valid");
      }

      // Save NTP update timestamp
      ntp_last_update_ts = getEpochTime();
      if (ntp_last_update_ts > 0) {
        File file = fs.open(ntpLastUpdate, FILE_WRITE);
        if(!file) {
            Serial.println("Failed to open NTP timestamp file for writing");
        } else  {
            file.write( (uint8_t*) &ntp_last_update_ts, sizeof(ntp_last_update_ts));          
            file.close();      
            Serial.print("Saved NTP update timestamp: "); 
            Serial.println(ntp_last_update_ts, DEC);            
        }
      }

  }
  
  // Update ESP32 internal RTC if we have valid external RTC data
  if (rtcDataValid) {
    esp32rtc.setTime(rtcTime.seconds, rtcTime.minutes, rtcTime.hours, 
                     rtcDate.date, rtcDate.month, rtcDate.year);
    Serial.println("ESP32 internal RTC updated from external RTC:");    
    Serial.println(esp32rtc.getTime("%A, %B %d %Y %H:%M:%S"));
  } else {
    Serial.println("Warning: No valid time source available!");
  }

   /*-------------------- --------------- --------------------*/

    webServer.on("/", []() {
      webServer.send(200, "text/plain", "Hi! I am here.");
    });

    //ElegantOTA.begin(&webServer);    // Start ElegantOTA
    webServer.begin();
    Serial.println("OTA HTTP server started");

    /*-------------------- --------------- --------------------*/
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());  

    delay(1000);
    
    dma_display->clearScreen();
    dma_display->setCursor(0,0);

    dma_display->print(WiFi.localIP());
    dma_display->clearScreen();
    delay(3000);

    // Initialize bouncing squares for animation mode
    initBouncingSquares();

}

unsigned long last_update = 0;
char buffer[64];
void loop() 
{
    // YOU MUST CALL THIS EVERY LOOP
    button.update();

    // Handle button press logic
    if (button.pressed() && !buttonPressHandled) {
        buttonPressStartTime = millis();
        buttonPressHandled = false;
        Serial.println("Button pressed");
    }
    
    if (button.isPressed() && !buttonPressHandled) {
        // Check if button has been held for more than 2 seconds
        if (millis() - buttonPressStartTime > 2000) {
            Serial.println("Button held for >2s - going to sleep");
            esp_deep_sleep_start();
            buttonPressHandled = true;
        }
    }
    
    if (button.released() && !buttonPressHandled) {
        // Button was released before 2 seconds - cycle display mode
        if (millis() - buttonPressStartTime < 2000) {
            currentDisplayMode = (DisplayMode)((currentDisplayMode + 1) % MODE_COUNT);
            Serial.print("Switched to display mode: ");
            Serial.println(currentDisplayMode);
            
            // Clear screen when switching modes
            dma_display->clearScreen();
            
            // If switching to bouncing squares, reinitialize them
            if (currentDisplayMode == MODE_BOUNCING_SQUARES) {
                initBouncingSquares();
            }
        }
        buttonPressHandled = true;
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

            Serial.println("Performing screen time update...");
            dma_display->fillRect(0, 9, PANEL_RES_X, 9, 0x00); // wipe the section of the screen just used for the time
            dma_display->setCursor(8, 10);
            dma_display->print(buffer);
        } else {
            Serial.println("Failed to get time from all sources.");
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
    if ((millis() - last_update) > 1000) {
        struct tm timeinfo;
        if (getTimeWithFallback(&timeinfo)) {
            dma_display->clearScreen();
            
            // Display time
            memset(buffer, 0, 64);
            snprintf(buffer, 64, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            dma_display->setCursor(8, 4);
            dma_display->setTextColor(dma_display->color565(255, 255, 255));
            dma_display->print(buffer);
            
            // Display date
            memset(buffer, 0, 64);
            snprintf(buffer, 64, "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
            dma_display->setCursor(2, 16);
            dma_display->setTextColor(dma_display->color565(200, 200, 200));
            dma_display->print(buffer);
            
            // Display day of week
            const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
            dma_display->setCursor(2, 26);
            dma_display->setTextColor(dma_display->color565(150, 150, 255));
            dma_display->print(days[timeinfo.tm_wday]);

            Serial.println("Clock-only mode update");
        } else {
            Serial.println("Failed to get time from all sources.");
            dma_display->clearScreen();
            dma_display->setCursor(8, 10);
            dma_display->setTextColor(dma_display->color565(255, 0, 0));
            dma_display->print("NO TIME");
        }
        last_update = millis();
    }
}

// Update clock overlay for bouncing squares mode
void updateClockOverlay() {
    if ((millis() - last_update) > 1000) {
        struct tm timeinfo;
        if (getTimeWithFallback(&timeinfo)) {
            memset(buffer, 0, 64);
            snprintf(buffer, 64, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
            
            // Draw time with black background for readability
            dma_display->fillRect(18, 12, 28, 8, 0x0000);
            dma_display->setCursor(20, 14);
            dma_display->setTextColor(dma_display->color565(255, 255, 255));
            dma_display->print(buffer);

            Serial.println("Bouncing squares with clock overlay update");
        } else {
            Serial.println("Failed to get time from all sources.");
            // Show error in overlay
            dma_display->fillRect(18, 12, 28, 8, 0x0000);
            dma_display->setCursor(20, 14);
            dma_display->setTextColor(dma_display->color565(255, 0, 0));
            dma_display->print("ERR");
        }
        last_update = millis();
    }
} 