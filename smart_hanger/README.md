# Smart Hanger
LIFF / 自動通信と組み合わせて何時ころ洗濯物が乾くか、乾いたら通知を実現します。

- 回路及び基板はこれと共通 : https://github.com/cpulabs/line-things-devices/tree/master/air-quality-monitor


## シナリオ (案)
### 設定 (一度だけ)
1. LIFFを開きDeviceに接続
2. 設定タブを開き、予め服を干していない状態で室内の 気温 / 湿度を取得してデバイスに設定

### 機能有効化
1. 濡れた服をハンガーにかける
2. SW2ボタンを押して計測を開始 -> 自動通信でスマホに通知。またはLIFFから開始ボタンを押す(Settings characteristicを使用)
3. デバイス内で予め設定した服がかかっていない状態の気温・湿度から乾くまでの時間を予測
4. 服が乾いた & スマホとConnectedなときは乾いたことを通知(スマホに接続されていないときは待つ)


## GATT Service

### Hanger Service

#### Settings characteristic
Write


デバイスの設定と開始などを行う。

| Format|
|----|
| [1Byte]CMD |


| CMD | function |
----|----
|CMD == 0|Reset device|
|CMD == 1|Set dry temp / humidity to device(EEPROMにも書き込まれるため、Reset後も保持)|
|CMD == 2|Start|


#### Status characteristic
Read

| Format|
|----|
|[1Byte]Status, [2Byte]Predict time, [2Byte]Dry-Temp, [2Byte]Dry-Humidity, [2Byte]Current-Temp, [2Byte]Current-Humidity, [1Byte]Battery level, [4Byte]reserved|

| Type | function |
----|----
|Status|0:Idle, 1:Working|
|Predict Time|Status==1のときに有効。予測時間(単位minute)|
|Dry temp|服がかかっていないときの気温(100倍する必要あり)|
|Dry humidity|服がかかっていないときの湿度(100倍する必要あり)|
|Current temp|現在の気温(100倍する必要あり)|
|Current humidity|現在の湿度(100倍する必要あり)|
|Battery|バッテリ残量(1~100%)|
|Reserved|将来拡張領域。0が帰る|



#### Notify characteristic
Read / Notify

バッテリが10%以下、または服が乾いた(Status==1) + Connectionが有効なときに、一度だけNotifyする。

| Format|
|----|
|[1Byte]Battery Status, [1Byte]Clothes Status|

| Type | function |
----|----
|Battery Status|バッテリが10%以下になったら1を通知。それ以外は0|
|Clothes Status|服が乾いたら1を通知。それ以外は0|


### PSDI Service
#### PSDI characteristic
Read

LINE Things PSDIの仕様を実装
