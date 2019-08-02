
#include <Wire.h>
#include <bluefruit.h>
#include <utility/bonding.h>

#define BLE_DEV_NAME "LINE Things crane game"
#define FIRMWARE_VERSION 1
#define MAX_PLAYING_TIME 55  // Second
#define BLE_MAX_PRPH_CONNECTION 1

// Debug On / Off
#define USER_DEBUG

/*********************************************************************************
 * IO
 *********************************************************************************/
#define IO_COIN_DETECT 27
#define IO_CAPSEL_DETECT 28
#define IO_SYSTEM_READY 15
#define IO_ARM_POWER 14

#define IO_RELAY1_DIR 2
#define IO_RELAY1_COM 3
#define IO_RELAY2_DIR 4
#define IO_RELAY2_COM 5
#define IO_RELAY3_DIR 12
#define IO_RELAY3_COM 13

/*********************************************************************************
 * BLE Command
 *********************************************************************************/
#define DEVICE_CMD_ARM_X 1
#define DEVICE_CMD_ARM_Y 2
#define DEVICE_CMD_LIFF_TIMEOUT 4

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
 * software time callback
 *********************************************************************************/
volatile int g_notify_flag = 0;
SoftwareTimer timerNotify;
void notifyTiming(TimerHandle_t xTimerID) {
    g_notify_flag = 1;
}

volatile unsigned int g_playing_time = 0;
SoftwareTimer playingTime;
void playingTimeUpdateEvent(TimerHandle_t xTimerID) {
    g_playing_time++;
}

volatile int g_reload_request_flag;
void coinDetectEvent() {
    g_reload_request_flag = 1;
    // debugPrint("Coin Insert");
}

/*********************************************************************************
BLE settings
*********************************************************************************/
#define USER_SERVICE_UUID "9b468a8f-d18b-45a7-b0a5-d48cb3c1004f"
#define USER_CHARACTERISTIC_WRITE_UUID "e7024f7b-c61b-46bb-8690-e24c743e9b52"
#define USER_CHARACTERISTIC_NOTIFY_UUID "82d23d9a-91b9-4933-96b0-966a148e9a43"
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
uint8_t blesv_user_write_uuid[16];
uint8_t blesv_user_notify_uuid[16];
BLEService blesv_user = BLEService(blesv_user_uuid);
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
    strUUID2Bytes(USER_CHARACTERISTIC_WRITE_UUID, blesv_user_write_uuid);
    strUUID2Bytes(USER_CHARACTERISTIC_NOTIFY_UUID, blesv_user_notify_uuid);

    // BLE start
    Bluefruit.begin();
    // BEL set power
    Bluefruit.setTxPower(4);  // Set max power. Accepted values are: -40, -30,
                              // -20, -16, -12, -8, -4, 0, 4

    // BLE devicename
    Bluefruit.setName(BLE_DEV_NAME);
    Bluefruit.Periph.setConnInterval(
        9, 24);  // connection interval min=20ms, max=2s

    // Set the connect/disconnect callback handlers
    Bluefruit.Periph.setConnectCallback(event_ble_connect);
    Bluefruit.Periph.setDisconnectCallback(event_ble_disconnect);
}

void bleAdvert_start(void) {
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.setFastTimeout(30);
    Bluefruit.Advertising.setFastTimeout(0);
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

    blesv_user_notify.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    blesv_user_notify.setPermission(SECMODE_ENC_NO_MITM, SECMODE_ENC_NO_MITM);
    blesv_user_notify.setFixedLen(1);
    blesv_user_notify.begin();

    blesv_user_write.setProperties(CHR_PROPS_WRITE);
    blesv_user_write.setPermission(SECMODE_ENC_NO_MITM, SECMODE_ENC_NO_MITM);
    blesv_user_write.setWriteCallback(event_ble_write);
    blesv_user_write.setFixedLen(2);
    blesv_user_write.begin();
}

// Event for connect BLE central
volatile int connection_count = 0;
volatile uint16_t last_connect_hdl = 0;
volatile int new_connection_flag = 0;
void event_ble_connect(uint16_t conn_handle) {
    char central_name[32] = {0};

    connection_count = (uint8_t) Bluefruit.Periph.connected();

    BLEConnection *connection = Bluefruit.Connection(conn_handle);
    connection->getPeerName(central_name, sizeof(central_name));

    String msg = "[BLE]Connected from " + String(central_name) +
                 ". Available connection count " + String(connection_count);
    ;
    debugPrint(msg);
    last_connect_hdl = conn_handle;

    // Notiry connected central count
    /*
    if (connection_count > 1 && connection_count < 4) {
        blesv_user_notify.notify8(conn_handle, connection_count - 1);
    }

    if (connection_count < BLE_MAX_PRPH_CONNECTION) {
        // new_connection_flag = 1;
        Bluefruit.Advertising.start(0);
    }
    */
}

// Event for disconnect BLE central
void event_ble_disconnect(uint16_t conn_handle, uint8_t reason) {
    String msg = "[BLE]Disconnect host";
    debugPrint(msg);

    g_reload_request_flag = 0;
    // Bluefruit.Advertising.start(0);
}

typedef struct ble_write_action {
    byte changed = 0;
    byte action = 0;
    byte value = 0;
} bleWriteAction;

volatile bleWriteAction g_write_action;

void event_ble_write(uint16_t conn_handle, BLECharacteristic *chr,
                     uint8_t *data, uint16_t len) {
    byte cmd = data[0];
    byte value = data[1];

    g_write_action.changed = 1;
    g_write_action.action = cmd;
    g_write_action.value = value;

    debugPrint("[BLE] Write");
}

#define ARM_Y_DIR_BACK 0
#define ARM_Y_DIR_FRONT 1
#define ARM_X_DIR_RIGHT 0
#define ARM_X_DIR_LEFT 1
#define ARM_Z_DIR_DOWN 1
#define ARM_Z_DIR_UP 0

/*********************************************************************************
Setup
*********************************************************************************/
void setup() {
    // Pin config
    digitalWrite(IO_RELAY1_DIR, 0);
    pinMode(IO_RELAY1_DIR, OUTPUT);
    digitalWrite(IO_RELAY1_COM, 0);
    pinMode(IO_RELAY1_COM, OUTPUT);

    digitalWrite(IO_RELAY2_DIR, 0);
    pinMode(IO_RELAY2_DIR, OUTPUT);
    digitalWrite(IO_RELAY2_COM, 0);
    pinMode(IO_RELAY2_COM, OUTPUT);

    digitalWrite(IO_RELAY3_DIR, 0);
    pinMode(IO_RELAY3_DIR, OUTPUT);
    digitalWrite(IO_RELAY3_COM, 0);
    pinMode(IO_RELAY3_COM, OUTPUT);

    digitalWrite(IO_ARM_POWER, 1);
    pinMode(IO_ARM_POWER, OUTPUT);

    pinMode(IO_COIN_DETECT, INPUT);
    pinMode(IO_CAPSEL_DETECT, INPUT);

    pinMode(IO_SYSTEM_READY, OUTPUT);
    digitalWrite(IO_SYSTEM_READY, 0);

    // Interrupt
    // attachInterrupt(IO_COIN_DETECT, coinDetectEvent, RISING);

    // Serial Init
    Serial.begin(115200);
    delay(1000);

    // BLE Init
    bleConfigure();
    bleServicePsdi_setup();
    bleServiceUser_setup();
    bleAdvert_start();
    blesv_user_notify.notify8(0);  // Reset

    /*
    Serial.println("Clear bonding data\n");
    Bluefruit.clearBonds();
    Bluefruit.Central.clearBonds();
    bond_print_list(BLE_GAP_ROLE_PERIPH);
    bond_print_list(BLE_GAP_ROLE_CENTRAL);
    */

    // Configure Timer
    playingTime.begin(1000, playingTimeUpdateEvent);

    // Reset position
    arm_x_pos_reset();
    arm_y_pos_reset();
    arm_z_pos_reset();

    // Crane machine reset
    digitalWrite(IO_ARM_POWER, 0);
    delay(500);
    digitalWrite(IO_ARM_POWER, 1);
    delay(500);
    debugPrint("Init done");
}

void arm_z_pos_drop_and_reset() {
    arm_z_control(ARM_Z_DIR_DOWN, 1);
    delay(1000);
    arm_z_control(ARM_Z_DIR_UP, 1);
    delay(1000);
    arm_z_control(0, 0);
}

void arm_z_pos_catch_and_reset() {
    arm_z_control(ARM_Z_DIR_DOWN, 1);
    delay(3000);
    arm_z_control(ARM_Z_DIR_UP, 1);
    delay(3500);
    arm_z_control(0, 0);
}

void arm_z_pos_short_reset() {
    arm_z_control(ARM_Z_DIR_UP, 1);
    delay(1500);
    arm_z_control(0, 0);
}

void arm_z_pos_reset() {
    arm_z_control(ARM_Z_DIR_UP, 1);
    delay(3500);
    arm_z_control(0, 0);
}

void arm_x_pos_reset() {
    arm_x_control(ARM_X_DIR_LEFT, 1);
    delay(6000);
    arm_x_control(0, 0);
}

void arm_y_pos_reset() {
    arm_y_control(ARM_Y_DIR_FRONT, 1);
    delay(6000);
    arm_y_control(0, 0);
}

void arm_y_control(int dir, bool ena) {
    digitalWrite(IO_RELAY1_DIR, dir);
    digitalWrite(IO_RELAY1_COM, ena);
}

void arm_x_control(int dir, bool ena) {
    digitalWrite(IO_RELAY2_DIR, dir);
    digitalWrite(IO_RELAY2_COM, ena);
}

void arm_z_control(int dir, bool ena) {
    digitalWrite(IO_RELAY3_DIR, dir);
    digitalWrite(IO_RELAY3_COM, ena);
}

#define NOTIFY_REQ_INSERT_COIN 1
#define NOTIFY_READY 2

void loop() {
    char player_name[32] = {0};
    // Init
    digitalWrite(IO_SYSTEM_READY, 1);
    debugPrint("-----------------------------------------------------");
    debugPrint("[INFO]System Ready. Waiting for connect BLE central.");
    // debugPrint("[INFO]Line up host : " +
    // String(Bluefruit.Periph.connected()));

    // 遊び始めるとき
    while (1) {
        if (Bluefruit.connected()) {
            break;
        }
        delay(200);
    }
    String msg = "[BLE]Connected host";
    debugPrint(msg);

    // Initializing
    g_write_action.changed = 0;

    // ゲームを始める
    delay(1000);
    digitalWrite(IO_SYSTEM_READY, 0);
    playGame();

    delay(500);
    // Start advertising
    // Bluefruit.Advertising.start(0);
}

bool checkOverPlayingTime() {
    if (g_playing_time > MAX_PLAYING_TIME) {
        return true;
    }
    return false;
}

void playGame() {
    int resp = 0;

    // Crane machine reset
    digitalWrite(IO_ARM_POWER, 0);
    delay(500);
    digitalWrite(IO_ARM_POWER, 1);
    delay(500);

    // Notify coin insert information
    blesv_user_notify.notify8(NOTIFY_REQ_INSERT_COIN);

    // waiting for insert coint
    while (digitalRead(IO_COIN_DETECT) == 1) {
        delay(5);
        if (!Bluefruit.connected()) {
            debugPrint("[INFO]Player disconnect.");
            return;
        }
    }
    g_playing_time = 0;
    playingTime.start();

    debugPrint("[INFO]Reset position automaticaly.");
    arm_z_pos_short_reset();

    debugPrint("[INFO]Start game.");
    blesv_user_notify.notify8(NOTIFY_READY);

    // BLE control
    resp = playGameBleControl();
    blesv_user_notify.notify8(0);  // Reset LIFF state

    playingTime.stop();
    g_playing_time = 0;

    // Set to initial position
    arm_x_pos_reset();
    arm_y_pos_reset();

    if (resp == -1) {
        debugPrint("[INFO]Player disconnect.");
        // Reset position
        digitalWrite(IO_ARM_POWER, 0);
        delay(500);
        digitalWrite(IO_ARM_POWER, 1);
        delay(500);
        return;
    } else if (resp == -2) {
        debugPrint("[INFO]Player timeout.");
    }

    // Drop toy
    arm_z_pos_drop_and_reset();

    // Crane machine reset
    digitalWrite(IO_ARM_POWER, 0);
    delay(500);
    digitalWrite(IO_ARM_POWER, 1);
    delay(500);

    // Auotmation Close
    BLEConnection *connection = Bluefruit.Connection(last_connect_hdl);
    if (connection->connected() == true) {
        connection->disconnect();
    }
    debugPrint("[INFO]Disconnect to central automatic.");
}

int playGameBleControl() {
    // waiting for start X controls
    while (!(g_write_action.changed &&
             g_write_action.action == DEVICE_CMD_ARM_X &&
             g_write_action.value == 1)) {
        // Player disconnect
        if (!Bluefruit.connected()) {
            return -1;
        }
        // Time out
        if (g_write_action.changed &&
            g_write_action.action == DEVICE_CMD_LIFF_TIMEOUT &&
            g_write_action.value == 1) {
            return -2;
        }
    }

    arm_x_control(ARM_X_DIR_RIGHT, 1);
    g_write_action.changed = 0;
    if (checkOverPlayingTime()) {
        return -2;
    }

    // waiting for stop X control
    while (!(g_write_action.changed &&
             g_write_action.action == DEVICE_CMD_ARM_X &&
             g_write_action.value == 0)) {
        // Player disconnect
        if (!Bluefruit.connected()) {
            return -1;
        }
        // Time out
        if (g_write_action.changed &&
            g_write_action.action == DEVICE_CMD_LIFF_TIMEOUT &&
            g_write_action.value == 1) {
            return -2;
        }
    }

    arm_x_control(ARM_X_DIR_RIGHT, 0);
    g_write_action.changed = 0;
    if (checkOverPlayingTime()) {
        return -2;
    }

    // waiting for start Y controls
    while (!(g_write_action.changed &&
             g_write_action.action == DEVICE_CMD_ARM_Y &&
             g_write_action.value == 1)) {
        // Player disconnect
        if (!Bluefruit.connected()) {
            return -1;
        }
        // Time out
        if (g_write_action.changed &&
            g_write_action.action == DEVICE_CMD_LIFF_TIMEOUT &&
            g_write_action.value == 1) {
            return -2;
        }
    }

    arm_y_control(ARM_Y_DIR_BACK, 1);
    g_write_action.changed = 0;

    if (checkOverPlayingTime()) {
        return -2;
    }

    // waiting for stop Y control
    while (!(g_write_action.changed &&
             g_write_action.action == DEVICE_CMD_ARM_Y &&
             g_write_action.value == 0)) {
        // Player disconnect
        if (!Bluefruit.connected()) {
            return -1;
        }
        // Time out
        if (g_write_action.changed &&
            g_write_action.action == DEVICE_CMD_LIFF_TIMEOUT &&
            g_write_action.value == 1) {
            return -2;
        }
    }
    arm_y_control(ARM_Y_DIR_BACK, 0);
    g_write_action.changed = 0;

    // Catch toy
    arm_z_pos_catch_and_reset();
    return 0;
}
