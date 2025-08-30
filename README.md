# HD-WF1-WF2-LED-MatrixPanel-DMA
Custom firmware based on my [HUB75 DMA library](https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-DMA),  for the:

* Huidu WF1 LED Controller Card based on the ESP32-S2; or
* Huidu WF2 LED Controller Card based on the ESP32-S3.

## Features
- **Multiple Display Modes**: Three different visual modes accessible via button control
- **Robust Real-Time Clock**: Enhanced RTC handling with NTP synchronization, data validation, and automatic fallback
- **Deep Sleep Support**: Power-saving sleep mode with button wake-up
- **WiFi Connectivity**: OTA firmware updates via web interface
- **Debounced Button Control**: Reliable button handling with short/long press detection
- **Animated Graphics**: Colorful animations and bouncing effects

## Huidu WF1
These can be bought for about USD $5 and are quite good as they come with a battery and a Real Time Clock (BM8563), and a push button connected to GPIO 11 for your use.
 
Seller #1: [ https://www.aliexpress.com/item/1005005038544582.html ](https://www.aliexpress.com/item/1005006075952980.html)

Seller #2: (https://www.aliexpress.com/item/1005005556957598.html) 

There are probably other sellers on Aliexpress as well. I give no guarantees that these will use the ESP32-S2, check the model version!

![image](https://github.com/mrfaptastic/HD-WF1-LED-MatrixPanel-DMA/assets/12006953/ccdff75b-b764-424a-b923-dbac86f1b151)

### How to flash?
 
To put the board into Download mode, simply bridge the 2 pads near the MicroUSB port, then connect the board into your PC using a [USB-A to USB-A cable](https://www.aliexpress.com/item/1005006854476947.html), connecting to the **USB-A port** on the WF1 (not the Micro USB input). It is NOT possible to program the board using the onboard Micro USB port. Thanks [Rappbledor](https://github.com/mrfaptastic/HD-WF1-LED-MatrixPanel-DMA/issues/3)

![image](https://github.com/user-attachments/assets/821aea15-4616-4b60-a251-4f1255f092e0)


Alternatively, solder a micro-usb to the exposed pins underneath to gain access to the D+ and D- lines connected to the USB-A port on the device. The ESP32-S2 routes the USB D+ and D- signals to GPIOs 20 and 19 respectively. 

![image](https://github.com/mrfaptastic/HD-WF1-LED-MatrixPanel-DMA/assets/12006953/fba33a4d-9737-4366-9a3b-776bec22ab2f)

The above has been successfully tested on board is rev 7.0.1-1 and it comes with an ESP32-S2 with 4MB flash and no PS Ram.

## Huidu WF2

HD-WF2 boards are easily flashed with USB-A to USB-C cables often used with phone chargers, so no need to look for rare USB-A to USB-A cables. You just need to plug type-c port to you laptop's type-c port (or some USB hub with type-c) and plug USB-A to the board, voila... :)

The best fit board definition for PlatformIO is:

`board = wifiduino32s3`

and if you need serial debug messages via USB-A port just add

```
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1
```

Limitations:

* Whilst the WF-2 has two HUB75 ('75EX1' and '75EX2') connectors. It is NOT possible to drive BOTH at once. You can only use wither the 75EX1 or 75EX2 port at any time to drive a single HUB75 panel, or a chain of panels.
* 75EX2 ports 'E' pin does not appear to be mapped to a GPIO on the S3, as such, can not be used to drive a HUB75 LED Panel that requires an 'E' input (for example, a 64x64 pixel LED Matrix).


[Source](https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA/discussions/667#discussioncomment-10438431)

## GitHub Sketch Output
The firmware features multiple display modes that can be cycled through using the onboard push button:

### Display Modes
1. **Clock with Animation** (Default) - Shows time and date with colorful animated background
2. **Clock Only** - Clean time and date display with no background animation
3. **Bouncing Squares** - Animated bouncing squares with time overlay

### Button Controls
- **Short Press** (< 2 seconds): Cycles through the display modes
- **Long Press** (> 2 seconds): Puts the device into deep sleep mode

The device will wake up from deep sleep when the button is pressed again.

### Technical Details
- Uses Bounce2 library for reliable button debouncing
- Button state is monitored continuously in the main loop
- Deep sleep preserves battery life when the display is not needed
- All three display modes maintain accurate timekeeping
- Bouncing squares animation uses physics-based collision detection
- **Enhanced RTC Management**:
  - Automatic data validation for RTC readings
  - NTP synchronization every 7 days (configurable)
  - Fallback from ESP32 internal RTC to external BM8563 RTC
  - Error handling and recovery for corrupted time data
  - Detailed logging of time synchronization events

Designed for a 64x32px LED matrix panel.

![image](https://github.com/user-attachments/assets/d7293beb-f293-4741-9fbf-6555be1db297)
