// Used this sketch with a multimeter to see when certain traces on the PCB had a voltage.

// Pin 11 on the WF-01 is the toggle switch

const int outputPins[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15, 16, 17, 18, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45};
const int numOutputPins = sizeof(outputPins) / sizeof(outputPins[0]);

volatile bool interruptFlag = false;

void setup() {
  // Initialize all output pins as output pins
  for (int i = 0; i < numOutputPins; i++) {
    pinMode(outputPins[i], OUTPUT);
  }
  
  pinMode(11, INPUT_PULLUP); // Set pin 11 as input with internal pull-up resistor
  // The button is active-low (pulled up), so trigger on the falling edge.
  attachInterrupt(digitalPinToInterrupt(11), runme, FALLING);

  // Start the serial communication
  Serial.begin(115200);
}

void loop() {
  // Toggle each pin's output value high and low for one second each, except for pin 11
  for (int i = 0; i < numOutputPins; i++) {

    if (outputPins[i] == 11) {
      continue; // Skip pin 11
    }
    digitalWrite(outputPins[i], HIGH); // Set pin i high
    Serial.print("Pin ");
    Serial.print(outputPins[i]);
    Serial.println(" set to HIGH");
    delay(1000); // Wait for one second


    // Wait for the interrupt to occur on pin 11
    if (interruptFlag) {
      interruptFlag = false;
      // Do something here when the interrupt occurs on pin 11
      //break;
      delay(30000);
    }

    digitalWrite(outputPins[i], LOW); // Set pin i low
    Serial.print("Pin ");
    Serial.print(outputPins[i]);
    Serial.println(" set to LOW");
    delay(1000); // Wait for one second
  }


}

void IRAM_ATTR runme() {
  // This function will be called when the interrupt occurs on pin 11
  interruptFlag = true;
  // Do something here
}
