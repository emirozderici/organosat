
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <esp_task_wdt.h>
#include <Adafruit_VEML7700.h>
#include <Adafruit_AS7341.h> 
#include <Adafruit_SGP30.h>
#include <Adafruit_BME680.h>
#include <DFRobot_IICSerial.h>
#include "DFRobot_GNSS.h"
#include <HardwareSerial.h>
#include "GasBreakout.h"

#define IA1            1
#define IA0            0
#define I2C_SDA        6
#define I2C_SCL        7
#ifndef D6
  #define D6 21
#endif
#ifndef D7
  #define D7 20
#endif
#define LORA_TX_PIN    21
#define LORA_RX_PIN    20
#define LORA_M1_PIN    3
#define SD_CS_PIN      5
#define I2C_HUB_ADDR   0x20

static const float NO_DATA_F = -1.0f;
static const int   NO_DATA_I = -1;
// baseline resistance in clean air per gas, averaged over ~5 min of readings
const float R0_CO  = 127007.20f;
const float R0_NH3 = 24022.31f;
const float R0_NO2 = 4390.56f;

bool hasVEML = false;
bool hasAS   = false;
bool hasSGP  = false;
bool hasBME  = false;
bool hasMic  = false;
bool hasO2   = false;
bool hasGPS  = false;

bool sdReady      = false;

Adafruit_VEML7700  veml = Adafruit_VEML7700();
Adafruit_AS7341    as7341;
Adafruit_SGP30     sgp;
Adafruit_BME680    bme;
DFRobot_GNSS_I2C gnss(&Wire, 0x20);
DFRobot_IICSerial  o2Serial(Wire, SUBUART_CHANNEL_1, IA1, IA0);
GasBreakout gas(Wire, 0x19);

uint16_t gAS[10] = {0};
float    gLux       = NO_DATA_F;
float    gTemp      = NO_DATA_F;
float    gHum       = NO_DATA_F;
float    gPres      = NO_DATA_F;
int      gECO2      = NO_DATA_I;
int      gTVOC      = NO_DATA_I;
float    gNH3_ppm     = NO_DATA_F;
float    gCO_ppm    = NO_DATA_F;
float    gNO2_ppm     = NO_DATA_F;
float    gO2        = NO_DATA_F;
int      gDT_SCORE  = NO_DATA_I;

double   gGPS_Lat = 0.0;
double   gGPS_Lon = 0.0;
double   gGPS_Alt = 0.0;
int      gGPS_NumSat = 0;

unsigned long lastCheckTime = 0;
unsigned long currentTime   = 0;
int           roundCount    = 0;

void checkComponents() {
  Serial.println("checking sensors");
  esp_task_wdt_reset();
  
  hasVEML = veml.begin();
  if (hasVEML) {
    veml.setGain(VEML7700_GAIN_1_8); // Lowest  gain so it doesnt oversaturate values as we will be outside 
    veml.setIntegrationTime(VEML7700_IT_25MS); // Shortest integration configuration 
    esp_task_wdt_reset();
  }
  
  Serial.print("  VEML7700 : ");
  Serial.println(hasVEML ? "ok" : "not found");
  esp_task_wdt_reset();

  hasAS = as7341.begin();
  if (hasAS) {
    as7341.setATIME(100);
    as7341.setASTEP(999);
    as7341.setGain(AS7341_GAIN_8X); // will keep results normal during extreme outdoor brightness
    esp_task_wdt_reset();
  }
  Serial.print("  AS7341   : ");
  Serial.println(hasAS ? "ok" : "not found");
  esp_task_wdt_reset();

  if (!hasSGP) {
    esp_task_wdt_reset();
    hasSGP = sgp.begin();
    if (hasSGP) {
      sgp.IAQinit();
      esp_task_wdt_reset();
    }
  } else {
    bool stillThere = sgp.IAQmeasure();
    if (!stillThere) hasSGP = false;
    esp_task_wdt_reset();
  }
  esp_task_wdt_reset();
  Serial.print("  SGP30    : ");
  Serial.println(hasSGP ? "ok" : "not found");
  esp_task_wdt_reset();

  hasBME = bme.begin(0x76);
  if (!hasBME) hasBME = bme.begin(0x77);
  esp_task_wdt_reset();
  if (hasBME) {
    esp_task_wdt_reset();
    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  }
  esp_task_wdt_reset();
  Serial.print("  BME680   : ");
  Serial.println(hasBME ? "ok" : "not found");
  esp_task_wdt_reset();
  
  hasMic = gas.initialise();
  Serial.print("  MICS6814 : ");
  Serial.println(hasMic ? "ok" : "not found");
  esp_task_wdt_reset();

  while (o2Serial.available()) o2Serial.read();
  o2Serial.println("#MOXY");
  esp_task_wdt_reset();
  delay(1000);
  hasO2 = (o2Serial.available() > 0);
  if (hasO2) {
    Serial.print("  O2 probe: ");
    while (o2Serial.available()) {
      char c = o2Serial.read();
      Serial.print(c);
      esp_task_wdt_reset();
    }
    Serial.println();
  }
  Serial.print("  O2 Sensor: ");
  Serial.println(hasO2 ? "ok" : "not found");
  esp_task_wdt_reset();

  hasGPS = gnss.begin();
  Serial.print("  GPS      : ");
  Serial.println(hasGPS ? "ok" : "not found");
  esp_task_wdt_reset();
}

void readSensors() {
  int retries = 1;
  esp_task_wdt_reset();
  
  gLux = NO_DATA_F;
  if (hasVEML) {
    for (int attempt = 0; attempt <= retries; attempt++) {
      float lux = veml.readLux();
      if (lux >= 0.0f) {
        gLux = lux;
        break;
      }
      if (attempt < retries) delay(5);
    }
  }

  gTemp = gHum = gPres = NO_DATA_F;
  if (hasBME) {
    for (int attempt = 0; attempt <= retries; attempt++) {
      esp_task_wdt_reset();
      if (bme.performReading()) {
        gTemp     = bme.temperature;
        gTemp -= 10.0f; // Correcting 10 C offset observed during testing
        gHum      = bme.humidity;
        gPres     = bme.pressure / 100.0f;
        break;
      }
      if (attempt < retries) delay(10);
    }
  }
  esp_task_wdt_reset();

  gECO2 = NO_DATA_I;
  gTVOC = NO_DATA_I;
  if (hasSGP) {
    if (gTemp != NO_DATA_F && gHum != NO_DATA_F) {
      double absHum = 216.7 * (gHum / 100.0) * 6.112
                      * exp((17.62 * gTemp) / (243.12 + gTemp))
                      / (273.15 + gTemp);
      uint16_t ah = (uint16_t)(absHum * 256.0 + 0.5);
      if (ah > 0) sgp.setHumidity(ah);
    }
    for (int attempt = 0; attempt <= retries; attempt++) {
      if (sgp.IAQmeasure()) {
        gECO2 = sgp.eCO2;
        gTVOC = sgp.TVOC;
        break;
      }
      if (attempt < retries) delay(5);
    }
  }
  esp_task_wdt_reset();
// resistance to ppm conversion using sensitivity curves on SGX MiCS-6814 datasheet 
  gNH3_ppm = gCO_ppm = gNO2_ppm = NO_DATA_F;
  if (hasMic) {
    for (int attempt = 0; attempt <= retries; attempt++) {
      GasBreakout::Reading m;
      m = gas.readAll();
      if (m.nh3 > 0.0f && m.reducing > 0.0f && m.oxidising > 0.0f) {
        gCO_ppm  = pow(m.reducing  / R0_CO,  -1.179f) * 4.385f;
        gNH3_ppm = pow(m.nh3       / R0_NH3, -1.67f)  / 1.47f;
        gNO2_ppm = pow(m.oxidising / R0_NO2,  1.007f) / 6.855f;
        break;
      }
      if (attempt < retries) delay(5);
    }
  }
  esp_task_wdt_reset();

  memset(gAS, 0, sizeof(gAS));
  if (hasAS) {
    for (int attempt = 0; attempt <= retries; attempt++) {
      esp_task_wdt_reset();
      uint16_t ch[12];
      if (as7341.readAllChannels(ch)) {
        gAS[0] = ch[0];
        gAS[1] = ch[1];
        gAS[2] = ch[2];
        gAS[3] = ch[3];
        gAS[4] = ch[6];
        gAS[5] = ch[7];
        gAS[6] = ch[8];
        gAS[7] = ch[9];
        gAS[8] = ch[10];
        gAS[9] = ch[11];
        break;
      }
      if (attempt < retries) delay(5);
    }
  }
  esp_task_wdt_reset();

  gO2 = NO_DATA_F;
  if (hasO2) {
    for (int attempt = 0; attempt <= retries; attempt++) {
      delay(200);
      while (o2Serial.available()) o2Serial.read();
      o2Serial.println("#MOXY");
      esp_task_wdt_reset();
      delay(1000);
      esp_task_wdt_reset();
      if (o2Serial.available()) {
        char buf[64];
        int idx = 0;
        while (o2Serial.available() && idx < 63) {
          esp_task_wdt_reset();
          buf[idx++] = o2Serial.read();
        }
        buf[idx] = '\0';
        esp_task_wdt_reset();
        Serial.print("[O2 RAW] ");
        Serial.println(buf);
        char* found = strstr(buf, "MOXY");
        if (found) {
          char* p = found + 4;
          while (*p == ' ' || *p == ':' || *p == '=') p++;
          if (*p) {
            gO2 = atof(p);
            if (gO2 > 0.0f) {
              esp_task_wdt_reset();
              gO2 = gO2 / 10000.0f; // To convert raw oxygen into %
            } else {
              gO2 = -1.0f;
            }
            break;
          }
        }
      }
    }
  }
  esp_task_wdt_reset();

  gGPS_Lat = gGPS_Lon = gGPS_Alt = 0.0f;
  if (hasGPS) {
    gGPS_NumSat = gnss.getNumSatUsed();
    esp_task_wdt_reset();
    if (gGPS_NumSat >= 4) {
      esp_task_wdt_reset();
      sLonLat_t lat = gnss.getLat();
      sLonLat_t lon = gnss.getLon();
      double alt = gnss.getAlt();
      esp_task_wdt_reset();
      gGPS_Lat = lat.latitudeDegree;
      gGPS_Lon = lon.lonitudeDegree;
      gGPS_Alt = alt;
    }
  }
  esp_task_wdt_reset();
}

void buildTelemetryLine(char* buf, size_t len) {
  snprintf(buf, len,
    "ORG,%d,%lu,"
    "%.6f,%.6f,%.1f,"
    "%.2f,%.2f,%f,%.2f,"
    "%d,%d,"
    "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
    "%d,"
    "%.3f,%.3f,%.3f,"
    "%.2f",
    roundCount, millis(),
    gGPS_Lat, gGPS_Lon, gGPS_Alt,
    gTemp, gPres, gO2, gHum,
    gECO2, gTVOC,
    gAS[0], gAS[1], gAS[2], gAS[3], gAS[4],
    gAS[5], gAS[6], gAS[7], gAS[8], gAS[9],
    gDT_SCORE,
    gNH3_ppm, gCO_ppm, gNO2_ppm,
    gLux
  );
}

void saveToSD(const char* data) {
  if (!sdReady) return;

  File f = SD.open("/log_FinalV2.csv", FILE_APPEND);
  if (!f) {
    delay(20);
    f = SD.open("/log_FinalV2.csv", FILE_APPEND);
  }
  if (f) {
    f.println(data);
    f.flush();
    f.close();
  } else {
    Serial.println("SD card - Write FAILED");
  }
}

void writeSDHeader() {
  if (!sdReady) return;
  File f = SD.open("/log_FinalV2.csv", FILE_READ);
  bool empty = (!f || f.size() == 0);
  if (f) f.close();
  if (empty) {
    File fw = SD.open("/log_FinalV2.csv", FILE_WRITE);
    if (fw) {
      fw.println(F(
        "TEAM_ID,PACKET_COUNT,UPTIME_MS,"
        "GPS_LAT,GPS_LONG,GPS_ALT_M,"
        "TEMP_C,PRESSURE_HPA,OXYGEN_PCT,HUMIDITY_PCT,"
        "ECO2_PPM,TVOC_PPB,"
        "SPEC_415,SPEC_445,SPEC_480,SPEC_515,SPEC_555,SPEC_590,SPEC_630,SPEC_680,CLEAR,NIR,"
        "DT_SCORE,"
        "NH3_ppm,CO_ppm,NO2_ppm,"
        "LUX"
      ));
      fw.flush();
      fw.close();
      Serial.println("SD card - CSV header written.");
    }
  }
}

int computeLifeScore() {
    // If any sensor reads no data, skip the tree and return -1.
    if (gECO2 == NO_DATA_I|| gTemp == NO_DATA_F ||
        gHum == NO_DATA_F || gLux ==NO_DATA_F ||
        gCO_ppm == NO_DATA_F || gNH3_ppm == NO_DATA_F ||
        gNO2_ppm == NO_DATA_F) {
        return -1;
    }

    if (gNO2_ppm <= 0.504f) {
        if (gCO_ppm <= 1.984f) {
            if (gLux <= 4999.400f) {
                if (gLux <= 56.600f) {
                    if (gTemp <= -4.650f) {
                        return 0;
                    } else {
                        return 1;
                    }
                } else {
                    if (gTemp <= 18.990f) {
                        if (gHum <= 51.766f) {
                            return 0;
                        } else {
                            if (gNH3_ppm <= 25.317f) {
                                return 1;
                            } else {
                                return 0;
                            }
                        }
                    } else {
                        if (gECO2 <= 1949.500f) {
                            if (gNH3_ppm <= 25.732f) {
                                return 2;
                            } else {
                                return 0;
                            }
                        } else {
                            return 0;
                        }
                    }
                }
            } else {
                if (gLux <= 49992.451f) {
                    if (gTemp <= 9.987f) {
                        return 1;
                    } else {
                        return 2;
                    }
                } else {
                    return 1;
                }
            }
        } else {
            if (gNH3_ppm <= 146.093f) {
                if (gECO2 <= 3508.500f) {
                    return 1;
                } else {
                    return 0;
                }
            } else {
                return 0;
            }
        }
    } else {
        return 0;
    }
}

void printToSerial() {
  Serial.print("========== ROUND ");
  Serial.print(roundCount);
  Serial.println(" ==========\n");

  Serial.print("VEML Lux:        ");
  if (gLux != NO_DATA_F) { Serial.print(gLux, 2); Serial.println(" lx"); }
  else                     { Serial.println("N/A"); }
  Serial.println();

  if (gTemp != NO_DATA_F) {
    Serial.print("Temperature:     "); Serial.print(gTemp, 2); Serial.println(" C");
    Serial.print("Humidity:        "); Serial.print(gHum, 2);  Serial.println(" %");
    Serial.print("Pressure:        "); Serial.print(gPres, 2); Serial.println(" hPa");
  } else {
    Serial.println("BME680:          NO VALID READING");
  }
  Serial.println();

  if (gECO2 != NO_DATA_I) {
    Serial.print("SGP30 eCO2:      "); Serial.print(gECO2); Serial.println(" ppm");
    Serial.print("SGP30 TVOC:      "); Serial.print(gTVOC); Serial.println(" ppb");
  } else if (hasSGP) {
    Serial.println("SGP30:           NO VALID READING");
  } else {
    Serial.println("SGP30:           NOT FOUND");
  }
  Serial.println();

  if (gNH3_ppm != NO_DATA_F) {
    //Raw resistance data collected is converted to ppm values earlier and outputted for easy analysis.
    Serial.print("MICS6814 NH3:    "); Serial.print(gNH3_ppm, 3); Serial.println(" ppm");
    Serial.print("MICS6814 CO:     "); Serial.print(gCO_ppm,  3); Serial.println(" ppm");
    Serial.print("MICS6814 NO2:    "); Serial.print(gNO2_ppm, 3); Serial.println(" ppm");
  } else {
    Serial.println("MICS6814:        N/A");
  }
  Serial.println();

  Serial.print("Oxygen Level:    ");
  if (gO2 != NO_DATA_F) { Serial.print(gO2, 2); Serial.println(" %"); }
  else                    { Serial.println("N/A (No Reading)"); }
  Serial.println();

  if (hasAS) {
    Serial.println("AS7341 spectral channels debug:");
    for (int i = 0; i < 10; i++) {
      Serial.print("  Ch"); Serial.print(i);
      Serial.print(": "); Serial.println(gAS[i]);
    }
  } else {
    Serial.println("AS7341:          NOT FOUND");
  }
  Serial.println();

  if (hasGPS) {
    if (gGPS_Lat != 0.0 || gGPS_Lon != 0.0) {
      Serial.print("GPS Lat: "); Serial.println(gGPS_Lat, 6);
      Serial.print("GPS Lon: "); Serial.println(gGPS_Lon, 6);
      Serial.print("GPS Alt: "); Serial.println(gGPS_Alt, 1);
      Serial.print("GPS Sat: "); Serial.println(gGPS_NumSat);
    } else {
      Serial.print("GPS: Waiting for satellite fix...found ");
      Serial.println(gGPS_NumSat);
    }
  } else {
    Serial.println("GPS: NOT FOUND");
  }
  Serial.println("--------------------------------\n");
}

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);
  Serial.println("OrganoSat is starting...");

  pinMode(LORA_M1_PIN, OUTPUT);
  digitalWrite(LORA_M1_PIN, HIGH); // Start with radio in sleep mode to prevent GNSS interference
  esp_task_wdt_config_t wdtCfg = {
    .timeout_ms     = 5000,
    .idle_core_mask = 0,
    .trigger_panic  = true
  };
  esp_task_wdt_reconfigure(&wdtCfg);
  esp_task_wdt_add(NULL);
  Serial.println("watchdog has been armed successfully to 5 secs");

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  Wire.setTimeout(500);

  o2Serial.begin(19200);

  Wire.beginTransmission(I2C_HUB_ADDR);
  Serial.println(Wire.endTransmission() == 0 ? "I2C hub exists." : "I2C is not there.");

  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  SPI.begin(8, 9, 10, SD_CS_PIN);
  delay(200);
  sdReady = SD.begin(SD_CS_PIN, SPI, 4000000);
  Serial.println(sdReady ? "SD card ready." : "SD card not found.");
  if (sdReady) writeSDHeader();

  Serial1.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
  Serial.println("LoRa serial ready.");
  gas.initialise();
  Serial.println("MICS6814 adc ready.");

  checkComponents();

  lastCheckTime = millis();
  Serial.println("Setup complete.\n");
}

void loop() {
  currentTime = millis();
  esp_task_wdt_reset();

  roundCount++;
  readSensors();
  gDT_SCORE = computeLifeScore();

  esp_task_wdt_reset();

  char line[512];
  buildTelemetryLine(line, sizeof(line));

  printToSerial();
  Serial.print("[LINE] "); Serial.println(line);

  esp_task_wdt_reset();
  saveToSD(line);
  // to execute once every 5 rounds.
  if (roundCount % 5 == 0) {
    esp_task_wdt_reset();
    digitalWrite(LORA_M1_PIN, LOW); // Wake radio up 
    esp_task_wdt_reset();
    delay(500);
    esp_task_wdt_reset();
    Serial1.println(line); // Transmit 
    esp_task_wdt_reset();
    delay(200);
    esp_task_wdt_reset();
    digitalWrite(LORA_M1_PIN, HIGH); // Force sleep mode 
    esp_task_wdt_reset();
  }

  unsigned long healthInterval = 15000;
  if (currentTime - lastCheckTime >= healthInterval) {
    lastCheckTime = currentTime;
    esp_task_wdt_reset();
    checkComponents();
  }

  esp_task_wdt_reset();
  
  unsigned long targetCycle = 200;
  unsigned long elapsed = millis() - currentTime;
  if (elapsed < targetCycle) {
    unsigned long remaining = targetCycle - elapsed;
    unsigned long waitStart = millis();
    while (millis() - waitStart < remaining) {
      //Watchdog reset to prevent accidental trigger and a waste of time
      esp_task_wdt_reset();
      delay(1);
    }
  }
}
