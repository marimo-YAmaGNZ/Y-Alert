/****************************************************************************************
   Y-Alert(YokatoChan Alert)

    This program is for ESP32 and requires the libraly of ESP8266 Audio.
    https://github.com/earlephilhower/ESP8266Audio
    https://github.com/Gianbacchio/ESP8266_Spiram

    Arduino用漢字フォントライブラリ SDカード版
    製作者      :たま吉さん
    製作者HP    :http://nuneno.cocolog-nifty.com
    https://github.com/Tamakichi/Arduino-KanjiFont-Library-SD

****************************************************************************************/
#include <M5Stack.h>
#include <WiFi.h>
#include <math.h>
#include "time.h"
#include "string.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <sdfonts.h>
#include "AudioFileSourceSD.h"
#include "AudioFileSourceID3.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include "src\yalert.h"
#include "src\7seg.h"

#define DEBUG_FLAG (true)

// CSPin番号
#define SD_PN 4

// 1グループのチャンネル数
const int maxChannels = 2;
// RSSで得られる動画の数
const int maxVideos = 15;

// 時差
const long gmt_offset_sec = 9 * 3600;


//***************************************************************************
// システム設定で変更できる値

// WiFi設定関連
String ssid;
String password;

// Youtube関連
String apiKey;

// チェック間隔（分）
int Interval = 1;

// チェックするグループの数
int8_t GroupMax;

//***************************************************************************


// SDカードアクセス状態
bool SDCardAccess = false;

// 画面表示
boolean is_state_changed = true;    // 画面が切り替わったか

// 画面表示時の色
uint16_t backcolor;
uint16_t forecolor;

// 現在の表示画面
DisplayMode disp_page = DISP_CLOCK;

bool Check = true;    // 現在のチェック状態

// サムネイル保存用ファイル名
const char* thumbnailFileName = "/thumbnail.jpg";
// サムネイルが取得できなかった時の画像ファイル名
const char* noimageFileName = "/noimage.jpg";
// 表示するがサムネイルファイル名
const char* thumbnail;

// MP3
bool PlayAlert = false;
String AlertFileName;
// ミュート状態
RTC_DATA_ATTR bool mute = false;

long APICallCount = 0;

// 時計表示位置
#define DATE_Y_POS (26)
#define TIME_Y_POS (82)

// 動画配信状態
typedef enum {
  UNKNOWN = -1,   // 不明
  UPCOMING = 0,   // 配信予定
  LIVE = 1,       // 配信中
  NONE = 2,       // 配信終了
} VideoStatus;


// RSSから得る情報を格納する構造体
struct video {
  String videoid;
  String updatetime;
};


// 動画情報を管理するクラス
class videoinfomation {
public:
  String videoid;           // ビデオID
  String updatetime;        // RSS上のupdatetimeフィールド
  String prev_updatetime;   // 前回のupdatetimeフィールド更新確認用
  VideoStatus status;       // 動画の配信状態
  struct tm starttime;      // 開始予定日時

  videoinfomation &operator=(const videoinfomation& src){
    videoid = src.videoid;
    updatetime = src.updatetime;
    prev_updatetime = src.prev_updatetime;
    status = src.status;
    starttime = src.starttime;

    return(*this);
  };

  // 配信開始予定日時と現在日時の差分を求める
  time_t later() {
    if (status == UPCOMING){
        time_t now = time(NULL);
        time_t videostart = mktime(&starttime);
        return videostart - now;
    } else {
      return (time_t)-1;
    }
  };
};


// チャンネル情報を格納するクラス
class channelinfomation {
public:
  String channelName;         // 表示用のチャンネル名
  String channelID;           // チャンネルID
  String AlertFileName;       // アラート時に鳴るMP3のファイル名
  String VideoInfoFilename;   // 動画情報の保存ファイル名
  String AleartedVideoID;     // アラートを発したビデオID
  videoinfomation videos[maxVideos]; // 動画情報

  // RSS情報からチャンネル動画情報を更新する
  void UpdateVideoInfo(struct video* newvideos) {
    videoinfomation oldvideos[maxVideos];
    int8_t newindex = 0;

    // 現在の情報をコピー
    for (int o = 0 ; o < maxVideos; o++) {
      oldvideos[o] = videos[o];
    }

    bool find;
    for (int n = 0; n < maxVideos ; n++) {
      find = false;

      //　現在の情報に存在するか探す
      for (int o = 0 ; o < maxVideos; o++) {
        // 存在する場合はそれを利用
        if ( newvideos[n].videoid == oldvideos[o].videoid) {
          videos[newindex] = oldvideos[o];
          videos[newindex].prev_updatetime = videos[newindex].updatetime;
          videos[newindex].updatetime = newvideos[n].updatetime;

          newindex++;
          find = true;
          break;
        }
      }

      // 存在しない場合は新しい動画としてデータを追加する
      if (!find) {
        // NewVideo
        videos[newindex].videoid = newvideos[n].videoid;
        videos[newindex].updatetime = newvideos[n].updatetime;
        videos[newindex].prev_updatetime = "";
        videos[newindex].status = UNKNOWN;
        newindex++;
      }
    }

  };
};


// 監視グループ情報を格納するクラス
class Group
{
private:
  String _filename;
  int8_t isFirst = -1;
  int8_t nextchannel = -1;
  int8_t nextvideo = -1;
  int8_t prev_isFirst = -2;
  int8_t prev_nextchannel = -2;
  int8_t prev_nextvideo = -2;
  bool nowstream = false;

public:
  String GroupName;                   // 表示グループ名
  int8_t ChannelCount = 0;            // チャンネル数（最大maxChannelsまで）
  String ChannelInfoFilename;         // チャンネル情報のファイル名
  channelinfomation Channels[maxChannels]; // チャンネル情報
  String NextStreamvideoid;           // ビデオID

  bool LoadGroup(String filename);
  void SaveGroup(void);
  bool isChanged(void);
  void Backup(void);
  bool CheckNextStream(void);
  String GetNextStreamChannelString(void);
  String GetNextStreamDateTimeString(void);
  String GetAlertFileName(void);
};

// 指定ファイル名からグループ情報を読み込む
bool Group::LoadGroup(String filename) {
  _filename = filename;
  Serial.println("Load FileName = " + filename);
  File file = SD.open(filename,FILE_READ);
  String line,value;
  int8_t videocount = 0;
  
  int size;
  char* linedata;

  while (file.available() != 0){
    line = file.readStringUntil('\n');
    line.trim();
    Serial.println(line);

    // グループ情報
    if (line.startsWith("g:")){
      line.replace("g:","");
      // グループ情報
      GroupName = line;
      Serial.println("GroupName = " + GroupName);
      continue;
    }

    // チャンネル情報
    if (line.startsWith("c:")){
      line.replace("c:","");
      size = line.length() + 1;
      linedata = new char[size];
      line.toCharArray(linedata, size);

      // チャンネル情報
      value = strtok(linedata,",");
      value.trim();
      Channels[ChannelCount].channelName = value;

      value = strtok(NULL,",");
      value.trim();
      Channels[ChannelCount].channelID = value;

      value = strtok(NULL,",");
      value.trim();
      Channels[ChannelCount].AlertFileName = value;

      value = strtok(NULL,",");
      value.trim();
      Channels[ChannelCount].AleartedVideoID = value;

      ChannelCount++;
      videocount = 0;
      delete[] linedata;
    }

    // 動画情報
    if (line.startsWith("v:")){
      line.replace("v:","");
      size = line.length() + 1;
      linedata = new char[size];
      line.toCharArray(linedata, size);

      // ビデオ情報
      value = strtok(linedata,",");
      value.trim();
      Channels[ChannelCount-1].videos[videocount].videoid = value;

      // 2列目
      value = strtok(NULL,",");
      value.trim();
      Channels[ChannelCount-1].videos[videocount].updatetime = value;

      // 3列目
      value = strtok(NULL,",");
      value.trim();
      Channels[ChannelCount-1].videos[videocount].prev_updatetime = value;

      // 4列目
      value = strtok(NULL,",");
      value.trim();
      Channels[ChannelCount-1].videos[videocount].status = VideoStatus(value.toInt());

      // // 5列目
      value = strtok(NULL,",");
      value.trim();
      strptime(value.c_str(),"%Y/%m/%d %H:%M:%S",&Channels[ChannelCount-1].videos[videocount].starttime);

      videocount++;
      delete[] linedata;
    }
  }

  file.close();

}

// SDカードにグループ情報を書き込む
void Group::SaveGroup(void) {
  char date[20];

  File file = SD.open(_filename,FILE_WRITE);
  Serial.println("Write FileName = " + _filename);

  // グループの書き出し
  file.println("g:" + GroupName);

  // チャンネル情報書き出し
  for (int i = 0 ; i < ChannelCount ; i++) {
    file.print("c:" + Channels[i].channelName);
    file.print(" , " + Channels[i].channelID);
    file.print(" , " + Channels[i].AlertFileName);
    file.print(" , ");
    file.println(Channels[i].AleartedVideoID);

    // ビデオ情報の書き出し
    for ( int v = 0 ; v < maxVideos ; v++ ) {
      file.print("\tv:" + Channels[i].videos[v].videoid);
      file.print(" , " + Channels[i].videos[v].updatetime);
      file.print(" , " + Channels[i].videos[v].prev_updatetime);
      file.print(" , " + String(Channels[i].videos[v].status));
      strftime(date,sizeof(date),"%Y/%m/%d %H:%M:%S",&Channels[i].videos[v].starttime);
      file.print(" , ");
      file.println(date);
    }
    file.println("");
  }

  file.close();

}

bool Group::isChanged(void){
  if (prev_isFirst != isFirst) return true;
  if (prev_nextchannel != nextchannel) return true;
  if (prev_nextvideo != nextvideo) return true;
  if (nowstream) {
    nowstream = false;
    return true;
  }
  return false;
}

void Group::Backup(void){
  prev_isFirst = isFirst;
  prev_nextchannel = nextchannel;
  prev_nextvideo = nextvideo;
}

// 次の配信を求めて、その配信がアラート対象か判断する
bool Group::CheckNextStream(void){

  int8_t c=-1,v=-1;
  time_t d=999999999;

  for (int ci = 0; ci < ChannelCount ; ci++) {
    for ( int vi = 0; vi < maxVideos ; vi++) {
      if ( Channels[ci].videos[vi].status == LIVE) {
        c = ci;
        v = vi;

        if (nextchannel == c && nextvideo == v) {
          // 次回配信から配信中になった
          nowstream = true;
        }
        goto LOOPEND;
      }
      if ( Channels[ci].videos[vi].status == UPCOMING) {
        if (Channels[ci].videos[vi].later() < d){
          c = ci;
          v = vi;
          d = Channels[ci].videos[vi].later();
        }
      }
    }
  }
LOOPEND:

  isFirst = false;
  nextchannel = c;
  nextvideo = v;

  if ( nextchannel == -1) {
    Serial.println("次回配信はなし");
    return false;
  } else {
    Serial.print("次回配信 videoid = " + Channels[c].videos[v].videoid);
    Serial.print(" : status = " + String(Channels[c].videos[v].status));
    Serial.print(" : update = " + Channels[c].videos[v].prev_updatetime);
    Serial.print(" / " + Channels[c].videos[v].updatetime);
    Serial.printf(" : starttime = %d/%02d/%02d %02d:%02d\n" ,Channels[c].videos[v].starttime.tm_year + 1900
                                                            ,Channels[c].videos[v].starttime.tm_mon + 1
                                                            ,Channels[c].videos[v].starttime.tm_mday
                                                            ,Channels[c].videos[v].starttime.tm_hour
                                                            ,Channels[c].videos[v].starttime.tm_min);
    if ( Channels[c].videos[v].later() <= 60 * Interval ) {
      if (Channels[c].AleartedVideoID != Channels[c].videos[v].videoid) {
        Serial.println("アラート！");
        Channels[c].AleartedVideoID = Channels[c].videos[v].videoid;
        NextStreamvideoid = Channels[c].videos[v].videoid;
        return true;
      } else {
        return false;
      }
    } else {
      return false;
    }
  }

}

// 次回配信のチャンネル名部分に表示する文言を取得する
String Group::GetNextStreamChannelString(void){
  if (isFirst) return "データ取得中";
  if (nextchannel == -1) return "予定なし";
  return Channels[nextchannel].channelName;
}

// 次回配信の日時部分に表示する文言を取得する
String Group::GetNextStreamDateTimeString(void) {
  if (nextchannel == -1) return "";
  if (Channels[nextchannel].videos[nextvideo].status == LIVE) return "配信中";

  char work[17];
  strftime(work,sizeof(work),"%Y/%m/%d %H:%M",&Channels[nextchannel].videos[nextvideo].starttime);

  return String(work);
}

// アラート時に鳴らすMP3ファイル名を取得する
String Group::GetAlertFileName(void) {
  return Channels[nextchannel].AlertFileName;
}

Group* groups;
int CheckGroupIndex = 0;
int PrevGroupIndex = -1;



// システム設定の読み込み
bool LoadSystemSetting(){
  String line , name , value;

  // ファイルが存在しない場合
  if (!SD.exists(Settingfilename)) return false;

  SDCardAccess = true;
  File file = SD.open(Settingfilename, FILE_READ);
  if(!file)
  {
    Serial.println("SD Card Read Error.");
    SDCardAccess = false;
    return false;
  }
  
  while (file.available() != 0){
    line = file.readStringUntil('\n');
    line.trim();

    // コメント行は飛ばす
    if (line.startsWith("//")) continue;

    int size = line.length() + 1;
    char linedata[size];
    line.toCharArray(linedata, size);

    // 設定名
    name = strtok(linedata,"=");
    name.trim();
    // 値
    value = strtok(NULL,"=");
    value.trim();

    if (name == "ssid") ssid = value;
    if (name == "password") password = value;
    if (name == "apiKey") apiKey = value;
    if (name == "Interval") Interval = value.toInt();
    if (name == "GroupCount") GroupMax = value.toInt();
  }

  file.close();
  SDCardAccess = false;

  return true;
}


bool LoadGroupData(){
  groups = new Group[GroupMax];

  String filename;
  File dir = SD.open("/GroupData");

  for ( int i = 0 ; i < GroupMax ; i++) {
    File file = dir.openNextFile();
    filename = String(file.name());
    file.close();
    groups[i].LoadGroup(filename);
  }

  dir.close();
}



#define NTP_ACCESS_MS_INTERVAL (300000)
// System Clock
class SystemClock
{
private:
  struct tm time_info;
  time_t timer;
  uint32_t ntpset = 0;
  bool updateByNtp(void);
  void updateBySoftTimer(uint32_t elasped_second);

public:
  uint32_t year = 0;
  uint32_t month = 0;
  uint32_t day = 0;
  uint32_t hour = 0;
  uint32_t minute = 0;
  uint32_t week_day = 0;
  uint32_t second = 0;
  uint32_t prev_year = 0;
  uint32_t prev_month = 0;
  uint32_t prev_day = 0;
  uint32_t prev_hour = 0;
  uint32_t prev_minute = 0;
  uint32_t prev_week_day = 0;
  uint32_t prev_second = 0;
  void backupCurrentTime(void);
  bool updateClock(void);
};

void SystemClock::backupCurrentTime(void)
{
  prev_year = year;
  prev_month = month;
  prev_day = day;
  prev_hour = hour;
  prev_minute = minute;
  prev_week_day = week_day;
  prev_second = second;
}

void SystemClock::updateBySoftTimer(uint32_t elasped_second)
{
  struct tm *local_time;
  time_t timer_add = timer + elasped_second;
  local_time = localtime(&timer_add);

  year = local_time->tm_year + 1900;
  month = local_time->tm_mon + 1;
  day = local_time->tm_mday;
  hour = local_time->tm_hour;
  minute = local_time->tm_min;
  week_day = local_time->tm_wday;
  second = local_time->tm_sec;
}

bool SystemClock::updateByNtp(void)
{
  // Serial.println("---NTP ACCESS---");
  if (!getLocalTime(&time_info,500))
  {
    year = 0;
    month = 0;
    day = 0;
    hour = 0;
    minute = 0;
    week_day = 0;
    second = 0;
    timer = 0;

    // WiFiを繋ぎなおす
    WiFi.disconnect();
    if(WiFi.begin(ssid.c_str(), password.c_str()) != WL_DISCONNECTED) {
        ESP.restart();
    }
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
    ntpset = 0;
    return false;
  }
  else
  {
    year = time_info.tm_year + 1900;
    month = time_info.tm_mon + 1;
    day = time_info.tm_mday;
    hour = time_info.tm_hour;
    minute = time_info.tm_min;
    week_day = time_info.tm_wday;
    second = time_info.tm_sec;
    timer = mktime(&time_info);

    ntpset = millis();
    return true;
  }
}

bool SystemClock::updateClock(void){
  uint32_t diffmillis = millis() - ntpset;

  if (ntpset == 0 || diffmillis > NTP_ACCESS_MS_INTERVAL)
    updateByNtp();
  else
    updateBySoftTimer(diffmillis / 1000);
  
}

SystemClock cl_system_clock;


// 点滅表示フラグ
boolean isHideDisplay(void)
{
  return ((millis() / 500 % 2) == 0);
}

// ISO8601形式の文字列からstruct tmに変換
// 手抜き実装
struct tm isoTotm(String iso) {
  struct tm rettime;

  strptime(iso.c_str(),"%Y-%m-%dT%H:%M:%S%z",&rettime);

  if (iso.indexOf("Z") > -1){
    // 標準時の値なので時差を計算する
    time_t gtime = mktime(&rettime);
    // struct tm -> time_t の変換でも時差が計算されるので、時差を計算して標準時にする
    gtime += gmt_offset_sec;
    struct tm *local = localtime(&gtime);
    return (*local);
  }
  
  return rettime;
}

// フォントデータの表示
// buf(in) : フォント格納アドレス
// ビットパターン表示
// d: 8ビットパターンデータ
void fontDisp(uint16_t x, uint16_t y, uint8_t* buf) {

  uint8_t bn = SDfonts.getRowLength();               // 1行当たりのバイト数取得

  for (uint8_t i = 0; i < SDfonts.getLength(); i += bn ) {
    for (uint8_t j = 0; j < bn; j++) {
      for (uint8_t k = 0; k < 8; k++) {
        if (buf[i + j] & 0x80 >> k) {
          M5.Lcd.drawPixel(x + 8 * j + k , y + i / bn, forecolor);
        } else {
          M5.Lcd.drawPixel(x + 8 * j + k , y + i / bn, backcolor);
        }
      }
    }
  }
}

// 指定した文字列を指定したサイズで表示する
// pUTF8(in) UTF8文字列
// sz(in)    フォントサイズ(8,10,12,14,16,20,24)
void fontDump(uint16_t x, uint16_t y, char* pUTF8, uint8_t sz) {
  uint8_t buf[MAXFONTLEN]; // フォントデータ格納アドレス(最大24x24/8 = 72バイト)

  SDfonts.open();                                   // フォントのオープン
  SDfonts.setFontSize(sz);                          // フォントサイズの設定

  uint16_t mojisu = 0;
  while ( pUTF8 = SDfonts.getFontData(buf, pUTF8) ) { // フォントの取得
    fontDisp(x + mojisu * sz, y, buf);                 // フォントパターンの表示
    ++mojisu;
  }

  SDfonts.close();                                  // フォントのクローズ
}


// 全体初期化
void setup() {

  M5.begin();
  M5.Power.begin();

  // LCDの明るさ設定
  M5.Lcd.setBrightness(25);

  // 設定ファイルの読み込み
  if (!LoadSystemSetting()){
    M5.Lcd.setTextSize(3);
    M5.Lcd.println("  SettingFile\n  Load Failed.");
    M5.Lcd.setTextSize(2);
    M5.Lcd.println("\nPlease Check SD Card.");

    Serial.println("SettingFile Load Failed.");
    displayBottomAria("","RESET","");
    while (true) {
      M5.update();
      // Bボタン
      if (M5.BtnB.wasPressed())  ESP.restart();
      delay(1);
    }
  }
  if (!LoadGroupData()){
    M5.Lcd.setTextSize(3);
    M5.Lcd.println("  GroupData\n  Load Failed.");
    M5.Lcd.setTextSize(2);
    M5.Lcd.println("\nPlease Check SD Card.");

    Serial.println("GroupData Load Failed.");
    displayBottomAria("","RESET","");
    while (true) {
      M5.update();
      // Bボタン
      if (M5.BtnB.wasPressed())  ESP.restart();
      delay(1);
    }
  }

  // WiFiの初期化
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if(WiFi.begin(ssid.c_str(), password.c_str()) != WL_DISCONNECTED) {
    M5.Lcd.setTextSize(3);
    M5.Lcd.println("  WiFi\n  Connect Error.");
    M5.Lcd.setTextSize(2);
    M5.Lcd.println("\nPlease Check ssid & password.");

    Serial.println("WiFi Connect Error.");
    displayBottomAria("","RESET","");
    while (true) {
      M5.update();
      // Bボタン
      if (M5.BtnB.wasPressed())  ESP.restart();
      delay(1);
    }
  }
  while (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      delay(500);
  }

  // TimeZone設定
  configTime(gmt_offset_sec,0,"ntp.nict.jp","time.google.com","ntp.jst.mfeed.ad.jp");
  cl_system_clock.updateClock();

  // 動画情報取得タスク
  xTaskCreatePinnedToCore(YoutubeChecker, "YoutubeTask", 1024 * 8 , NULL, 1, NULL, 0);
  // MP3鳴動タスク
  xTaskCreatePinnedToCore(AudioTask, "AudioTask", 1024 * 4 , NULL, 2, NULL, 1);

  // 日本語フォント初期化
  SDfonts.init(SD_PN);

}


// 画面上部エリアの描画
void displayTopAria() {
  int16_t block_w = 5;
  int16_t block_h = 11;
  uint16_t maskcolor = M5.Lcd.color565(180, 180, 180);
  // バッテリー残量
  int battery = M5.Power.getBatteryLevel();

  // 文字の色、大きさの設定
  M5.Lcd.setTextColor(forecolor,backcolor);
  M5.Lcd.setTextSize(2);

  // 充電中表示
  M5.Lcd.setCursor(10,6);
  M5.Lcd.print(M5.Power.isCharging()? "C" : " ");

  // バッテリーアイコン表示
  // LEFT
  M5.Lcd.drawFastVLine(30,3,18,forecolor);
  M5.Lcd.drawFastVLine(31,3,18,forecolor);
  // RIGHT
  M5.Lcd.drawFastVLine(62,3,18,forecolor);
  M5.Lcd.drawFastVLine(63,3,18,forecolor);
  // TOP
  M5.Lcd.drawFastHLine(30,3,34,forecolor);
  M5.Lcd.drawFastHLine(30,4,34,forecolor);
  // BOTTOM
  M5.Lcd.drawFastHLine(30,20,34,forecolor);
  M5.Lcd.drawFastHLine(30,21,34,forecolor);
  // 出っ張り
  M5.Lcd.fillRect(64,7,3,11,forecolor);

  // BLOCK1
  M5.Lcd.fillRect(34, 7, block_w, block_h, (battery >= 25)? forecolor : backcolor);
  // BLOCK2
  M5.Lcd.fillRect(41, 7, block_w, block_h, (battery >= 50)? forecolor : backcolor);
  // BLOCK3
  M5.Lcd.fillRect(48, 7, block_w, block_h, (battery >= 75)? forecolor : backcolor);
  // BLOCK4
  M5.Lcd.fillRect(55, 7, block_w, block_h, (battery >= 100)? forecolor : backcolor);

  // 電池残量表示
  M5.Lcd.setCursor(77,6);
  M5.Lcd.printf("%d%%",battery);

  // SDカードアクセス
  M5.Lcd.setCursor(190,6);
  M5.Lcd.printf(SDCardAccess? "SD" : "  ");

  // ミュートアイコンの表示
  for (int y = 0; y < 20 ; y++){
    for (int x = 0; x < 26; x++){
      if (!mute) {
        M5.Lcd.drawPixel(230 + x, 3 + y, (muteicon1[y][x])? forecolor : backcolor);
      } else {
        M5.Lcd.drawPixel(230 + x, 3 + y, (muteicon2[y][x])? forecolor : backcolor);
      }
    }
  }

  // WiFi 電波状態表示
  long rssi = WiFi.RSSI();

  int siglevel;
  if (rssi <= -96)  siglevel = 1;
  else if (rssi <= -85) siglevel = 2;
  else if (rssi <= -75) siglevel = 3;
  else siglevel = 4;
  
  M5.Lcd.fillRect(280, 18, 5, 5, (siglevel >= 1)? forecolor : maskcolor);
  M5.Lcd.fillRect(288, 13, 5, 10, (siglevel >= 2)? forecolor : maskcolor);
  M5.Lcd.fillRect(296, 8, 5, 15, (siglevel >= 3)? forecolor : maskcolor);
  M5.Lcd.fillRect(304, 3, 5, 20, (siglevel >= 4)? forecolor : maskcolor);

}


// 画面下部（ボタンガイド）の描画
void displayBottomAria(char* Menu1,char* Menu2,char* Menu3){
  // １文字の幅 = 12
  const int16_t char_w = 12;
  static char str1[9],str2[9],str3[9];
  int16_t start_X;

  M5.Lcd.setTextColor(forecolor,backcolor);
  M5.Lcd.setTextSize(2);

  // 左項目（Aボタン）
  if (strcmp(Menu1,str1)){
    // クリア
    M5.Lcd.setCursor(8 ,218);
    M5.Lcd.print("        ");
    // 描画
    start_X = (96 - (strlen(Menu1) * char_w)) / 2;
    M5.Lcd.setCursor(8 + start_X,218);
    M5.Lcd.print(Menu1);
    strcpy(str1,Menu1);
  }

  // 中項目（Bボタン）
  if (strcmp(Menu2,str2)){
    // クリア
    M5.Lcd.setCursor(112 ,218);
    M5.Lcd.print("        ");
    // 描画
    start_X = (96 - (strlen(Menu2) * char_w)) / 2;
    M5.Lcd.setCursor(112 + start_X,218);
    M5.Lcd.print(Menu2);
    strcpy(str2,Menu2);
  }

  // 右項目（Cボタン）
  if (strcmp(Menu3,str3)){
    // クリア
    M5.Lcd.setCursor(216 ,218);
    M5.Lcd.print("        ");
    // 描画
    start_X = (96 - (strlen(Menu3) * char_w)) / 2;
    M5.Lcd.setCursor(216 + start_X,218);
    M5.Lcd.print(Menu3);
    strcpy(str3,Menu3);
  }

  // 枠の描画
  M5.Lcd.drawRoundRect(5,215,100,21,3,forecolor);
  M5.Lcd.drawRoundRect(109,215,100,21,3,forecolor);
  M5.Lcd.drawRoundRect(213,215,100,21,3,forecolor);
 
}


// 時計画面表示
void displayDateTimeScreen()
{
  // 文字の色、大きさの設定
  M5.Lcd.setTextColor(forecolor,backcolor);
  M5.Lcd.setTextSize(2);

  // 日付表示
  // Year
  DrawNuber7seg_S((cl_system_clock.year / 1000), 10, DATE_Y_POS, forecolor, backcolor);
  DrawNuber7seg_S(((cl_system_clock.year % 1000) / 100), 35, DATE_Y_POS, forecolor, backcolor);
  DrawNuber7seg_S((((cl_system_clock.year % 1000) % 100) / 10), 60, DATE_Y_POS, forecolor, backcolor);
  DrawNuber7seg_S((((cl_system_clock.year % 1000) % 100) % 10), 85, DATE_Y_POS, forecolor, backcolor);

  // スラッシュ
  M5.Lcd.drawLine(110, LCD_SMALL_BAR_LENGTH * 2 + DATE_Y_POS + 5, 120, DATE_Y_POS, forecolor);
  M5.Lcd.drawLine(111, LCD_SMALL_BAR_LENGTH * 2 + DATE_Y_POS + 5, 121, DATE_Y_POS, forecolor);

  // Month
  DrawNuber7seg_S((cl_system_clock.month / 10), 130, DATE_Y_POS, forecolor, backcolor);
  DrawNuber7seg_S((cl_system_clock.month % 10), 155, DATE_Y_POS, forecolor, backcolor);

  // スラッシュ
  M5.Lcd.drawLine(180, LCD_SMALL_BAR_LENGTH * 2 + DATE_Y_POS + 5, 190, DATE_Y_POS, forecolor);
  M5.Lcd.drawLine(181, LCD_SMALL_BAR_LENGTH * 2 + DATE_Y_POS + 5, 191, DATE_Y_POS, forecolor);

  // Day
  DrawNuber7seg_S((cl_system_clock.day / 10), 200, DATE_Y_POS, forecolor, backcolor);
  DrawNuber7seg_S((cl_system_clock.day % 10), 225, DATE_Y_POS, forecolor, backcolor);

  M5.Lcd.setTextSize(3);
  if (cl_system_clock.week_day != cl_system_clock.prev_week_day)
  {
    M5.Lcd.setTextColor(backcolor);
    M5.Lcd.drawString(aweek[cl_system_clock.prev_week_day], 255, DATE_Y_POS + 4);
    M5.Lcd.setTextColor(forecolor);
  }
  M5.Lcd.drawString(aweek[cl_system_clock.week_day], 255, DATE_Y_POS + 4);


  // 時間表示
  // hour
  DrawNuber7seg_L((cl_system_clock.hour / 10), 30, TIME_Y_POS, forecolor, backcolor);
  DrawNuber7seg_L((cl_system_clock.hour % 10), 95, TIME_Y_POS, forecolor, backcolor);

  // コロンの表示関連
  if (isHideDisplay() == false)
  {
    M5.Lcd.fillEllipse(154, TIME_Y_POS + 20, 4, 4, forecolor);
    M5.Lcd.fillEllipse(154, TIME_Y_POS + 50, 4, 4, forecolor);
  }
  else
  {
    M5.Lcd.fillEllipse(154, TIME_Y_POS + 20, 4, 4, backcolor);
    M5.Lcd.fillEllipse(154, TIME_Y_POS + 50, 4, 4, backcolor);
  }

  // minute
  DrawNuber7seg_L((cl_system_clock.minute / 10), 185, TIME_Y_POS, forecolor, backcolor);
  DrawNuber7seg_L((cl_system_clock.minute % 10), 250, TIME_Y_POS, forecolor, backcolor);

  // 表示時間のバックアップ
  cl_system_clock.backupCurrentTime();
}

// アラート画面表示
void displayYoutubeAleartStart(){

  if (is_state_changed){
    is_state_changed = false;

    backcolor = TFT_BLACK;
    forecolor = TFT_WHITE;

    // 画面をクリア
    M5.Lcd.clear(backcolor);

    // サムネイルを表示
    Serial.println(thumbnail);
    M5.Lcd.drawJpgFile(SD,thumbnail,0,30);
  }
}

void loop() {
  int length;
  String dispstring;
  char *dispchar;

  cl_system_clock.updateClock();

  // 画面上部を描画
  displayTopAria();

  switch (disp_page){
    case DISP_CLOCK:

      backcolor = M5.Lcd.color565(0, 180, 0);
      forecolor = TFT_BLACK;

      // 画面表示が変わった場合は全体をクリア
      if (is_state_changed == true) M5.Lcd.fillScreen(backcolor);

      // 時計表示
      displayDateTimeScreen();

      // チェック中のグループ表示
      if (is_state_changed || PrevGroupIndex != CheckGroupIndex) {
        M5.Lcd.fillRect(10,59,310,16,backcolor);

        dispstring = "チェック中:" + groups[CheckGroupIndex].GroupName;
        length = dispstring.length() + 1;
        dispchar = new char[length];
        dispstring.toCharArray(dispchar, length);

        fontDump(10, 59, dispchar, 16);
        delete[] dispchar;

        PrevGroupIndex = CheckGroupIndex;
        is_state_changed = true;
      }

      // 次の配信を表示(漢字の描画は遅いのでなるべく更新しない)
      if (is_state_changed || groups[CheckGroupIndex].isChanged()){
        groups[CheckGroupIndex].Backup();
        M5.Lcd.fillRect(10,165,370,45,backcolor);

        dispstring = "次回配信:" + groups[CheckGroupIndex].GetNextStreamChannelString();
        length = dispstring.length() + 1;
        dispchar = new char[length];
        dispstring.toCharArray(dispchar, length);
        fontDump(10, 165, dispchar, 20);
        delete[] dispchar;

        M5.Lcd.setTextColor(forecolor,backcolor);
        dispstring = groups[CheckGroupIndex].GetNextStreamDateTimeString();
        length = dispstring.length() + 1;
        if (length > 15) {
          M5.Lcd.setTextColor(forecolor,backcolor);
          M5.Lcd.setTextSize(2);
          M5.Lcd.setCursor(120,190);
          M5.Lcd.print(dispstring);
        } else {
          dispchar = new char[length];
          dispstring.toCharArray(dispchar, length);
          fontDump(110, 190, dispchar, 20);
          delete[] dispchar;
        }
      }

      is_state_changed = false;

      // 画面下部を描画
      char* menu1;
      if (mute) menu1 = "UNMUTE";
      else menu1 = "MUTE";

      displayBottomAria(menu1, "<-", "->");
      
      // ボタン判定
      M5.update();
      // Aボタン
      if (M5.BtnA.wasPressed()) {
        mute = !mute;
      }            
      // Bボタン
      if (M5.BtnB.wasPressed()) {
        if (CheckGroupIndex == 0){
          CheckGroupIndex = GroupMax - 1;
        } else {
          CheckGroupIndex--;
        }
      }            
      // Cボタン
      if (M5.BtnC.wasPressed()) {
        if (CheckGroupIndex == (GroupMax - 1)){
          CheckGroupIndex = 0;
        } else {
          CheckGroupIndex++;
        }
      }            
      break;

    case YOUTUBE_ALART:
      displayYoutubeAleartStart();

      is_state_changed = false;

      // 画面下部を描画
      displayBottomAria("RESET","","STOP");

      M5.update();
      // Aボタン
      if (M5.BtnA.wasPressed()) {
        // 時計モードに戻る
        disp_page = DISP_CLOCK;
        is_state_changed = true;
        PlayAlert = false;
      }            
      // Bボタン
      if (M5.BtnB.wasPressed()) {
      }            
      if (M5.BtnC.wasPressed()) {
        // 音だけ止める
        PlayAlert = false;
      }            
      break;
  }

  delay(100);

}


// 動画チェックタスク
void YoutubeChecker(void* arg){

  unsigned long tmstart,tmend;
  long PrevAPICallCount = 0;
  Group* checkingGroup;
  struct video rssvideos[maxVideos];

  while(1){
    if (!Check) {
      // チェック無し
      delay(100);
      continue;
    }

    checkingGroup = &groups[CheckGroupIndex];

    if (WiFi.status() == WL_CONNECTED){
      tmstart = millis();

      for (int i = 0; i < checkingGroup->ChannelCount; i++){
        // RSSを取得する
        getVideoId(checkingGroup->Channels[i].channelID,rssvideos);
        checkingGroup->Channels[i].UpdateVideoInfo(rssvideos);
      }

      tmend = millis();
      Serial.println("RSS処理時間 = " + String(tmend - tmstart) + "ms");

      tmstart = millis();

      for(int c = 0; c < checkingGroup->ChannelCount; c++){
        for(int i = 0; i < maxVideos; i++){
          if (checkingGroup->Channels[c].videos[i].prev_updatetime != checkingGroup->Channels[c].videos[i].updatetime) getVideoInfomation(&checkingGroup->Channels[c].videos[i]);

#if DEBUG_FLAG
          Serial.printf("C%d-%02d番目",c,(i + 1));
          Serial.print(" videoid = " + checkingGroup->Channels[c].videos[i].videoid);
          Serial.print(" : status = " + String(checkingGroup->Channels[c].videos[i].status));
          Serial.print(" : update = " + checkingGroup->Channels[c].videos[i].prev_updatetime);
          Serial.print(" / " + checkingGroup->Channels[c].videos[i].updatetime);
          Serial.printf(" : starttime = %d/%02d/%02d %02d:%02d\n" ,checkingGroup->Channels[c].videos[i].starttime.tm_year + 1900
                                                                  ,checkingGroup->Channels[c].videos[i].starttime.tm_mon + 1
                                                                  ,checkingGroup->Channels[c].videos[i].starttime.tm_mday
                                                                  ,checkingGroup->Channels[c].videos[i].starttime.tm_hour
                                                                  ,checkingGroup->Channels[c].videos[i].starttime.tm_min);
#endif

          if (checkingGroup->Channels[c].videos[i].status == UPCOMING ){
            if (checkingGroup->Channels[c].videos[i].later() < 0){
              // 配信が始まっている可能性があるのでAPIで状態を取得する
              getVideoInfomation(&checkingGroup->Channels[c].videos[i]);
            }
          }
        }
      }

      tmend = millis();
      Serial.println("API処理時間 = " + String(tmend - tmstart) + "ms");

      // 次の配信を通知すべきかチェック
      if (checkingGroup->CheckNextStream()){
        // アラート表示に変更

        // サムネイルを取得する
        if (downloadthumbnail(thumbnailFileName, checkingGroup->NextStreamvideoid)){
          thumbnail = thumbnailFileName;
        } else {
          thumbnail = noimageFileName;
        }

        // 画面をアラームモードに変更する
        disp_page = YOUTUBE_ALART;
        is_state_changed = true;

        // アラート鳴動
        AlertFileName = checkingGroup->GetAlertFileName();
        PlayAlert = true;

      }
    }

    if ( PrevAPICallCount != APICallCount ){
      PrevAPICallCount = APICallCount;

      // 変更があったのでファイルに保存する
      checkingGroup->SaveGroup();
    }

    // Interval分/2毎に動作させる
    for (int w = 0; w < (6000 * Interval) /2 ; w++) {
      if (checkingGroup != &groups[CheckGroupIndex]) {
        checkingGroup->SaveGroup();
        break;
      }

      delay(10);
    }
  }

}


/****************************************************************************************
 *
 *   RSSからvideoidを取得する
 *       struct channelinfomation*　：　取得するチャンネル
 * 
****************************************************************************************/
void getVideoId(String channelID,struct video* videos) {

  String rssUrl = "/feeds/videos.xml?channel_id=" + channelID;
  const char* rssHost = "www.youtube.com";

  Serial.print("getting RSS! : https://");
  Serial.print(rssHost);
  Serial.println(rssUrl);

  // Creating TCP connections
  WiFiClientSecure client;
   
  if (!client.connect(rssHost, httpsport)) {
    Serial.println("connection failed");
    return;
  }

  // Reading reply from server
  int cnt = 0;
  char c;
  String work;

  // Requesting to the server
  client.print("GET " + rssUrl + " HTTP/1.1\r\nHost: " + rssHost + "\r\nConnection: close\r\n\r\n");

  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      client.stop();
      Serial.println("connection timed out");
      return;
    }
    if (WiFi.status() != WL_CONNECTED) return;
  }

  // ヘッダを読み飛ばす
  while (client.connected()) {
    work = client.readStringUntil('\n');

    if (work == "\r") {
      break;
    }
  }
  delay(1);
  
  // RSSを保存するファイル
  SDCardAccess = true;
  File file = SD.open("/rss.xml", FILE_WRITE);
  if(!file)
  {
    Serial.println("SDCard Write Error.");
    SDCardAccess = false;
    return;
  }

  unsigned long datasize = 0,readsize = 0;
  char buff[5];
  while (client.available()) {

    // 送信されるサイズを取得
    client.readBytes(buff,4);
    buff[4] = NULL;

    // 指定バイト数の後ろに\r\nがあるので読み飛ばす
    client.read();
    client.read();

    datasize = strtoul(buff,NULL,16);
    readsize = 0;
    if (datasize == 0) break;

    // 指定バイト数まで読み込む
    while (readsize < datasize){
      c = client.read();
      file.write(c);
      readsize++;

      if((readsize % 1024) == 0) delay(1);
    }

    // 指定バイト数の後ろに\r\nがあるので読み飛ばす
    client.read();
    client.read();

  }
  // 最後まで受信しきる
  while (client.available()){
    c = client.read();
    file.write(c);
  }

  client.stop();
  file.close();
  SDCardAccess = false;

  SDCardAccess = true;
  file = SD.open("/rss.xml", FILE_READ);

  while (file.available()) {

    if (file.find("<yt:videoId>")){
      work = file.readStringUntil('<');
      videos[cnt].videoid = work;

      if (file.find("<updated>")){
        work = file.readStringUntil('<');
        videos[cnt].updatetime = work;
      }

      if (file.find("</entry>")){
        // 次の動画に
        cnt++;

        if (cnt == 15) break;
      }
    }

    if (millis() - timeout > 10000) {
      Serial.println("\r\nTime Out!");
      break;
    }
    delay(1);
  }
  file.close();
  SDCardAccess = false;
  Serial.println("RSS End.");
}


/****************************************************************************************
 *
 *   YouTube APIからビデオ情報（配信状態、配信開始予定日）を取得する
 *       struct videoinfomation* vinfo : 情報を取得するビデオ情報
 * 
****************************************************************************************/
bool getVideoInfomation(struct videoinfomation* vinfo){
  const char* apiHost = "www.googleapis.com";
  char c;
  String line;
  bool StartTimeRecive = false;

  // 配信状態が未定の物、配信予定の物だけ処理する
  if (vinfo->videoid != "" && vinfo->status != NONE) {
    // Creating TCP connections
    WiFiClientSecure client;
    
    if (!client.connect(apiHost, httpsport)) {
      Serial.println("[getVideoInfomation] connection failed");
      return false;
    }

    String url = "/youtube/v3/videos?part=snippet,liveStreamingDetails&id=" + String(vinfo->videoid) + "&key=" + String(apiKey);

    // Sending request
    client.print("GET " + url + " HTTP/1.1\r\nHost: " + apiHost + "\r\nConnection: close\r\n\r\n");

    APICallCount++;

    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 5000) {
        client.stop();
        Serial.println("connection timed out");
        break;
      }
    }

    // ヘッダーを読み飛ばす
    while (client.connected()) {
      line = client.readStringUntil('\n');
      if (line == "\r") {
        break;
      }
    }

    // Reading reply from server
    line = "";
    while (client.available()) {
      c = client.read();
      while ( c != '\n') {
        line += String(c);
        c = client.read();
        // delay(1);
        if (WiFi.status() != WL_CONNECTED) {
          client.stop();
          return false;
        }
      }
      // Serial.println(line);

      // 配信状態の取得
      if (line.indexOf("liveBroadcastContent") > -1) {
        //Serial.println("配信状態取得");
        if (line.indexOf("upcoming") > -1) {
          // 配信予定
          vinfo->status = UPCOMING;
        } else if (line.indexOf("\"live\"") > -1) {
          // Live中
          vinfo->status = LIVE;
        } else {
          // 配信終了
          vinfo->status = NONE;
        }
      }

      // 開始時間の取得
      if (line.indexOf("\"scheduledStartTime\": \"") > -1){
        line.trim();
        line.replace("\"scheduledStartTime\": \"","");
        line.replace("\"","");
        line.trim();
        vinfo->starttime = isoTotm(line);
        StartTimeRecive = true;
        break;
      }
      line = "";
      delay(1);
    }
    // 最後まで受信しきる
    while ( client.available()) {
      c = client.read();
    }
    client.stop();
    
    if (!StartTimeRecive) {
      vinfo->starttime = isoTotm("2000-01-01T09:00:00Z");
    }
  } else {
    Serial.println("API情報取得キャンセル video id = " + String(vinfo->videoid));
  }

  return true;
}


/****************************************************************************************
 *
 *   指定videoIDのサムネイルを取得する
 *       String fileName : SDカードに保存するファイル名
 *       char* videoid  : サムネイルを取得するvideoID
 * 
****************************************************************************************/
bool downloadthumbnail(String fileName, String videoid){
  const char* thumbnailHost = "img.youtube.com";
  String url = "/vi/" + videoid + "/mqdefault.jpg";   // 320x180のサムネイル用URL

  Serial.print("getting thumbnail! : https://");
  Serial.print(thumbnailHost);
  Serial.println(url);

  // 書き込みファイルの準備
  SDCardAccess = true;
  File file = SD.open(fileName, FILE_WRITE);
  if(!file)
  {
    Serial.println("SD Card Write Error.");
    SDCardAccess = false;
    return false;
  }

  WiFiClientSecure client;
  
  if(!client.connect(thumbnailHost,httpsport)){
    Serial.println("[HTTPS] connect failed.");
    file.close();
    return false;
  }

  // GETリクエストを送信 
  client.print("GET " + url + " HTTP/1.0\r\nHost: " + String(thumbnailHost) + "\r\nConnection: close\r\n\r\n");

  long image_size;  // 受信するイメージファイルのサイズ(byte)
  String line;

  // ヘッダーを読む
  while (client.connected()) {
    line = client.readStringUntil('\n');
    // Serial.print(line);

    // 画像のサイズを取得
    if (line.indexOf("Content-Length: ") > -1) {
      line.replace("Content-Length: ", "");
      line.replace("\n", "");
      line.replace("\r", "");
      image_size = atol(line.c_str());
    }
    if (line == "\r") {
      break;
    }
  }
  Serial.println("Image Size = " + String(image_size) + "Byte");

  int recvbytes= 0;
  const size_t bufsize = 512;
  uint8_t buf[bufsize];
  size_t readsize;

  unsigned long downloadstart = millis();

  while (image_size!=0) {
    if (client.available()){
      readsize = client.read(buf,bufsize);
      file.write(buf,readsize);
      image_size = image_size - readsize;
      delay(1);
    } else {
      delay(1);
    }

    // 1分以内にダウンロードできないもしくは通信中にWiFiが切れたら異常終了
    if (((millis() - downloadstart) > 60000) || (WiFi.status() != WL_CONNECTED)){
      Serial.println("DownLoad Timeout!");
      file.close();
      client.stop();
      SDCardAccess = false;
      return false;
    }
  }
  Serial.println("DownLoad End!");

  // 正常終了
  file.close();
  SDCardAccess = false;
  client.stop();
  return true;
}


/****************************************************************************************
 *
 *   MP3再生タスク
 *
****************************************************************************************/
void AudioTask(void* arg){

  while(1){

    // アラート音声の再生
    if (!mute && PlayAlert){
      AudioGeneratorMP3 *Alertmp3;
      AudioFileSourceSD *Alertfile;
      AudioOutputI2S *Alertout;
      AudioFileSourceID3 *Alertid3;

      // 音声の読み込み
      Alertfile = new AudioFileSourceSD(AlertFileName.c_str());
      Alertid3 = new AudioFileSourceID3(Alertfile);
      Alertout = new AudioOutputI2S(0, 1);
      Alertout->SetOutputModeMono(true);
      Alertout->SetGain(0.3);
      Alertmp3 = new AudioGeneratorMP3();
      Alertmp3->begin(Alertid3, Alertout);

      // 再生が終わるまでループ
      // 再生フラグがfalseになっても終了
      while(Alertmp3->isRunning()) {
        if (!Alertmp3->loop() || !PlayAlert)
        {
          Alertmp3->stop();
          delay(100);
        }
        delay(1);
      }
    }

    delay(10);
  }

}

