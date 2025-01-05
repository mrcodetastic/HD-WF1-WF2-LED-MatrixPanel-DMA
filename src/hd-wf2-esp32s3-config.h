// -------------------------------------------
// WF2 GPIO Configuration
// Note: Whilst the WF-2 has two HUB75 ('75EX1' and '75EX2') connectors. It is NOT possible to drive BOTH at once. 
//       You can only use wither the 75EX1 or 75EX1 port at any time to drive a single HUB75 panel, or a chain of panels.

#define RUN_LED_PIN       40
#define PUSH_BUTTON_PIN   11

// I2C RTC BM8563 I2C port
#define BM8563_I2C_SDA    41
#define BM8563_I2C_SCL    42

// HUB75 Pins
#define WF2_X1_R1_PIN 2
#define WF2_X1_R2_PIN 3
#define WF2_X1_G1_PIN 6
#define WF2_X1_G2_PIN 7
#define WF2_X1_B1_PIN 10
#define WF2_X1_B2_PIN 11
#define WF2_X1_E_PIN 21

#define WF2_X2_R1_PIN 4
#define WF2_X2_R2_PIN 5
#define WF2_X2_G1_PIN 8
#define WF2_X2_G2_PIN 9
#define WF2_X2_B1_PIN 12
#define WF2_X2_B2_PIN 13
#define WF2_X2_E_PIN -1        // Currently unknown, so X2 port will not work (yet) with 1/32 scan panels

#define WF2_A_PIN 39
#define WF2_B_PIN 38
#define WF2_C_PIN 37
#define WF2_D_PIN 36
#define WF2_OE_PIN 35
#define WF2_CLK_PIN 34
#define WF2_LAT_PIN 33

// USB
#define WF2_USB_DM_PIN 19
#define WF2_USB_DP_PIN 20


