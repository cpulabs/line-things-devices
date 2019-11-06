#include <Adafruit_BME280.h>
#include <Adafruit_LittleFS.h>
#include <Adafruit_Sensor.h>
#include <InternalFileSystem.h>
#include <Wire.h>
#include <bluefruit.h>
using namespace Adafruit_LittleFS_Namespace;

#define BLE_DEV_NAME "LINE Things smart hanger"
#define FIRMWARE_VERSION 1

// Debug On / Off
#define USER_DEBUG

#define SEALEVELPRESSURE_HPA (1013.25)
#define BLE_MAX_PRPH_CONNECTION 3
#define PREDICT_START_TIME 5//600          // After this time start predict time for dry(Second).

#define STT_IDLE 0
#define STT_WORKING 1
#define STT_DONE 2


//{"id":"6590511685983488588","name":"LINE Things Smart Hanger","type":"BLE","channelId":1653346008,"actionUri":"line://app/1653346008-blqyppy6","serviceUuid":"6ff28a9e-787a-42d0-a56d-4185eb433bcc","psdiServiceUuid":"e625601e-9e55-4597-a598-76018a0d293d","psdiCharacteristicUuid":"26e2b12b-85f0-4f3f-9fdd-91d114270e6e"}w022571808515m-4980:smart_hanger JP24538$

typedef struct dry_profile {
    float temp;
    float humidity;
    int pressure;
    int device_id;
} dryValue;

typedef struct sensor_value {
    int battery;
    float temp;
    float humidity;
    float pressure;
    float altitude;
} sensorValue;

/*********************************************************************************
 * Internal config file
 *********************************************************************************/
#define FILENAME "/config_hanger.txt"
File file(InternalFS);

/*********************************************************************************
 * IO
 *********************************************************************************/
#define IO_BATTERY 28
#define IO_SW 27
#define IO_LED 2

/*********************************************************************************
 * Debug print
 *********************************************************************************/
void debugPrint(String text) {
#ifdef USER_DEBUG
    text = "[DBG]" + text;
    Serial.println(text);
#endif
}

/*********************************************************************************
 * Callback
 *********************************************************************************/
SoftwareTimer ledControl;
void ledControlEvent(TimerHandle_t xTimerID) {
    digitalWrite(IO_LED, !digitalRead(IO_LED));
}

volatile bool g_start_from_switch = false;
void swChangedEvent() {
    g_start_from_switch = true;
}

/*********************************************************************************
 * UUID Configure data
 *********************************************************************************/
void profileFileWrite(dryValue profile) {
    int i = 0;
    if (file.open(FILENAME, FILE_O_WRITE)) {
        file.seek(0);
        int16_t configdata[4] = {profile.temp,     profile.humidity,
                                 profile.pressure, profile.device_id};
        file.write((uint8_t *) configdata, sizeof(configdata));
        file.close();
        debugPrint("[Flash] Write profile file : done");
    } else {
        debugPrint("[Flash][ERROR] Write profile file : Failed!");
    }
}

void profileFileRead(dryValue *profile) {
    int16_t configdata[4];
    file.open(FILENAME, FILE_O_READ);
    file.read(configdata, sizeof(configdata));
    file.close();

    profile->temp = configdata[0];
    profile->humidity = configdata[1];
    profile->pressure = configdata[2];
    profile->device_id = configdata[3];
}

int profileFileExist() {
    file.open(FILENAME, FILE_O_READ);
    if (!file) {
        file.close();
        return -1;
    }
    file.close();
    return 0;
}

/*********************************************************************************
BLE settings
*********************************************************************************/
#define USER_SERVICE_UUID "6ff28a9e-787a-42d0-a56d-4185eb433bcc"
#define USER_CHARACTERISTIC_STATUS_READ_UUID "441f6ded-7e95-4552-93c4-c8f3b9823ffd"
#define USER_CHARACTERISTIC_SETTINGS_WRITE_UUID "e217a2cd-0243-4701-a5d3-20ec8eb52600"
#define USER_CHARACTERISTIC_INFO_NOTIFY_UUID "0de64328-b597-4263-b2b0-2bf2f16ea4c8"
#define PSDI_SERVICE_UUID "e625601e-9e55-4597-a598-76018a0d293d"
#define PSDI_CHARACTERISTIC_UUID "26e2b12b-85f0-4f3f-9fdd-91d114270e6e"

// LINE PSDI
uint8_t blesv_line_uuid[16];
uint8_t blesv_line_product_uuid[16];
BLEService blesv_line = BLEService(blesv_line_uuid);
BLECharacteristic blesv_line_product = BLECharacteristic(
    blesv_line_product_uuid);  // Product Specific(Read, 16Byte)

// User Service
uint8_t blesv_user_uuid[16];
uint8_t blesv_user_read_uuid[16];
uint8_t blesv_user_write_uuid[16];
uint8_t blesv_user_notify_uuid[16];
BLEService blesv_user = BLEService(blesv_user_uuid);
BLECharacteristic blesv_user_read = BLECharacteristic(blesv_user_read_uuid);
BLECharacteristic blesv_user_write = BLECharacteristic(blesv_user_write_uuid);
BLECharacteristic blesv_user_notify = BLECharacteristic(blesv_user_notify_uuid);

// UUID Converter
void strUUID2Bytes(String strUUID, uint8_t binUUID[]) {
    String hexString = String(strUUID);
    hexString.replace("-", "");

    for (int i = 16; i != 0; i--) {
        binUUID[i - 1] =
            hex2c(hexString[(16 - i) * 2], hexString[((16 - i) * 2) + 1]);
    }
}

char hex2c(char c1, char c2) {
    return (nibble2c(c1) << 4) + nibble2c(c2);
}

char nibble2c(char c) {
    if ((c >= '0') && (c <= '9')) return c - '0';
    if ((c >= 'A') && (c <= 'F')) return c + 10 - 'A';
    if ((c >= 'a') && (c <= 'f')) return c + 10 - 'a';
    return 0;
}

void bleConfigure() {
    // UUID setup
    strUUID2Bytes(PSDI_SERVICE_UUID, blesv_line_uuid);
    strUUID2Bytes(PSDI_CHARACTERISTIC_UUID, blesv_line_product_uuid);
    strUUID2Bytes(USER_SERVICE_UUID, blesv_user_uuid);
    strUUID2Bytes(USER_CHARACTERISTIC_STATUS_READ_UUID, blesv_user_read_uuid);
    strUUID2Bytes(USER_CHARACTERISTIC_SETTINGS_WRITE_UUID, blesv_user_write_uuid);
    strUUID2Bytes(USER_CHARACTERISTIC_INFO_NOTIFY_UUID, blesv_user_notify_uuid);

    // BLE start
    Bluefruit.begin(BLE_MAX_PRPH_CONNECTION, 0);
    // BEL set power
    Bluefruit.setTxPower(-16);  // Set max power. Accepted values are: -40, -30,
                                // -20, -16, -12, -8, -4, 0, 4
    // BLE devicename
    Bluefruit.setName(BLE_DEV_NAME);
    Bluefruit.Periph.setConnInterval(
        12, 1600);  // connection interval min=20ms, max=2s

    // Set the connect/disconnect callback handlers
    Bluefruit.Periph.setConnectCallback(event_ble_connect);
    Bluefruit.Periph.setDisconnectCallback(event_ble_disconnect);
}

void bleAdvert_start(void) {
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.setFastTimeout(30);
    Bluefruit.Advertising.setInterval(
        32, 32);  // interval : fast=20ms, slow:32ms(unit of 0.625ms)
    Bluefruit.Advertising.restartOnDisconnect(true);

    // Addition Service UUID
    Bluefruit.Advertising.addService(
        blesv_user);  // LINE app側で発見するためにUser service
                      // UUIDを必ずアドバタイズパケットに含める
    Bluefruit.ScanResponse.addName();
    // Start
    Bluefruit.Advertising.start();
}

void bleServicePsdi_setup(void) {
    blesv_line.begin();
    blesv_line_product.setProperties(CHR_PROPS_READ);
    blesv_line_product.setPermission(SECMODE_ENC_NO_MITM, SECMODE_ENC_NO_MITM);
    blesv_line_product.setFixedLen(sizeof(uint32_t) * 2);
    blesv_line_product.begin();
    uint32_t deviceAddr[] = {NRF_FICR->DEVICEADDR[0], NRF_FICR->DEVICEADDR[1]};
    blesv_line_product.write(deviceAddr, sizeof(deviceAddr));
}

void bleServiceUser_setup() {
    blesv_user.begin();

    blesv_user_notify.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
    blesv_user_notify.setPermission(SECMODE_ENC_NO_MITM, SECMODE_ENC_NO_MITM);
    blesv_user_notify.setFixedLen(3);
    blesv_user_notify.begin();

    blesv_user_read.setProperties(CHR_PROPS_READ);
    blesv_user_read.setPermission(SECMODE_ENC_NO_MITM, SECMODE_ENC_NO_MITM);
    blesv_user_read.setFixedLen(16);
    blesv_user_read.begin();

    blesv_user_write.setProperties(CHR_PROPS_WRITE);
    blesv_user_write.setPermission(SECMODE_ENC_NO_MITM, SECMODE_ENC_NO_MITM);
    blesv_user_write.setWriteCallback(event_ble_write);
    blesv_user_write.setFixedLen(1);
    blesv_user_write.begin();
}

// Event for connect BLE central
volatile int connection_count = 0;
volatile uint16_t last_connect_hdl = 0;
volatile int new_connection_flag = 0;
void event_ble_connect(uint16_t conn_handle) {
    char central_name[32] = {0};

    // connection_count++;
    connection_count = (uint8_t) Bluefruit.Periph.connected();

    BLEConnection *connection = Bluefruit.Connection(conn_handle);
    connection->getPeerName(central_name, sizeof(central_name));

    String msg = "[BLE]Connected from " + String(central_name) +
                 ". Available connection count " + String(connection_count);
    ;
    debugPrint(msg);

    ledControl.start();

    last_connect_hdl = conn_handle;
    new_connection_flag = 1;

    if (connection_count < BLE_MAX_PRPH_CONNECTION) {
        Bluefruit.Advertising.start(0);
    }
}

// Event for disconnect BLE central
void event_ble_disconnect(uint16_t conn_handle, uint8_t reason) {
    char central_name[32] = {0};

    // connection_count--;
    connection_count = (uint8_t) Bluefruit.Periph.connected();

    BLEConnection *connection = Bluefruit.Connection(conn_handle);
    connection->getPeerName(central_name, sizeof(central_name));

    String msg = "[BLE]Disconnect from " + String(central_name) +
                 ". Available connection count " + String(connection_count);
    debugPrint(msg);

    if(connection_count == 0){
        ledControl.stop();
    }

    Bluefruit.Advertising.start(0);
}


volatile bool g_ble_cmd_req_reset = false;
volatile bool g_ble_cmd_req_setconfig = false;
volatile bool g_ble_cmd_req_start = false;
volatile bool g_ble_cmd_change_device_id = false;
volatile int g_ble_request_device_id = 0;


void event_ble_write(uint16_t conn_handle, BLECharacteristic *chr,
                     uint8_t *data, uint16_t len) {
    switch(data[0]){
      case 0: // Reset devices
          g_ble_cmd_req_reset = true;
          break;
      case 1: // Set configure
          g_ble_cmd_req_setconfig = true;
          break;
      case 2: // Start smart hanger manager
          g_ble_cmd_req_start = true;
          break;
      default:
        if(data[0] >= 4 && data[0] < 132){
          debugPrint("[BLE]Change device ID.");
          g_ble_cmd_change_device_id = true;
          g_ble_request_device_id = data[0] - 4;
        }else{
          debugPrint("[BLE]request command failed.");
        }
        break;
    }
}

/*********************************************************************************
Sensor
*********************************************************************************/
uint8_t getBatteryLevel() {
    int baterry_data = 0;
    uint8_t result;
    int battery_data;

    // Load Battery value | 10bit @1.2Vref
    battery_data = analogRead(IO_BATTERY);
    battery_data += analogRead(IO_BATTERY);
    battery_data += analogRead(IO_BATTERY);
    battery_data += analogRead(IO_BATTERY);
    battery_data = battery_data / 4;

    if (battery_data < 682) {  // when baterry < 2.4V
        result = 0;
    } else if (battery_data > 853) {
        result = 100;
    } else {
        result = map(battery_data, 682, 853, 0, 100);
    }
    return result;
}

Adafruit_BME280 bme;

/*********************************************************************************
Setup
*********************************************************************************/
void setup() {
    dryValue profile;

    // Pin config
    pinMode(IO_LED, OUTPUT);
    digitalWrite(IO_LED, 0);
    pinMode(IO_SW, INPUT_PULLUP);

    // Interrupt
    attachInterrupt(IO_SW, swChangedEvent, RISING);

    // Serial通信初期化
    Serial.begin(115200);

    // Enable low power feature
    sd_power_mode_set(NRF_POWER_MODE_LOWPWR);
    sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);
    delay(500);

    // Sensor init
    if (!bme.begin(BME280_ADDRESS_ALTERNATE, &Wire)) {
        debugPrint("Could not find a valid BME280 sensor, check wiring!");
        while (1)
            ;
    }
    bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                    Adafruit_BME280::SAMPLING_X2,   // temperature
                    Adafruit_BME280::SAMPLING_X16,  // pressure
                    Adafruit_BME280::SAMPLING_X1,   // humidity
                    Adafruit_BME280::FILTER_X16,
                    Adafruit_BME280::STANDBY_MS_0_5);

    // ADC setup for battery
    analogReference(AR_INTERNAL_1_2);  // ADC reference = 1.2V
    analogReadResolution(10);          // ADC 10bit

    // BLEの設定
    bleConfigure();
    bleServicePsdi_setup();
    bleServiceUser_setup();
    bleAdvert_start();

    // profile file
    if (profileFileExist() == -1) {
        profile.temp = 0;
        profile.humidity = 0;
        profile.pressure = 0;
        profile.device_id = 0;
        profileFileWrite(profile);
    } else {
        profileFileRead(&profile);
    }

    debugPrint("-----------------------------");
    debugPrint("Show parameter");
    debugPrint("User device id : " + String(profile.device_id));
    debugPrint("Temp : " + String(profile.temp) + "");
    debugPrint("Humidity : " + String(profile.humidity) + "%");
    debugPrint("Pressure : " + String(profile.pressure) + "hPa");
    debugPrint("-----------------------------");

    // Configure Timer
    ledControl.begin(500, ledControlEvent);

    debugPrint("[Initial]Init done");
    user_loop(profile);
}

unsigned int dry_check(dryValue profile, sensorValue value_start, sensorValue value_now){
    int progress = map(value_now.humidity, profile.humidity, value_start.humidity, 100, 0);
    if(progress < 0){
      progress = 0;
    }
    if(progress >= 100){
      progress = 100;
    }

    debugPrint("-----------------------------");
    debugPrint("Progress report");
    debugPrint("Progress : " + String(progress) + "%");
    debugPrint("Humidity[Current] : " + String(value_now.humidity) + "%");
    debugPrint("Humidity[Dry]     : " + String(profile.humidity) + "%");
    debugPrint("-----------------------------");

    return progress;
}

unsigned int predict(unsigned long time, unsigned int progress){
    if(time == 0 || progress == 0){
      debugPrint("-----------------------------");
      debugPrint("Progress report[TIME]");
      debugPrint("Can not guess time");
      debugPrint("-----------------------------");
      return 65535;
    }
    double progress_1sec = ((double)progress / time);
    double predect_time = (100 - progress) / progress_1sec;

    debugPrint("-----------------------------");
    debugPrint("Progress report[TIME]");
    debugPrint("Progress : " + String(progress) + "%");
    debugPrint("Spend time : " + String(time) + "sec");
    debugPrint("Predect time : " + String(predect_time / 60) + "min");
    debugPrint("Progress per sec : " + String(progress_1sec) + "%");
    debugPrint("-----------------------------");

    if(predect_time >= 65535){
        return 65535;
    }
    return (unsigned int)predect_time;
}

bool wetClothesAutoDetect(dryValue profile, sensorValue value){
  float offset = 18;
  if((profile.humidity + offset) < value.humidity){
    return true;
  }
  return false;
}

void user_loop(dryValue profile) {
    sensorValue value_now;
    sensorValue value_start;

    int state = 0;
    int16_t tx_frame[8];

    float delta_humidity = 0;
    float delta_delta = 0;
    unsigned long start_time;
    unsigned long spend_time;

    bool notify_request = false;
    bool id_changed = false;
    bool system_reset = false;
    bool autoDetect = false;

    unsigned int progress;
    unsigned int predict_time = 0;
    bool dry_status;
    

    while (1) {
        if(g_ble_cmd_req_reset){
            debugPrint("[Status changed]System Reset");
            g_ble_cmd_req_reset = false;
            state = STT_IDLE;
            system_reset = true;
        }

        // Change device id
        if(g_ble_cmd_change_device_id){
          g_ble_cmd_change_device_id = false;
          id_changed = true;
          profile.device_id = g_ble_request_device_id;
          profile.temp = profile.temp;
          profile.pressure = profile.pressure;
          profile.humidity = profile.humidity;
          profileFileWrite(profile);
          //Read Profile
          dryValue tmp;
          profileFileRead(&tmp);
          debugPrint("-----------------------------");
          debugPrint("Load parameter");
          debugPrint("User device id : " + String(profile.device_id));
          debugPrint("Temp : " + String(tmp.temp) + "");
          debugPrint("Humidity : " + String(tmp.humidity) + "%");
          debugPrint("Pressure : " + String(tmp.pressure) + "hPa");
          debugPrint("-----------------------------");
        }

        // Senser Read
        value_now.battery = getBatteryLevel();
        value_now.temp = bme.readTemperature();
        value_now.pressure = bme.readPressure() / 100.0F;
        value_now.humidity = bme.readHumidity();
        value_now.altitude = bme.readAltitude(SEALEVELPRESSURE_HPA);

        debugPrint("-----------------------------");
        debugPrint("Current state");
        debugPrint("State : " + String(state) + "");
        debugPrint("Current sensor value");
        debugPrint("Temp : " + String(value_now.temp) + "");
        debugPrint("Humidity : " + String(value_now.humidity) + "%");
        debugPrint("Pressure : " + String(value_now.pressure) + "hPa");
        debugPrint("Altitude : " + String(value_now.altitude) + "hPa");
        debugPrint("-----------------------------");

        // Transmit data
        tx_frame[0] = tx_frame[0];
        tx_frame[1] = 0;
        tx_frame[2] = profile.temp * 100;
        tx_frame[3] = profile.humidity * 100;
        tx_frame[4] = value_now.temp * 100;
        tx_frame[5] = value_now.humidity * 100;
        tx_frame[6] = value_now.battery;
        tx_frame[7] = profile.device_id;

        // Spend time
        spend_time = (millis() / 1000) - start_time;

        // Notify
        if(notify_request || value_now.battery <= 10 || id_changed || system_reset){
            uint8_t notify_data[3];
            notify_data[0] = profile.device_id;
            notify_data[1] = (value_now.battery <= 10)? 1 : 0;
            notify_data[2] = (notify_request)? 1 : 0;

            for (uint8_t conn_hdl = 0;
                conn_hdl < BLE_MAX_PRPH_CONNECTION; conn_hdl++) {
                blesv_user_notify.notify(conn_hdl, (uint8_t *) notify_data, 3);
            }
            notify_request = false;
            id_changed = false;
            system_reset = false;
        }

        switch(state){
            case STT_IDLE:
                // Update Status
                tx_frame[0] = 0;
                blesv_user_read.write((uint8_t *) tx_frame,
                                         sizeof(tx_frame));

                // Request set dry profile
                if(g_ble_cmd_req_setconfig){
                    g_ble_cmd_req_setconfig = false;
                    profile.device_id = profile.device_id;
                    profile.temp = value_now.temp;
                    profile.pressure = value_now.pressure;
                    profile.humidity = value_now.humidity;
                    profileFileWrite(profile);
                    //Read Profile
                    dryValue tmp;
                    profileFileRead(&tmp);
                    debugPrint("-----------------------------");
                    debugPrint("Load parameter");
                    debugPrint("User device id : " + String(profile.device_id));
                    debugPrint("Temp : " + String(tmp.temp) + "");
                    debugPrint("Humidity : " + String(tmp.humidity) + "%");
                    debugPrint("Pressure : " + String(tmp.pressure) + "hPa");
                    debugPrint("-----------------------------");
                }

                // request start
                autoDetect = wetClothesAutoDetect(profile, value_now);
                if(autoDetect){
                  Serial.println("Wet clothes auto detected.");
                  Serial.println("Start...");
                }
                if(g_ble_cmd_req_start || g_start_from_switch || autoDetect){
                    // Transmit via BLE
                    tx_frame[0] = 1;
                    tx_frame[1] = 65535 / 60; //change to minute
                    blesv_user_read.write((uint8_t *) tx_frame,
                                                 sizeof(tx_frame));

                    g_ble_cmd_req_start = false;
                    g_start_from_switch = false;
                    debugPrint("[Status changed]IDLE to WORKING");
                    start_time = millis() / 1000;
                    value_start = value_now;
                    for(int i = 0; i < 10; i++){
                      delay(6000);
                    }
                    debugPrint("Start");
                    state = STT_WORKING;
                }

                delay(1000);
                break;
            case STT_WORKING:
                // Check dry or not
                progress = dry_check(profile, value_start, value_now);
                if(progress >= 90){
                  dry_status = true;
                }else{
                  dry_status = false;
                }

                if(spend_time > PREDICT_START_TIME){
                  Serial.println("PROGRESS : " + String(progress));
                  predict_time = predict(spend_time, progress);
                  Serial.println("PROGRESS[TIME] : " + String(predict_time));
                }

                // Transmit via BLE
                tx_frame[0] = 1;
                tx_frame[1] = predict_time / 60; //change to minute
                blesv_user_read.write((uint8_t *) tx_frame,
                                             sizeof(tx_frame));

                //Status Change
                if(dry_status){
                    tx_frame[0] = 2; // status change to done
                    tx_frame[0] = 0;
                    blesv_user_read.write((uint8_t *) tx_frame,
                                                 sizeof(tx_frame));
                    state = STT_DONE;
                    debugPrint("[Status changed]WORKING to DONE");
                    /*
                    debugPrint("Set new dry parameter");
                    profile.temp = value_now.temp;
                    profile.pressure = value_now.pressure;
                    profile.humidity = value_now.humidity;
                    profileFileWrite(profile);
                    */
                }

                delay(5000);
                break;
            case STT_DONE:
                // Wait for connect ble
                if(!Bluefruit.connected()){
                    delay(2000);
                    break;
                }
                //digitalWrite(IO_LED, HIGH);
                delay(5000);

                // Notify status;
                //digitalWrite(IO_LED, LOW);
                tx_frame[0] = 0; // status change to idle
                blesv_user_read.write((uint8_t *) tx_frame,
                                             sizeof(tx_frame));
                notify_request = true;
                state = STT_IDLE;
                g_ble_cmd_req_start = false;
                g_start_from_switch = false;
                debugPrint("[Status changed]DONE to INITIAL");
                break;

            default:
                tx_frame[0] = 0; // status change to idle
                blesv_user_read.write((uint8_t *) tx_frame,
                                             sizeof(tx_frame));
                debugPrint("[ERROR]set undefined state");
                state = STT_IDLE;
                break;
        }
    }
}

void loop() {
}
