
#include "Arduino.h"
#include "loggingHelper.h"
#include <sstream>
#include <string>
#include <cstring>

#include "BLEDevice.h"
#include <WiFi.h>
#include "credentials.h"
#include <PubSubClient.h>

#include <rom/rtc.h>
#include <esp_int_wdt.h>
#include <esp_task_wdt.h>

#define PROBEERRORVALUE 6550 //ERROR Value, if Probe Value greater than this Value it is not published

WiFiClient espClient;
PubSubClient mqttclient(espClient);

static uint8_t credentials[] = {0x21, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0xb8, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00};
static uint8_t enableRealTimeData[] = {0x0B, 0x01, 0x00, 0x00, 0x00, 0x00};
static uint8_t unitCelsius[] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
static uint8_t batteryLevel[] = {0x08, 0x24, 0x00, 0x00, 0x00, 0x00};

//Device
static BLEUUID serviceUUID("0000fff0-0000-1000-8000-00805f9b34fb");
static BLEUUID RealtimeData(BLEUUID((uint16_t)0xfff4));
static BLEUUID SettingsData(BLEUUID((uint16_t)0xfff5));
static BLEUUID AccountAndVerify(BLEUUID((uint16_t)0xfff2));
static BLEUUID SettingsResults(BLEUUID((uint16_t)0xfff1));

long lastReconnectAttempt = 0;

bool wifiReConnect();
uint16_t littleEndianInt(uint8_t *pData);
uint16_t bigEndianInt(uint8_t *pData);
void setupWIFI();
// for more detailed instructions on these settings, see the EspMQTTClient github repo

static BLEAddress *pServerAddress;
static boolean doConnect = false;
static boolean connected = false;
static BLERemoteCharacteristic *pRemoteCharacteristic;
static BLERemoteCharacteristic *pSettingsCharacteristic;
static BLERemoteCharacteristic *pAccountAndVerifyCharacteristic;
static BLERemoteCharacteristic *pSettingsResultsCharacteristic;

boolean mqttIsConnected()
{
  return mqttclient.connected();
}

boolean mqttReconnect()
{
  #ifndef MQTTUSER
  if (mqttclient.connect("BBQClient"))
  {
    ESP_LOGI("BBQ", "MQTT Connected");
  }
  #endif
  #ifdef MQTTUSER
  if (mqttclient.connect("BBQClient",MQTTUSER,MQTTPASS))
  {
    ESP_LOGI("BBQ", "MQTT Connected");
  }
  #endif
  else
  {
    ESP_LOGE("BBQ", "MQTT not Connected");
  }
  return mqttIsConnected();
}

void mqttLoop()
{
  if (!mqttclient.connected())
  {
    long now = millis();
    if (now - lastReconnectAttempt > 5000)
    {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      if (mqttReconnect())
      {
        lastReconnectAttempt = 0;
      }
    }
  }
  else
  {
    // Client connected
    mqttclient.loop();
  }
}

static void notifyCallback(
    BLERemoteCharacteristic *pBLERemoteCharacteristic,
    uint8_t *pData,
    size_t length,
    bool isNotify)
{
  uint8_t probeId = 1;
  for (int i = 0; i < length; i += 2)
  {
    uint16_t val = littleEndianInt(&pData[i]);
    float temp = val / 10;
    // convert C to F
    temp = (temp * 9.0/5.0) + 32.0;
    ESP_LOGI("BBQ", "Probe %d has value %f", probeId, temp);
    if(((int)temp)<PROBEERRORVALUE)
    {
      if (mqttIsConnected())
      {
        char temperature_out[255];
        sprintf(temperature_out, "%s/probe%d", MQTT_TOPIC_PREFIX, probeId);
        mqttclient.publish(temperature_out, String((int)temp).c_str());
        ESP_LOGI("BBQ", "Publish %d to topic %s", (int)temp, temperature_out);
      }
    }
    probeId++;
  }
}


// Batterylevel: https://github.com/sworisbreathing/go-ibbq/issues/2#issuecomment-650725433
int getiBBQBatteryPercentage(uint16_t current,double maxVoltage)
{
  //const int voltages[] = {5580, 5595, 5609, 5624, 5639, 5644, 5649, 5654, 5661, 5668, 5676, 5683, 5698, 5712, 5727, 5733, 5739, 5744, 5750, 5756, 5759, 5762, 5765, 5768, 5771, 5774, 5777, 5780, 5783, 5786, 5789, 5792, 5795, 5798, 5801, 5807, 5813, 5818, 5824, 5830, 5830, 5830, 5835, 5840, 5845, 5851, 5857, 5864, 5870, 5876, 5882, 5888, 5894, 5900, 5906, 5915, 5924, 5934, 5943, 5952, 5961, 5970, 5980, 5989, 5998, 6007, 6016, 6026, 6035, 6044, 6052, 6062, 6072, 6081, 6090, 6103, 6115, 6128, 6140, 6153, 6172, 6191, 6211, 6230, 6249, 6265, 6280, 6285, 6290, 6295, 6300, 6305, 6310, 6315, 6320, 6325, 6330, 6335, 6340, 6344};
  const uint16_t voltages[] = {5580, 5595, 5609, 5624, 5639, 5644, 5649, 5654, 5661, 5668, 5676, 5683, 5698, 5712, 5727, 5733, 5739, 5744, 5750, 5756, 5759, 5762, 5765, 5768, 5771, 5774, 5777, 5780, 5783, 5786, 5789, 5792, 5795, 5798, 5801, 5807, 5813, 5818, 5824, 5830, 5830, 5830, 5835, 5840, 5845, 5851, 5857, 5864, 5870, 5876, 5882, 5888, 5894, 5900, 5906, 5915, 5924, 5934, 5943, 5952, 5961, 5970, 5980, 5989, 5998, 6007, 6016, 6026, 6035, 6044, 6052, 6062, 6072, 6081, 6090, 6103, 6115, 6128, 6140, 6153, 6172, 6191, 6211, 6230, 6249, 6265, 6280, 6296, 6312, 6328, 6344, 6360, 6370, 6381, 6391, 6407, 6423, 6431, 6439, 6455};
  int length = sizeof(voltages)/sizeof(uint16_t);
  double factor = maxVoltage / 6550.0;

  if (current > voltages[length - 1]*factor)
  {
    return 100;
  }

  if (current <= voltages[0]*factor)
  {
    return 0;
  }
  int i = 0;
  while ((current > (voltages[i]*factor)) && (i < length))
  {
    if(i>100){return 100;}
    i++;
  }
  return i;
}

static void notifyResultsCallback(
    BLERemoteCharacteristic *pBLERemoteCharacteristic,
    uint8_t *pData,
    size_t length,
    bool isNotify)
{
  
  for (int i = 0; i < length; i++)
  {
    Serial.print(pData[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  switch (pData[0])
  {
  case 0x24:
  {
    uint16_t currentVoltage = littleEndianInt(&pData[1]); // up to maxVoltage
    uint16_t maxVoltage = littleEndianInt(&pData[3]);     // if 0 maxVoltage is 6550
    maxVoltage = maxVoltage == 0 ? 6550 : maxVoltage;
    double battery_percent = getiBBQBatteryPercentage(currentVoltage,maxVoltage);

    ESP_LOGI("BBQ", "currentVoltage %d::maxVoltage %d::perc %d", currentVoltage, maxVoltage, (int)battery_percent);
    if (mqttIsConnected())
    {
      char battery_level_out[255];
      sprintf(battery_level_out, "%s/%s", MQTT_TOPIC_PREFIX, "battery_percent");
      mqttclient.publish(battery_level_out, String((int)battery_percent).c_str());
      ESP_LOGI("BBQ", "Publish %f to topic %s", battery_percent, battery_level_out);
    }

    break;
  }
  default:
  {
    ESP_LOGE("BBQ", "unknown ID %X", pData[0]);
    break;
  }
  }
}

void getBatteryData()
{
  if (pSettingsCharacteristic != nullptr && pSettingsResultsCharacteristic != nullptr)
  {
    ESP_LOGI("BBQ", " Request BatteryStatus");
    pSettingsCharacteristic->writeValue((uint8_t *)batteryLevel, sizeof(batteryLevel), true);
  }
}

bool connectToBLEServer(BLEAddress pAddress)
{
  ESP_LOGI("BBQ", "Forming a connection to %s", pAddress.toString().c_str());

  BLEClient *pClient = BLEDevice::createClient();
  ESP_LOGI("BBQ", " - Created client");

  // Connect to the remove BLE Server.
  pClient->connect(pAddress);
  ESP_LOGI("BBQ", " - Connected to server");

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr)
  {
    ESP_LOGE("BBQ", "Failed to find our service UUID: %s", serviceUUID.toString().c_str());
    return false;
  }
  ESP_LOGI("BBQ", " - Found our service");

  pAccountAndVerifyCharacteristic = pRemoteService->getCharacteristic(AccountAndVerify);
  if (pAccountAndVerifyCharacteristic == nullptr)
  {
    ESP_LOGE("BBQ", "Failed to find our characteristic UUID: %s", AccountAndVerify.toString().c_str());
    return false;
  }
  ESP_LOGI("BBQ", " - Found our AccountAndVerify characteristic");
  ESP_LOGI("BBQ", " - AccountAndVerify");
  pAccountAndVerifyCharacteristic->writeValue((uint8_t *)credentials, sizeof(credentials), true);

  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pRemoteCharacteristic = pRemoteService->getCharacteristic(RealtimeData);
  if (pRemoteCharacteristic == nullptr)
  {
    ESP_LOGE("BBQ", "Failed to find our characteristic UUID: %s", RealtimeData.toString().c_str());
    return false;
  }
  ESP_LOGI("BBQ", " - Found our RealtimeData characteristic");

  pSettingsCharacteristic = pRemoteService->getCharacteristic(SettingsData);
  if (pSettingsCharacteristic == nullptr)
  {
    ESP_LOGE("BBQ", "Failed to find our characteristic UUID: %s", SettingsData.toString().c_str());
    return false;
  }

  ESP_LOGI("BBQ", " - Found our SettingsData characteristic");
  ESP_LOGI("BBQ", " - enableRealTimeData");
  pSettingsCharacteristic->writeValue((uint8_t *)enableRealTimeData, sizeof(enableRealTimeData), true);
  ESP_LOGI("BBQ", " - unitCelsius");
  pSettingsCharacteristic->writeValue((uint8_t *)unitCelsius, sizeof(unitCelsius), true);

  ESP_LOGI("BBQ", " - ADD notifyCallback");
  pRemoteCharacteristic->registerForNotify(notifyCallback);

  pSettingsResultsCharacteristic = pRemoteService->getCharacteristic(SettingsResults);
  if (pSettingsResultsCharacteristic == nullptr)
  {
    ESP_LOGE("BBQ", "Failed to find our characteristic UUID: %s", SettingsResults.toString().c_str());
    return false;
  }
  ESP_LOGI("BBQ", " - Found our SettingsResults characteristic");
  ESP_LOGI("BBQ", " - ADD notifyResultsCallback");
  pSettingsResultsCharacteristic->registerForNotify(notifyResultsCallback);

  getBatteryData();
  return true;
}

/**
   Scan for BLE servers and find the first one that advertises the service we are looking for.
*/
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
  /**
        Called for each advertising BLE server.
    */
  void onResult(BLEAdvertisedDevice advertisedDevice)
  {
    ESP_LOGV("BBQ", "BLE Advertised Device found: %s", advertisedDevice.toString().c_str());

    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.getServiceUUID().equals(serviceUUID))
    {

      //
      ESP_LOGI("BBQ", "Found our device!  address: ");
      advertisedDevice.getScan()->stop();

      pServerAddress = new BLEAddress(advertisedDevice.getAddress());
      doConnect = true;

    } // Found our server
  }   // onResult
};    // MyAdvertisedDeviceCallbacks

uint16_t littleEndianInt(uint8_t *pData)
{
  uint16_t val = pData[1] << 8;
  val = val | pData[0];
  return val;
}

uint16_t bigEndianInt(uint8_t *pData)
{
  uint16_t val = pData[0] << 8;
  val = val + pData[1];
  return val;
}

void ESPHardRestart()
{
  esp_task_wdt_init(1, true);
  esp_task_wdt_add(NULL);
  while (true)
    ;
}

void rebootEspWithReason(String reason)
{
  ESP_LOGE("BBQ", "RebootReason: %s", reason.c_str());
  delay(1000);
  ESPHardRestart();
}

bool wifiReConnect()
{
  int wifi_retry = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_retry < 5)
  {
    wifi_retry++;
    // Serial.println("WiFi not connected. Try to reconnect");
    ESP_LOGE("BBQ", "WiFi not connected. Try to reconnect");
    WiFi.disconnect();
    delay(200);
    WiFi.mode(WIFI_OFF);
    delay(200);
    WiFi.mode(WIFI_STA);
    delay(200);
    WiFi.begin(DEFAULT_SSID, DEFAULT_WIFIPASSWORD);
    delay(400);
  }
  return (WiFi.status() == WL_CONNECTED);
}


void ETHEvent(WiFiEvent_t event)
{
  char buf[20];
  switch (event)
  {
  case SYSTEM_EVENT_STA_CONNECTED:
    ESP_LOGE("BBQ", "WiFI Connected");
    break;
  case SYSTEM_EVENT_STA_GOT_IP:
    ESP_LOGD("BBQ", "-------------");
    // sprintf(buf, "%s", ETH.macAddress());
    // ESP_LOGD("BBQ","ETH MAC: %s\r\n",buf);
    sprintf(buf, "%d.%d.%d.%d", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);
    ESP_LOGD("BBQ", "IPv4: %s", buf);
    sprintf(buf, "%d.%d.%d.%d", WiFi.subnetMask()[0], WiFi.subnetMask()[1], WiFi.subnetMask()[2], WiFi.subnetMask()[3]);
    ESP_LOGD("BBQ", "Subnet: %s", buf);
    sprintf(buf, "%d.%d.%d.%d", WiFi.gatewayIP()[0], WiFi.gatewayIP()[1], WiFi.gatewayIP()[2], WiFi.gatewayIP()[3]);
    ESP_LOGD("BBQ", "Gateway: %s", buf);
    sprintf(buf, "%d.%d.%d.%d", WiFi.dnsIP()[0], WiFi.dnsIP()[1], WiFi.dnsIP()[2], WiFi.dnsIP()[3]);
    ESP_LOGD("BBQ", "DNS1: %s", buf);
    sprintf(buf, "%d.%d.%d.%d", WiFi.dnsIP(1)[0], WiFi.dnsIP(1)[1], WiFi.dnsIP(1)[2], WiFi.dnsIP(1)[3]);
    ESP_LOGD("BBQ", "DNS2: %s", buf);
    ESP_LOGD("BBQ", "-------------");
    mqttReconnect();
    ESP_LOGD("BBQ", "mqttReconnect()");
    break;
  case SYSTEM_EVENT_STA_DISCONNECTED:
    ESP_LOGE("BBQ", "STA Disconnected");
    wifiReConnect();
    //rebootEspWithReason("SYSTEM_EVENT_STA_DISCONNECTED");
    break;
  case SYSTEM_EVENT_STA_STOP:
    ESP_LOGE("BBQ", "SYSTEM_EVENT_STA_STOP");
    break;
  default:
    break;
  }
}

void setupWIFI()
{
  WiFi.onEvent(ETHEvent);
  wifiReConnect();
}

void setup()
{
  Serial.begin(9600);
  setLogLevel();

  setupWIFI();
  mqttclient.setServer(MQTTSERVER, MQTTPORT);
  lastReconnectAttempt = 0;
  ESP_LOGD("BBQ", "Scanning");
  BLEDevice::init("");

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 30 seconds.
  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->start(30);
}

static uint8_t batterycounter = 0;
void loop()
{

  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
  // connected we set the connected flag to be true.
  if (doConnect == true)
  {
    if (connectToBLEServer(*pServerAddress))
    {
      ESP_LOGI("BBQ", "We are now connected to the BLE Server.");
      connected = true;
    }
    else
    {
      ESP_LOGI("BBQ", "We have failed to connect to the server; there is nothin more we will do.");
    }
    doConnect = false;
  }

  mqttLoop();
  //request battery ever x sec
  if (batterycounter < 1)
  {
    getBatteryData();
    batterycounter = 10;
  }
  batterycounter--;
  delay(10000); // Delay a second between loops.
}
