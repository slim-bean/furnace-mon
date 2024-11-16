#include <Arduino.h>
#include <esp_task_wdt.h>
#include "EmonLib.h"
#include <Wire.h>
#include <SPI.h>
#include "Adafruit_SHT31.h"

#include <PrometheusArduino.h>
#include <GrafanaLoki.h>

#include "config.h"
#include "certificates.h"

#define WDT_TIMEOUT 60

EnergyMonitor emon1;
Adafruit_SHT31 sht31 = Adafruit_SHT31();
// Create a transport and client object for sending our data.
PromLokiTransport transport;
PromClient prom(transport);
LokiClient loki(transport);

// Heartbeat message: "msg=heartbeat batt=0.000000 rssi=-62 temp=21.58 humidity=00.00"
// temp/humidity is not included in all, 100 chars should cover everything.
#define HBM_LENGTH 200
LokiStream message(1, HBM_LENGTH, "{job=\"furnace-mon\",location=\"" LOCATION "\"}");

LokiStreams streams(1);

// Create a write request for  series.
WriteRequest req(3, 1024);

TimeSeries temp(5, "temp_f", "{job=\"furance-mon\",location=\"" LOCATION "\"}");
TimeSeries humidity(5, "humidity", "{job=\"furance-mon\",location=\"" LOCATION "\"}");
TimeSeries current(5, "current_a", "{job=\"furance-mon\",location=\"" LOCATION "\"}");

void setup()
{
  Serial.begin(115200);

  // Start the watchdog timer, sometimes connecting to wifi or trying to set the time can fail in a way that never recovers
  esp_task_wdt_config_t config = {
      .timeout_ms = 300000,
      .trigger_panic = true,
  };
  esp_task_wdt_reconfigure(&config);
  esp_task_wdt_add(NULL);

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

  transport.setWifiSsid(WIFI_SSID);
  transport.setWifiPass(WIFI_PASSWORD);
  transport.setUseTls(true);
  transport.setCerts(grafanaCert, strlen(grafanaCert));
  transport.setDebug(Serial); // Remove this line to disable debug logging of the transport layer.
  if (!transport.begin())
  {
    Serial.println(transport.errmsg);
    while (true)
    {
    };
  }

  // Configure the client
  loki.setUrl(LOKI_URL);
  loki.setPath((char *)LOKI_PATH);
  loki.setPort(LOKI_PORT);
  loki.setUser(LOKI_USER);
  loki.setPass(LOKI_PASS);

  loki.setDebug(Serial); // Remove this line to disable debug logging of the client.
  if (!loki.begin())
  {
    Serial.println(loki.errmsg);
    while (true)
    {
    };
  }

  // Configure the client
  prom.setUrl(GC_URL);
  prom.setPath((char *)GC_PATH);
  prom.setPort(GC_PORT);
  prom.setUser(GC_USER);
  prom.setPass(GC_PASS);
  prom.setDebug(Serial); // Remove this line to disable debug logging of the client.
  if (!prom.begin())
  {
    Serial.println(prom.errmsg);
    while (true)
    {
    };
  }

  // Add our TimeSeries to the WriteRequest
  req.addTimeSeries(temp);
  req.addTimeSeries(humidity);
  req.addTimeSeries(current);
  req.setDebug(Serial);

  // Add our stream objects to the streams object
  streams.addStream(message);
  streams.setDebug(Serial); // Remove this line to disable debug logging of the write request serialization and compression.

  Wire.begin(I2C_SDA, I2C_SCL);

  emon1.current(ANALOG_PIN, CAL_VAL);
  if (!sht31.begin(0x45))
  { // Set to 0x45 for alternate i2c addr
    Serial.println("Couldn't find SHT31");
    while (1)
      delay(1);
  }

  esp_task_wdt_reset();
}

double Irms;
uint64_t lastIrms;
uint64_t lastSend;

void loop()
{
  char heartbeatMsg[HBM_LENGTH] = {'\0'};
  uint64_t start = millis();

  // Update this more frequently because it's an rms value with some history
  if (start - lastIrms > 250)
  {
    // Get current
    Irms = emon1.calcIrms(1480); // Calculate Irms only
    lastIrms = start;
  }

  if (start - lastSend > 15000)
  {
    lastSend = start;
    // Get temp and humidity
    float t = sht31.readTemperature();
    t = t * 1.8 + 32;
    float h = sht31.readHumidity();

    Serial.print(Irms);
    Serial.println("A");
    Serial.print(t);
    Serial.println("F");
    Serial.print(h);
    Serial.println("%");

    uint64_t time;
    time = loki.getTimeNanos();
    snprintf(heartbeatMsg, HBM_LENGTH, "millis=%llu rssi=%d temp=%.2f humidity=%.2f current=%.2f", start, WiFi.RSSI(), t, h, Irms);
    if (!message.addEntry(time, heartbeatMsg, strlen(heartbeatMsg)))
    {
      Serial.println(message.errmsg);
    }

    // Send the message, we build in a few retries as well.
    uint64_t lokiStart = millis();
    for (uint8_t i = 0; i <= 5; i++)
    {
      LokiClient::SendResult res = loki.send(streams);
      if (res != LokiClient::SendResult::SUCCESS)
      {
        // Failed to send
        Serial.println(loki.errmsg);
        delay(1000);
      }
      else
      {
        esp_task_wdt_reset();
        message.resetEntries();
        uint32_t diff = millis() - lokiStart;
        Serial.print("Send succesful in ");
        Serial.print(diff);
        Serial.println("ms");
        break;
      }
    }
  }

  delay(50);
}
