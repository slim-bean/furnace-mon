#include <Arduino.h>
#include <esp_task_wdt.h>
#include "EmonLib.h"

EnergyMonitor emon1; 

void setup() {
  Serial.begin(115200);

  // Start the watchdog timer, sometimes connecting to wifi or trying to set the time can fail in a way that never recovers
  // esp_task_wdt_init(WDT_TIMEOUT, true);
  // esp_task_wdt_add(NULL);

  // Wait 5s for serial connection or continue without it
  // some boards like the esp32 will run whether or not the
  // serial port is connected, others like the MKR boards will wait
  // for ever if you don't break the loop.
  uint8_t serialTimeout = 0;
  while (!Serial && serialTimeout < 50)
  {
    delay(100);
    serialTimeout++;
  }

  emon1.current(ANALOG_PIN, CAL_VAL); 
}

void loop() {
  // put your main code here, to run repeatedly:
  delay(250);
  double Irms = emon1.calcIrms(1480);  // Calculate Irms only
  //int Irms = analogRead(ANALOG_PIN)
  
  // Serial.print(Irms*115.0);	       // Apparent power
  // Serial.print(" ");
  Serial.println(Irms);		       // Irms

}
