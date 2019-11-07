const USER_SERVICE_UUID = "6ff28a9e-787a-42d0-a56d-4185eb433bcc";
const USER_CHARACTERISTIC_NOTIFY_UUID = "0de64328-b597-4263-b2b0-2bf2f16ea4c8";
const USER_CHARACTERISTIC_WRITE_UUID = "e217a2cd-0243-4701-a5d3-20ec8eb52600";
const USER_CHARACTERISTIC_READ_UUID = "441f6ded-7e95-4552-93c4-c8f3b9823ffd";

const SET_CMD_RESET = 0;
const SET_CMD_CONFIG = 1;
const SET_CMD_START = 2;

const deviceUUIDSet = new Set();
const connectedUUIDSet = new Set();
const connectingUUIDSet = new Set();
const notificationUUIDSet = new Set();

let logNumber = 1;

function onScreenLog(text) {
  const logbox = document.getElementById('logbox');
  logbox.value += '#' + logNumber + '> ';
  logbox.value += text;
  logbox.value += '\n';
  logbox.scrollTop = logbox.scrollHeight;
  logNumber++;
}

window.onload = () => {
  liff.init(async () => {
    onScreenLog('LIFF initialized');
    renderVersionField();

    await liff.initPlugins(['bluetooth']);
    onScreenLog('BLE plugin initialized');

    checkAvailablityAndDo(() => {
      onScreenLog('Finding devices...');
      findDevice();
    });
  }, e => {
    flashSDKError(e);
    onScreenLog(`ERROR on getAvailability: ${e}`);
  });
}

async function checkAvailablityAndDo(callbackIfAvailable) {
  const isAvailable = await liff.bluetooth.getAvailability().catch(e => {
    flashSDKError(e);
    onScreenLog(`ERROR on getAvailability: ${e}`);
    return false;
  });
  // onScreenLog("Check availablity: " + isAvailable);

  if (isAvailable) {
    document.getElementById('alert-liffble-notavailable').style.display = 'none';
    callbackIfAvailable();
  } else {
    document.getElementById('alert-liffble-notavailable').style.display = 'block';
    setTimeout(() => checkAvailablityAndDo(callbackIfAvailable), 1000);
  }
}

// Find LINE Things device using requestDevice()
async function findDevice() {
  const device = await liff.bluetooth.requestDevice().catch(e => {
    flashSDKError(e);
    onScreenLog(`ERROR on requestDevice: ${e}`);
    throw e;
  });
  // onScreenLog('detect: ' + device.id);

  try {
    if (!deviceUUIDSet.has(device.id)) {
      deviceUUIDSet.add(device.id);
      addDeviceToList(device);
    } else {
      // TODO: Maybe this is unofficial hack > device.rssi
      document.querySelector(`#${device.id} .rssi`).innerText = device.rssi;
    }

    checkAvailablityAndDo(() => setTimeout(findDevice, 100));
  } catch (e) {
    onScreenLog(`ERROR on findDevice: ${e}\n${e.stack}`);
  }
}

// Add device to found device list
function addDeviceToList(device) {
  onScreenLog('Device found: ' + device.name);

  const deviceList = document.getElementById('device-list');
  const deviceItem = document.getElementById('device-list-item').cloneNode(true);
  deviceItem.setAttribute('id', device.id);
  deviceItem.querySelector(".device-id").innerText = device.id;
  deviceItem.querySelector(".device-name").innerText = device.name;
  deviceItem.querySelector(".rssi").innerText = device.rssi;
  deviceItem.classList.add("d-flex");
  deviceItem.addEventListener('click', () => {
    deviceItem.classList.add("active");
    try {
      connectDevice(device);
    } catch (e) {
      onScreenLog('Initializing device failed. ' + e);
    }
  });
  deviceList.appendChild(deviceItem);
}

// Select target device and connect it
function connectDevice(device) {
  onScreenLog('Device selected: ' + device.name);

  if (!device) {
    onScreenLog('No devices found. You must request a device first.');
  } else if (connectingUUIDSet.has(device.id) || connectedUUIDSet.has(device.id)) {
    onScreenLog('Already connected to this device.');
  } else {
    connectingUUIDSet.add(device.id);
    initializeCardForDevice(device);

    // Wait until the requestDevice call finishes before setting up the disconnect listner
    const disconnectCallback = () => {
      updateConnectionStatus(device, 'disconnected');
      device.removeEventListener('gattserverdisconnected', disconnectCallback);
      onScreenLog('disconnected -> debug!!!!!!');
    };
    device.addEventListener('gattserverdisconnected', disconnectCallback);

    onScreenLog('Connecting ' + device.name);
    device.gatt.connect().then(() => {
      updateConnectionStatus(device, 'connected');
      connectingUUIDSet.delete(device.id);

      toggleNotification(device).catch(e => onScreenLog(`ERROR on toggleNotification(): ${e}\n${e.stack}`));
      //updateSettings(device);

    }).catch(e => {
      flashSDKError(e);
      onScreenLog(`ERROR on gatt.connect(${device.id}): ${e}`);
      updateConnectionStatus(device, 'error');
      connectingUUIDSet.delete(device.id);
    });
  }
}

// Setup device information card
function initializeCardForDevice(device) {
  const template = document.getElementById('device-template').cloneNode(true);
  const cardId = 'device-' + device.id;

  template.style.display = 'block';
  template.setAttribute('id', cardId);
  template.querySelector('.card > .card-header > .device-name').innerText = device.name;

  // Device disconnect button
  template.querySelector('.device-disconnect').addEventListener('click', () => {
    onScreenLog('Clicked disconnect button');
    device.gatt.disconnect();
  });

  template.querySelector('.write-config-button').addEventListener('click', () => {
    writeCommand(device, SET_CMD_CONFIG).catch(e => onScreenLog(`ERROR on writeCommand(): ${e}\n${e.stack}`));
    readStatus(device).catch(e => onScreenLog(`ERROR on readStatus(): ${e}\n${e.stack}`));
  });

  template.querySelector('.start-button').addEventListener('click', () => {
    writeCommand(device, SET_CMD_START).catch(e => onScreenLog(`ERROR on writeCommand(): ${e}\n${e.stack}`));
    resetDisplay(device);
    //readStatus(device).catch(e => onScreenLog(`ERROR on readStatus(): ${e}\n${e.stack}`));
    getDeviceViewStatus(device).innerText = "WORK";
  });

  template.querySelector('.system-reset-button').addEventListener('click', () => {
    writeCommand(device, SET_CMD_RESET).catch(e => onScreenLog(`ERROR on writeCommand(): ${e}\n${e.stack}`));
    resetDisplay(device);
    readStatus(device).catch(e => onScreenLog(`ERROR on readStatus(): ${e}\n${e.stack}`));
  });

  template.querySelector('.read-status-button').addEventListener('click', () => {
    readStatus(device).catch(e => onScreenLog(`ERROR on readStatus(): ${e}\n${e.stack}`));
  });

  // Tabs
  ['notify', 'settings', 'info'].map(key => {
    const tab = template.querySelector(`#nav-${key}-tab`);
    const nav = template.querySelector(`#nav-${key}`);

    tab.id = `nav-${key}-tab-${device.id}`;
    nav.id = `nav-${key}-${device.id}`;

    tab.href = '#' + nav.id;
    tab['aria-controls'] = nav.id;
    nav['aria-labelledby'] = tab.id;
  })

  // Remove existing same id card
  const oldCardElement = getDeviceCard(device);
  if (oldCardElement && oldCardElement.parentNode) {
    oldCardElement.parentNode.removeChild(oldCardElement);
  }

  document.getElementById('device-cards').appendChild(template);
  onScreenLog('Device card initialized: ' + device.name);
}

// Update Connection Status
function updateConnectionStatus(device, status) {
  if (status == 'connected') {
    onScreenLog('Connected to ' + device.name);
    connectedUUIDSet.add(device.id);

    const statusBtn = getDeviceStatusButton(device);
    statusBtn.setAttribute('class', 'device-status btn btn-outline-primary btn-sm disabled');
    statusBtn.innerText = "Connected";
    getDeviceDisconnectButton(device).style.display = 'inline-block';
    getDeviceCardBody(device).style.display = 'block';
  } else if (status == 'disconnected') {
    onScreenLog('Disconnected from ' + device.name);
    connectedUUIDSet.delete(device.id);

    const statusBtn = getDeviceStatusButton(device);
    statusBtn.setAttribute('class', 'device-status btn btn-outline-secondary btn-sm disabled');
    statusBtn.innerText = "Disconnected";
    getDeviceDisconnectButton(device).style.display = 'none';
    getDeviceCardBody(device).style.display = 'none';
    document.getElementById(device.id).classList.remove('active');
  } else {
    onScreenLog('Connection Status Unknown ' + status);
    connectedUUIDSet.delete(device.id);

    const statusBtn = getDeviceStatusButton(device);
    statusBtn.setAttribute('class', 'device-status btn btn-outline-danger btn-sm disabled');
    statusBtn.innerText = "Error";
    getDeviceDisconnectButton(device).style.display = 'none';
    getDeviceCardBody(device).style.display = 'none';
    document.getElementById(device.id).classList.remove('active');
  }
}

async function toggleNotification(device) {
  if (!connectedUUIDSet.has(device.id)) {
    window.alert('Please connect to a device first');
    onScreenLog('Please connect to a device first.');
    return;
  }

  const accelerometerCharacteristic = await getCharacteristic(
    device, USER_SERVICE_UUID, USER_CHARACTERISTIC_NOTIFY_UUID);

  await enableNotification(accelerometerCharacteristic, notificationCallback).catch(e => onScreenLog(`ERROR on notification start(): ${e}\n${e.stack}`));
  notificationUUIDSet.add(device.id);
}

async function enableNotification(characteristic, callback) {
  const device = characteristic.service.device;
  characteristic.addEventListener('characteristicvaluechanged', callback);
  await characteristic.startNotifications();
  onScreenLog('Notifications STARTED ' + characteristic.uuid + ' ' + device.id);
}

async function stopNotification(characteristic, callback) {
  const device = characteristic.service.device;
  characteristic.removeEventListener('characteristicvaluechanged', callback);
  await characteristic.stopNotifications();
  onScreenLog('Notifications STOPPED　' + characteristic.uuid + ' ' + device.id);
}

function notificationCallback(e) {
  const notifyData = new DataView(e.target.value.buffer);
  onScreenLog(`Notify ${e.target.uuid}: ${buf2hex(e.target.value.buffer)}`);
  updateStatus(e.target.service.device, notifyData);
}

async function writeCommand(device, cmd) {
  const card = getDeviceCard(device);
  const data = [cmd];

  onScreenLog('Write to device : ' + new Uint8Array(data));

  const characteristic = await getCharacteristic(
    device, USER_SERVICE_UUID, USER_CHARACTERISTIC_WRITE_UUID);
  await characteristic.writeValue(new Uint8Array(data)).catch(e => {
    onScreenLog(`Error writing ${characteristic.uuid}: ${e}`);
    throw e;
  });
}

async function readStatus(device) {
  const settingsCharacteristic = await getCharacteristic(
    device, USER_SERVICE_UUID, USER_CHARACTERISTIC_READ_UUID);

  const buffer = await readCharacteristic(settingsCharacteristic).catch(e => {
    return null;
  });


  onScreenLog(buf2hex(buffer));

  if (buffer != null) {
    onScreenLog("wrote data");

    let status = buffer.getInt16(0, true);
    const predict_time = buffer.getUint16(2, true);
    const dry_temp = buffer.getInt16(4, true) / 100;
    const dry_humidity = buffer.getInt16(6, true) / 100;
    const current_temp = buffer.getInt16(8, true) / 100;
    const current_humidity = buffer.getInt16(10, true) / 100;
    const battery = buffer.getInt16(12, true);
    const time = new Date();

    if (status == 0) {
      status = "IDLE";
    } else if (status == 1) {
      status = "WORK";
    } else if (status == 2) {
      status = "DONE";
    } else {
      status = "error";
    }

    getDeviceViewTemperature(device).innerText = current_temp + " ℃";
    getDeviceViewDryTemperature(device).innerText = dry_temp + " ℃";
    getDeviceViewDryHumidity(device).innerText = dry_humidity + " %";
    getDeviceViewBattery(device).innerText = battery + " %";
    getDeviceViewHumidity(device).innerText = current_humidity + " %";
    getDeviceViewPressure(device).innerText = "not support";
    getDeviceViewStatus(device).innerText = status;
    getDeviceViewPredictTime(device).innerText = predict_time + " minute";
    getDeviceViewAltitude(device).innerText = "not support";
    getDeviceViewLastUpdate(device).innerText = time.getHours() + "時" + time.getMinutes() + "分" + time.getSeconds() + "秒";
    onScreenLog("read data : ok");
  } else {
    onScreenLog("read data is null");
  }
}

function resetDisplay(device) {
  getDeviceViewTemperature(device).innerText = "";
  getDeviceViewBattery(device).innerText = "";
  getDeviceViewHumidity(device).innerText = "";
  getDeviceViewPressure(device).innerText = "";
  getDeviceViewStatus(device).innerText = "";
  getDeviceViewPredictTime(device).innerText = "";
  getDeviceViewAltitude(device).innerText = "";
  getDeviceViewLastUpdate(device).innerText = "";
  getDeviceViewDryHumidity(device).innerText = "";
  getDeviceViewDryTemperature(device).innerText = "";

}

function updateStatus(device, buffer) {
  const battery = buffer.getInt8(0);
  const dryStatus = buffer.getInt8(1);
  const time = new Date();

  let tmp = (battery == 1) ? "バッテリーが20%以下です。" : "";
  tmp = tmp + (dryStatus == 1) ? " 服が乾きました。" : "服はまだ乾いていません。";

  getDeviceViewStatus(device).innerText = tmp;
  getDeviceViewLastUpdate(device).innerText = time.getHours() + "時" + time.getMinutes() + "分" + time.getSeconds() + "秒";
}

async function readCharacteristic(characteristic) {
  const response = await characteristic.readValue().catch(e => {
    onScreenLog(`Error reading ${characteristic.uuid}: ${e}`);
    throw e;
  });
  if (response) {
    onScreenLog(`Read ${characteristic.uuid}: ${buf2hex(response.buffer)}`);
    const values = new DataView(response.buffer);
    return values;
  } else {
    throw 'Read value is empty?';
  }
}

async function getCharacteristic(device, serviceId, characteristicId) {
  const service = await device.gatt.getPrimaryService(serviceId).catch(e => {
    flashSDKError(e);
    throw e;
  });
  const characteristic = await service.getCharacteristic(characteristicId).catch(e => {
    flashSDKError(e);
    throw e;
  });
  onScreenLog(`Got characteristic ${serviceId} ${characteristicId} ${device.id}`);
  return characteristic;
}

function getDeviceCard(device) {
  return document.getElementById('device-' + device.id);
}

function getDeviceCardBody(device) {
  return getDeviceCard(device).getElementsByClassName('card-body')[0];
}

function getDeviceStatusButton(device) {
  return getDeviceCard(device).getElementsByClassName('device-status')[0];
}

function getDeviceDisconnectButton(device) {
  return getDeviceCard(device).getElementsByClassName('device-disconnect')[0];
}


function getDeviceViewTemperature(device) {
  return getDeviceCard(device).getElementsByClassName('progress-bar-temperature')[0];
}

function getDeviceViewBattery(device) {
  return getDeviceCard(device).getElementsByClassName('progress-bar-battery')[0];
}

function getDeviceViewDryTemperature(device) {
  return getDeviceCard(device).getElementsByClassName('progress-bar-dry-temperature')[0];
}

function getDeviceViewDryHumidity(device) {
  return getDeviceCard(device).getElementsByClassName('progress-bar-dry-humidity')[0];
}

function getDeviceViewHumidity(device) {
  return getDeviceCard(device).getElementsByClassName('progress-bar-humidity')[0];
}

function getDeviceViewPressure(device) {
  return getDeviceCard(device).getElementsByClassName('progress-bar-pressure')[0];
}

function getDeviceViewStatus(device) {
  return getDeviceCard(device).getElementsByClassName('progress-bar-status')[0];
}

function getDeviceViewPredictTime(device) {
  return getDeviceCard(device).getElementsByClassName('progress-bar-predict-time')[0];
}

function getDeviceViewAltitude(device) {
  return getDeviceCard(device).getElementsByClassName('progress-bar-altitude')[0];
}

function getDeviceViewLastUpdate(device) {
  return getDeviceCard(device).getElementsByClassName('progress-bar-lastupdate')[0];
}


function renderVersionField() {
  const element = document.getElementById('sdkversionfield');
  const versionElement = document.createElement('p')
    .appendChild(document.createTextNode('SDK Ver: ' + liff._revision));
  element.appendChild(versionElement);
}

function flashSDKError(error) {
  window.alert('SDK Error: ' + error.code);
  window.alert('Message: ' + error.message);
}

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function buf2hex(buffer) { // buffer is an ArrayBuffer
  return Array.prototype.map.call(new Uint8Array(buffer), x => ('00' + x.toString(16)).slice(-2)).join('');
}