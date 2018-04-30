//  File deep-insect is modified by ikeyasu 11/2016
//  Original file SerialCamera.pde for camera.
//  25/7/2011 by Piggy
//  Modify by Deray  08/08/2012

#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
extern "C" {
#include <user_interface.h>
};
#include "local_setting.h"

/**** RTCMem *****/
struct RTCMem {
  uint32_t hash;
  uint16_t count;
} gRTCMem;

/**** NTP *****/
WiFiUDP gNTPUDP;
NTPClient gNTPClient(gNTPUDP, "ntp.nict.jp");

/**** camera *****/
SoftwareSerial gCamSerial(12, 13, false, 256); //receivePin, transmitPin, inverse_logic, buffSize
#define PIC_PKT_LEN    2048                  //data length of each read, dont set this too big because ram is limited
#define PIC_FMT_VGA    7
#define PIC_FMT_CIF    5
#define PIC_FMT_OCIF   3
#define CAM_ADDR       0
#define PIC_FMT        PIC_FMT_VGA
const byte gCameraAddr = (CAM_ADDR << 5);  // addr

/*** others ***/
unsigned long gPicTotalLen = 0;  // picture length
ESP8266WiFiMulti gWifiMulti;
HTTPClient gHttp;
ADC_MODE(ADC_VCC);

void setup()
{
  Serial.begin(115200);
  gCamSerial.begin(115200);
  delayMicroseconds(10000);
  Serial.println("initialization done.");
  initCamera();
  gWifiMulti.addAP(WIFI_SSID, WIFI_PASS);
  gHttp.setReuse(true);
  
  Serial.print("wifi connecting ..");
  while ((gWifiMulti.run() != WL_CONNECTED)) {
    delay(500);
    Serial.print(".");
  }
  delay(1000);
  Serial.println("connected.");
  Serial.print("test connection ...");
  int httpCode = 0;
  while (httpCode != 200) {
    gHttp.begin(gURL);
    httpCode = gHttp.GET();
    Serial.print(".");
    Serial.print(httpCode);
    gHttp.end();
  }
  Serial.println(".done.");
  Serial.println("starting ntp...");
  gNTPClient.begin();
  gNTPClient.update();
  unsigned long epochTime = gNTPClient.getEpochTime();
  Serial.print("unix time: ");
  Serial.println(epochTime);

  int vcc = ESP.getVcc();
  Serial.print("vcc:");
  Serial.println(vcc);

  loadRTCMem();
  initCapture();
  delay(1500);
  Serial.println("Start to take a picture");
  capture();
  sendData(epochTime, vcc);
  Serial.print("Taking pictures success ,number : ");
  Serial.println(gRTCMem.count);
  gRTCMem.count++;
  Serial.println();
  writeRTCMem();
  ESP.deepSleep(60 * 60 * 1000 * 1000 , WAKE_RF_DEFAULT); // 60 sec
}

void loop()
{  
}

void initCamera()
{
  char cmd[] = {0xaa, 0x0d | gCameraAddr, 0x00, 0x00, 0x00, 0x00} ;
  unsigned char resp[6];

  gCamSerial.setTimeout(500);
  while (1) {
    //clearRxBuf();
    sendCmd(cmd, 6);
    if (gCamSerial.readBytes((char *)resp, 6) != 6) {
      continue;
    }
    if (resp[0] == 0xaa && resp[1] == (0x0e | gCameraAddr) && resp[2] == 0x0d && resp[4] == 0 && resp[5] == 0) {
      if (gCamSerial.readBytes((char *)resp, 6) != 6) continue;
      if (resp[0] == 0xaa && resp[1] == (0x0d | gCameraAddr) && resp[2] == 0 && resp[3] == 0 && resp[4] == 0 && resp[5] == 0) break;
    }
  }
  cmd[1] = 0x0e | gCameraAddr;
  cmd[2] = 0x0d;
  sendCmd(cmd, 6);
  Serial.println("Camera initialization done.");
}

void initCapture()
{
  char cmd[] = { 0xaa, 0x01 | gCameraAddr, 0x00, 0x07, 0x00, PIC_FMT };
  unsigned char resp[6];

  gCamSerial.setTimeout(100);
  while (1) {
    clearRxBuf();
    sendCmd(cmd, 6);
    if (gCamSerial.readBytes((char *)resp, 6) != 6) continue;
    if (resp[0] == 0xaa && resp[1] == (0x0e | gCameraAddr) && resp[2] == 0x01 && resp[4] == 0 && resp[5] == 0) break;
  }
  Serial.println("Capture initialization done.");
}

void capture() {
  char cmd[] = { 0xaa, 0x06 | gCameraAddr, 0x08, PIC_PKT_LEN & 0xff, (PIC_PKT_LEN >> 8) & 0xff , 0};
  unsigned char resp[6];

  gCamSerial.setTimeout(100);
  while (1) {
    clearRxBuf();
    sendCmd(cmd, 6);
    if (gCamSerial.readBytes((char *)resp, 6) != 6) continue;
    if (resp[0] == 0xaa && resp[1] == (0x0e | gCameraAddr) && resp[2] == 0x06 && resp[4] == 0 && resp[5] == 0) break;
  }
  cmd[1] = 0x05 | gCameraAddr;
  cmd[2] = 0;
  cmd[3] = 0;
  cmd[4] = 0;
  cmd[5] = 0;
  while (1) {
    clearRxBuf();
    sendCmd(cmd, 6);
    if (gCamSerial.readBytes((char *)resp, 6) != 6) continue;
    if (resp[0] == 0xaa && resp[1] == (0x0e | gCameraAddr) && resp[2] == 0x05 && resp[4] == 0 && resp[5] == 0) break;
  }
  cmd[1] = 0x04 | gCameraAddr;
  cmd[2] = 0x1;
  while (1) {
    clearRxBuf();
    sendCmd(cmd, 6);
    if (gCamSerial.readBytes((char *)resp, 6) != 6) continue;
    if (resp[0] == 0xaa && resp[1] == (0x0e | gCameraAddr) && resp[2] == 0x04 && resp[4] == 0 && resp[5] == 0)
    {
      gCamSerial.setTimeout(1000);
      if (gCamSerial.readBytes((char *)resp, 6) != 6)
      {
        continue;
      }
      if (resp[0] == 0xaa && resp[1] == (0x0a | gCameraAddr) && resp[2] == 0x01)
      {
        gPicTotalLen = (resp[3]) | (resp[4] << 8) | (resp[5] << 16);
        break;
      }
    }
  }
}

void clearRxBuf() {
  while (Serial.available()) gCamSerial.read();
}

int skipLF(char in[], int len) {
  int skip = 0;
  for (int i = 0; i + skip < len; i++) {
    if (in[i] == '\n') skip++;
    in[i] = in[i + skip];
  }
  return len - skip;
}

char sendData(int index, int vcc) {
  unsigned int pktCnt = (gPicTotalLen) / (PIC_PKT_LEN - 6);
  if ((gPicTotalLen % (PIC_PKT_LEN - 6)) != 0) pktCnt += 1;

  char cmd[] = { 0xaa, 0x0e | gCameraAddr, 0x00, 0x00, 0x00, 0x00 };
  char pkt[PIC_PKT_LEN];
  char result = 4; // 4 is default

  gCamSerial.setTimeout(1000);
  for (unsigned int i = 0; i < pktCnt; i++) {
    cmd[4] = i & 0xff;
    cmd[5] = (i >> 8) & 0xff;

    int retry_cnt = 0;
retry:
    delay(10);
    clearRxBuf();
    sendCmd(cmd, 6);
    uint16_t cnt = gCamSerial.readBytes((char *)pkt, PIC_PKT_LEN);

    unsigned char sum = 0;
    for (int y = 0; y < cnt - 2; y++) {
      sum += pkt[y];
    }
    if (sum != pkt[cnt - 2]) {
      if (++retry_cnt < 100) goto retry;
      else break;
    }
    sprintf(gURL + URL_LEN, "n=%d&last=%s&vcc=%d",
      index, (i == pktCnt - 1) ? "y" : "n", vcc);
    gHttp.begin(gURL);
    int httpCode = gHttp.POST((uint8_t *) &pkt[4], cnt - 6);
    Serial.print("sendData: httpCode=");
    Serial.print(httpCode, DEC);
    Serial.print(" size=");
    Serial.println(gHttp.getSize(), DEC);
    if (httpCode == 200 && gHttp.getSize() == 1) {
      String payload = gHttp.getString();
      result = (payload == "-") ? -1 : payload.toInt();
      Serial.print("sendData: result=");
      Serial.println(result, DEC);
    }
    gHttp.end();
  }
  cmd[4] = 0xf0;
  cmd[5] = 0xf0;
  sendCmd(cmd, 6);
  return result;
}

void sendCmd(char cmd[] , int cmd_len) {
  for (char i = 0; i < cmd_len; i++) gCamSerial.print(cmd[i]);
}

// https://lowreal.net/2016/01/10/1

// system_rtc_mem_write() 先のブロックアドレス。
// 4 bytes で align されており、先頭256bytes はシステム予約領域
// 64 から書けるはずだが、65 以降でないとうまくいかなかった。。
static const uint32_t USER_DATA_ADDR = 66;

// ハッシュ関数 (FNV) CRC でいいけどコード的に短いのでFNV
static uint32_t fnv_1_hash_32(uint8_t *bytes, size_t length) {
  static const uint32_t FNV_OFFSET_BASIS_32 = 2166136261U;
  static const uint32_t FNV_PRIME_32 = 16777619U;
  uint32_t hash = FNV_OFFSET_BASIS_32;;
  for(size_t i = 0 ; i < length ; ++i) hash = (FNV_PRIME_32 * hash) ^ (bytes[i]);
  return hash;
}

// struct の hash (先頭にあることを想定) を除くデータ部分のハッシュを計算する
uint32_t calc_hash(struct RTCMem &data) {
  return fnv_1_hash_32(((uint8_t*)&data) + sizeof(data.hash), sizeof(struct RTCMem) - sizeof(data.hash));
}

void loadRTCMem() {
  bool ok;
  ok = system_rtc_mem_read(USER_DATA_ADDR, &gRTCMem, sizeof(gRTCMem));
  if (!ok) {
    Serial.println("system_rtc_mem_read failed");
  }
  Serial.print("gRTCMem.count = ");
  Serial.println(gRTCMem.count);

  uint32_t hash = calc_hash(gRTCMem);
  if (gRTCMem.hash != hash) {
    Serial.println("gRTCMem may be uninitialized");
    gRTCMem.count = 0;
  }
}

void writeRTCMem() {
  uint32_t hash;
  bool ok;
  gRTCMem.hash = hash = calc_hash(gRTCMem);
  ok = system_rtc_mem_write(USER_DATA_ADDR, &gRTCMem, sizeof(gRTCMem));
  if (!ok) {
    Serial.println("system_rtc_mem_write failed");
  }
}

