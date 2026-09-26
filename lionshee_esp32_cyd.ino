#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>
#include "mbedtls/md.h"

#include "secrets.h"
#include "config.h"

// ============================================================
// Tuya
// ============================================================

const char* TUYA_HOST = "https://openapi.tuyaeu.com";

const char* TUYA_EMPTY_SHA256 =
  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

// ============================================================
// Display
// ============================================================

TFT_eSPI tft = TFT_eSPI();

// ============================================================
// Data
// ============================================================

float BactivePower = 0.0;
float RactivePower = 0.0;
float Rvoltage = 0.0;

float power_w = 0.0;

int preset_power = 0;
int batt_soc = 0;
int calculated_power = 0;

bool activePowerValid = false;
bool socOverrideApplied = false;

// ============================================================
// Polling
// ============================================================

unsigned long lastPollMillis = 0;

// ============================================================
// Tuya authentication
// ============================================================

String hmacSha256(
    const String& key,
    const String& data
)
{
  byte hmacResult[32];

  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);

  const mbedtls_md_info_t* mdInfo =
      mbedtls_md_info_from_type(
          MBEDTLS_MD_SHA256
      );

  mbedtls_md_setup(
      &ctx,
      mdInfo,
      1
  );

  mbedtls_md_hmac_starts(
      &ctx,
      (const unsigned char*)key.c_str(),
      key.length()
  );

  mbedtls_md_hmac_update(
      &ctx,
      (const unsigned char*)data.c_str(),
      data.length()
  );

  mbedtls_md_hmac_finish(
      &ctx,
      hmacResult
  );

  mbedtls_md_free(&ctx);

  String result;

  for (int i = 0; i < 32; i++)
  {
    if (hmacResult[i] < 16)
      result += "0";

    result += String(
        hmacResult[i],
        HEX
    );
  }

  result.toUpperCase();

  return result;
}

// ============================================================
// SHA256
// ============================================================

String sha256Hex(
    const String& data
)
{
  byte hash[32];

  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);

  const mbedtls_md_info_t* mdInfo =
      mbedtls_md_info_from_type(
          MBEDTLS_MD_SHA256
      );

  mbedtls_md_setup(
      &ctx,
      mdInfo,
      0
  );

  mbedtls_md_starts(
      &ctx
  );

  mbedtls_md_update(
      &ctx,
      (const unsigned char*)data.c_str(),
      data.length()
  );

  mbedtls_md_finish(
      &ctx,
      hash
  );

  mbedtls_md_free(
      &ctx
  );

  String result;

  for (int i = 0; i < 32; i++)
  {
    if (hash[i] < 16)
      result += "0";

    result += String(
        hash[i],
        HEX
    );
  }

  result.toLowerCase();

  return result;
}

// ============================================================
// Timestamp
// ============================================================

String getTimestamp()
{
  struct timeval tv;

  gettimeofday(
      &tv,
      nullptr
  );

  unsigned long long ms =
      ((unsigned long long)tv.tv_sec * 1000ULL) +
      (tv.tv_usec / 1000ULL);

  return String(ms);
}

// ============================================================
// Supla
// ============================================================

bool readSupla(
    const char* url,
    float& activePower,
    float* voltage = nullptr
)
{
  for (int attempt = 1; attempt <= 3; attempt++)
  {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;

    if (!http.begin(client, url))
    {
      delay(2000);
      continue;
    }

    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK)
    {
      String payload =
          http.getString();

      http.end();

      JsonDocument doc;

      DeserializationError error =
          deserializeJson(
              doc,
              payload
          );

      if (error)
      {
        delay(2000);
        continue;
      }

      JsonObject root =
          doc.as<JsonObject>();

      JsonArray phases =
          root["phases"].as<JsonArray>();

      if (
          phases.isNull() ||
          phases.size() == 0
      )
      {
        delay(2000);
        continue;
      }

      JsonObject phase =
          phases[0].as<JsonObject>();

      if (
          phase["powerActive"].isNull()
      )
      {
        delay(2000);
        continue;
      }

      activePower =
          phase["powerActive"].as<float>();

      if (voltage != nullptr)
      {
        if (!phase["voltage"].isNull())
        {
          *voltage =
              phase["voltage"].as<float>();
        }
      }

      return true;
    }

    http.end();

    delay(2000);
  }

  return false;
}

// ============================================================
// Tuya token
// ============================================================

bool getTuyaToken(
    String& accessToken
)
{
  String timestamp =
      getTimestamp();

  String stringToSign =
      "GET\n" +
      String(TUYA_EMPTY_SHA256) +
      "\n\n/v1.0/token?grant_type=1";

  String signString =
      String(TUYA_CLIENT_ID) +
      timestamp +
      stringToSign;

  String sign =
      hmacSha256(
          TUYA_CLIENT_SECRET,
          signString
      );

  String url =
      String(TUYA_HOST) +
      "/v1.0/token?grant_type=1";

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  if (!http.begin(client, url))
    return false;

  http.addHeader(
      "client_id",
      TUYA_CLIENT_ID
  );

  http.addHeader(
      "t",
      timestamp
  );

  http.addHeader(
      "sign_method",
      "HMAC-SHA256"
  );

  http.addHeader(
      "sign",
      sign
  );

  int httpCode =
      http.GET();

  if (httpCode != HTTP_CODE_OK)
  {
    http.end();
    return false;
  }

  String payload =
      http.getString();

  http.end();

  JsonDocument doc;

  DeserializationError error =
      deserializeJson(
          doc,
          payload
      );

  if (error)
    return false;

  if (
      !doc["result"]["access_token"]
          .is<const char*>()
  )
  {
    return false;
  }

  accessToken =
      doc["result"]["access_token"]
          .as<String>();

  return true;
}

// ============================================================
// Tuya shadow READ
// ============================================================

bool readTuyaShadow(
    int& presetPower,
    int& battSoc
)
{
  String accessToken;

  if (!getTuyaToken(accessToken))
    return false;

  const char* propertiesPath =
      "/v2.0/cloud/thing/"
      "bf1249852d1753bf782mm6"
      "/shadow/properties";

  String timestamp =
      getTimestamp();

  String stringToSign =
      "GET\n" +
      String(TUYA_EMPTY_SHA256) +
      "\n\n" +
      String(propertiesPath);

  String signString =
      String(TUYA_CLIENT_ID) +
      accessToken +
      timestamp +
      stringToSign;

  String sign =
      hmacSha256(
          TUYA_CLIENT_SECRET,
          signString
      );

  String url =
      String(TUYA_HOST) +
      String(propertiesPath);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  if (!http.begin(client, url))
    return false;

  http.addHeader(
      "client_id",
      TUYA_CLIENT_ID
  );

  http.addHeader(
      "t",
      timestamp
  );

  http.addHeader(
      "sign_method",
      "HMAC-SHA256"
  );

  http.addHeader(
      "sign",
      sign
  );

  http.addHeader(
      "access_token",
      accessToken
  );

  int httpCode =
      http.GET();

  if (httpCode != HTTP_CODE_OK)
  {
    http.end();
    return false;
  }

  String payload =
      http.getString();

  http.end();

  JsonDocument doc;

  DeserializationError error =
      deserializeJson(
          doc,
          payload
      );

  if (error)
    return false;

  JsonArray properties =
      doc["result"]["properties"]
          .as<JsonArray>();

  if (properties.isNull())
    return false;

  bool foundPreset = false;
  bool foundSoc = false;

  for (JsonObject property : properties)
  {
    const char* code =
        property["code"];

    if (!code)
      continue;

    if (
        strcmp(
            code,
            "preset_power"
        ) == 0
    )
    {
      presetPower =
          property["value"].as<int>();

      foundPreset = true;
    }

    if (
        strcmp(
            code,
            "batt_soc"
        ) == 0
    )
    {
      battSoc =
          property["value"].as<int>();

      foundSoc = true;
    }
  }

  return foundPreset && foundSoc;
}

// ============================================================
// Tuya shadow WRITE
// ============================================================

bool setTuyaPower(int newPower)
{
  Serial.println();
  Serial.println("------------------------------");
  Serial.println("Tuya POST");
  Serial.println("------------------------------");

  Serial.print("newPower = ");
  Serial.println(newPower);

  String accessToken;
  if (!getTuyaToken(accessToken)) {
    Serial.println("Tuya token failed");
    return false;
  }

  const char* propertiesPath =
      "/v2.0/cloud/thing/"
      "bf1249852d1753bf782mm6"
      "/shadow/properties/issue";

  // Build exactly the same body as n8n:
  // {"properties":"{\"default_power\":20}"}
  String bodyString =
      "{\"properties\":\"{\\\"default_power\\\":" +
      String(newPower) +
      "}\"}";

  String timestamp = getTimestamp();

  String bodyHash = sha256Hex(bodyString);

  String stringToSign =
      "POST\n" +
      bodyHash +
      "\n\n" +
      String(propertiesPath);

  String signString =
      String(TUYA_CLIENT_ID) +
      accessToken +
      timestamp +
      stringToSign;

  String sign =
      hmacSha256(TUYA_CLIENT_SECRET, signString);

  Serial.print("Path: ");
  Serial.println(propertiesPath);

  Serial.print("Body: ");
  Serial.println(bodyString);

  Serial.print("Body SHA256: ");
  Serial.println(bodyHash);

  Serial.print("Timestamp: ");
  Serial.println(timestamp);

  Serial.print("Sign: ");
  Serial.println(sign);

  String url =
      String(TUYA_HOST) +
      String(propertiesPath);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  if (!http.begin(client, url)) {
    Serial.println("HTTP begin failed");
    return false;
  }

  http.addHeader("client_id", TUYA_CLIENT_ID);
  http.addHeader("access_token", accessToken);
  http.addHeader("t", timestamp);
  http.addHeader("sign_method", "HMAC-SHA256");
  http.addHeader("sign", sign);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST(bodyString);

  String response = http.getString();

  Serial.print("HTTP code: ");
  Serial.println(httpCode);

  Serial.print("Tuya response: ");
  Serial.println(response);

  http.end();

  // HTTP 200 does NOT necessarily mean Tuya accepted the command.
  JsonDocument responseDoc;

  DeserializationError error =
      deserializeJson(responseDoc, response);

  bool tuyaSuccess = false;

  if (!error) {
    tuyaSuccess =
        responseDoc["success"] | false;

    int code =
        responseDoc["code"] | 0;

    const char* msg =
        responseDoc["msg"] | "";

    Serial.print("Tuya success: ");
    Serial.println(tuyaSuccess ? "true" : "false");

    Serial.print("Tuya code: ");
    Serial.println(code);

    Serial.print("Tuya msg: ");
    Serial.println(msg);
  }
  else {
    Serial.print("Tuya JSON parse error: ");
    Serial.println(error.c_str());
  }

  if (httpCode >= 200 &&
      httpCode < 300 &&
      tuyaSuccess) {

    Serial.println("Tuya power update OK");
    return true;
  }

  Serial.println("Tuya power update FAILED");
  return false;
}

// ============================================================
// Calculation
// ============================================================

void calculatePower()
{
  activePowerValid = true;

  float ap =
      BactivePower;

  if (
      !isfinite(ap) ||
      fabs(ap) > MAX_VALID_ACTIVE_POWER ||
      !isfinite(power_w)
  )
  {
    activePowerValid = false;
    ap = 0;
  }

  if (!activePowerValid)
  {
    calculated_power =
        preset_power;
  }
  else if (ap < 0)
  {
    calculated_power = 0;
  }
  else
  {
    float newPower =
        preset_power +
        ap -
        power_w;

    newPower =
        max(
            0.0f,
            newPower
        );

    newPower =
        min(
            (float)MAX_POWER,
            newPower
        );

    newPower =
        round(
            newPower / 10.0f
        ) * 10.0f;

    calculated_power =
        (int)newPower;
  }

  socOverrideApplied = false;

  if (
      batt_soc >=
      SOC_FORCE_THRESHOLD
  )
  {
    calculated_power =
        SOC_FORCE_POWER;

    socOverrideApplied = true;
  }
}

// ============================================================
// Display helper
// ============================================================

void drawLabelValue(
    int y,
    const char* label,
    const char* valueStr,
    uint16_t valueColor = TFT_WHITE
)
{
  tft.setTextSize(2);

  tft.setTextColor(
      TFT_WHITE,
      TFT_BLACK
  );

  tft.setCursor(
      LEFT_X,
      y
  );

  tft.print(label);

  tft.setTextColor(
      valueColor,
      TFT_BLACK
  );

  int valueWidth =
      tft.textWidth(valueStr);

  tft.setCursor(
      RIGHT_X - valueWidth,
      y
  );

  tft.print(valueStr);
}

// ============================================================
// Battery display
// ============================================================

void drawSocBattery(
    int x,
    int y,
    int w,
    int h,
    int soc,
    const char* name
)
{
  bool nodata =
      (soc < 0);

  soc =
      constrain(
          soc,
          0,
          100
      );

  uint16_t barColor;

  if (nodata)
  {
    barColor = TFT_DARKGREY;
  }
  else if (soc < 20)
  {
    barColor = TFT_RED;
  }
  else if (soc < 50)
  {
    barColor = TFT_YELLOW;
  }
  else
  {
    barColor = TFT_GREEN;
  }

  int nubWidth = 4;
  int nubHeight = h / 2;

  // Battery outline
  tft.drawRect(
      x,
      y,
      w,
      h,
      TFT_WHITE
  );

  // Battery terminal
  tft.fillRect(
      x + w,
      y + (h - nubHeight) / 2,
      nubWidth,
      nubHeight,
      TFT_WHITE
  );

  // Battery interior
  tft.fillRect(
      x + 2,
      y + 2,
      w - 4,
      h - 4,
      TFT_BLACK
  );

  // Battery charge
  int fillWidth =
      nodata
          ? 0
          : (int)(
              (w - 4) *
              soc /
              100.0
            );

  if (fillWidth > 0)
  {
    tft.fillRect(
        x + 2,
        y + 2,
        fillWidth,
        h - 4,
        barColor
    );
  }

  // Battery name: black shadow + white text
  int textY =
      y + (h - 16) / 2;

  tft.setTextSize(2);

  tft.setTextColor(
      TFT_BLACK
  );

  tft.setCursor(
      x + 9,
      textY + 1
  );

  tft.print(name);

  tft.setTextColor(
      TFT_WHITE
  );

  tft.setCursor(
      x + 8,
      textY
  );

  tft.print(name);

  // SOC outside the battery
  char pct[12];

  if (nodata)
  {
    snprintf(
        pct,
        sizeof(pct),
        "--"
    );
  }
  else
  {
    snprintf(
        pct,
        sizeof(pct),
        "%d %%",
        soc
    );
  }

  tft.setTextColor(
      nodata
          ? TFT_DARKGREY
          : TFT_WHITE,
      TFT_BLACK
  );

  tft.setCursor(
      RIGHT_X -
      tft.textWidth(pct),
      textY
  );

  tft.print(pct);
}

// ============================================================
// Screen
// ============================================================

void drawScreen()
{
  tft.fillScreen(
      TFT_BLACK
  );

  tft.setTextSize(2);

  // Header
  tft.setTextColor(
      TFT_WHITE,
      TFT_BLACK
  );

  tft.setCursor(
      LEFT_X,
      5
  );

  tft.print(
      "LS-H1600"
  );

  // Current local time
  struct tm timeinfo;

  if (
      getLocalTime(
          &timeinfo,
          100
      )
  )
  {
    char timeBuffer[6];

    strftime(
        timeBuffer,
        sizeof(timeBuffer),
        "%H:%M",
        &timeinfo
    );

    int timeWidth =
        tft.textWidth(timeBuffer);

    tft.setCursor(
        RIGHT_X - timeWidth,
        5
    );

    tft.print(
        timeBuffer
    );
  }

  tft.drawLine(
      10,
      27,
      310,
      27,
      TFT_WHITE
  );

  char buffer[32];

  // B meter
  snprintf(
      buffer,
      sizeof(buffer),
      "%.1f W",
      BactivePower
  );

  drawLabelValue(
      35,
      "BLO:",
      buffer
  );

  // R meter
  snprintf(
      buffer,
      sizeof(buffer),
      "%.1f W",
      RactivePower
  );

  drawLabelValue(
      58,
      "ROD:",
      buffer
  );

  // R voltage
  snprintf(
      buffer,
      sizeof(buffer),
      "%.1f V",
      Rvoltage
  );

  uint16_t voltageColor =
      (Rvoltage > 253.0)
          ? TFT_RED
          : TFT_WHITE;

  drawLabelValue(
      81,
      "U ROD:",
      buffer,
      voltageColor
  );

  // Preset power
  snprintf(
      buffer,
      sizeof(buffer),
      "%d W",
      preset_power
  );

  drawLabelValue(
      104,
      "Preset:",
      buffer
  );

  // Lionshee battery
  drawSocBattery(
      10,
      135,
      240,
      24,
      batt_soc,
      "LionShee"
  );

  // Calculated power
  snprintf(
      buffer,
      sizeof(buffer),
      "%d W",
      calculated_power
  );

  uint16_t calcColor =
      activePowerValid
          ? TFT_GREEN
          : TFT_YELLOW;

  drawLabelValue(
      180,
      "CALC:",
      buffer,
      calcColor
  );

  // Status
  const char* statusText;

  if (!activePowerValid)
  {
    statusText = "INVALID";
  }
  else if (socOverrideApplied)
  {
    statusText = "FORCE 650 W";
  }
  else
  {
    statusText = "OK";
  }

  uint16_t statusColor =
      !activePowerValid
          ? TFT_YELLOW
          : (
              socOverrideApplied
                  ? TFT_BLUE
                  : TFT_GREEN
            );

  drawLabelValue(
      207,
      "Status:",
      statusText,
      statusColor
  );
}

// ============================================================
// Read all data
// ============================================================

void pollData()
{
  Serial.println();
  Serial.println("==============================");
  Serial.println("Starting data refresh");
  Serial.println("==============================");

  // ----------------------------------------------------------
  // Read Supla B
  // ----------------------------------------------------------

  Serial.println(
      "Reading Supla B..."
  );

  if (
      readSupla(
          SUPLA_B_URL,
          BactivePower
      )
  )
  {
    Serial.print(
        "BactivePower = "
    );

    Serial.println(
        BactivePower
    );
  }
  else
  {
    Serial.println(
        "Supla B read failed"
    );
  }

  // ----------------------------------------------------------
  // Read Supla R
  // ----------------------------------------------------------

  Serial.println(
      "Reading Supla R..."
  );

  if (
      readSupla(
          SUPLA_R_URL,
          RactivePower,
          &Rvoltage
      )
  )
  {
    Serial.print(
        "RactivePower = "
    );

    Serial.println(
        RactivePower
    );

    Serial.print(
        "Rvoltage = "
    );

    Serial.println(
        Rvoltage
    );

    // Required calculation input
    power_w =
        -RactivePower;

    Serial.print(
        "power_w = "
    );

    Serial.println(
        power_w
    );
  }
  else
  {
    Serial.println(
        "Supla R read failed"
    );
  }

  // ----------------------------------------------------------
  // Read Tuya Shadow
  // ----------------------------------------------------------

  Serial.println(
      "Reading Tuya shadow..."
  );

  if (
      readTuyaShadow(
          preset_power,
          batt_soc
      )
  )
  {
    Serial.print(
        "preset_power = "
    );

    Serial.println(
        preset_power
    );

    Serial.print(
        "batt_soc = "
    );

    Serial.println(
        batt_soc
    );
  }
  else
  {
    Serial.println(
        "Tuya shadow read failed"
    );
  }

  // ----------------------------------------------------------
  // Calculate
  // ----------------------------------------------------------

  calculatePower();

  Serial.print(
      "calculated_power = "
  );

  Serial.println(
      calculated_power
  );

  Serial.print(
      "activePowerValid = "
  );

  Serial.println(
      activePowerValid
  );

  Serial.print(
      "socOverrideApplied = "
  );

  Serial.println(
      socOverrideApplied
  );

  // ----------------------------------------------------------
  // Send calculated power to Tuya
  // ----------------------------------------------------------

  setTuyaPower(
      calculated_power
  );

  // ----------------------------------------------------------
  // Update display
  // ----------------------------------------------------------

  drawScreen();

  Serial.println("==============================");
  Serial.println("Refresh complete");
  Serial.println("==============================");
}

// ============================================================
// Setup
// ============================================================

void setup()
{
  Serial.begin(
      115200
  );

  delay(500);

  // ----------------------------------------------------------
  // Display
  // ----------------------------------------------------------

  tft.init();

  tft.setRotation(1);

  tft.invertDisplay(
      false
  );

  tft.fillScreen(
      TFT_BLACK
  );

  tft.setTextColor(
      TFT_WHITE,
      TFT_BLACK
  );

  tft.setTextSize(2);

  tft.setCursor(
      10,
      10
  );

  tft.print(
      "Connecting WiFi..."
  );

  // ----------------------------------------------------------
  // WiFi
  // ----------------------------------------------------------

  WiFi.mode(
      WIFI_STA
  );

  WiFi.begin(
      WIFI_SSID,
      WIFI_PASSWORD
  );

  while (
      WiFi.status() !=
      WL_CONNECTED
  )
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  Serial.println(
      "WiFi connected"
  );

  Serial.print(
      "IP: "
  );

  Serial.println(
      WiFi.localIP()
  );

  // ----------------------------------------------------------
  // NTP
  // ----------------------------------------------------------

  // Poland: CET (UTC+1) in winter,
  // CEST (UTC+2) in summer.
  // DST transitions are handled automatically.
  configTime(
      0,
      0,
      "pool.ntp.org",
      "time.nist.gov"
  );

  setenv(
      "TZ",
      "CET-1CEST,M3.5.0,M10.5.0/3",
      1
  );

  tzset();

  delay(1000);

  // ----------------------------------------------------------
  // Initial read
  // ----------------------------------------------------------

  pollData();

  // Start 60-second polling interval
  lastPollMillis =
      millis();
}

// ============================================================
// Loop
// ============================================================

void loop()
{
  unsigned long currentMillis =
      millis();

  if (
      currentMillis -
      lastPollMillis >=
      POLL_INTERVAL_MS
  )
  {
    lastPollMillis =
        currentMillis;

    pollData();
  }
}