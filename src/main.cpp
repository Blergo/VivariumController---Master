#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_SPIDevice.h>
#include <RTClib.h>
#include <time.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lv_conf.h>
#include <lvgl.h>
#include <EEPROM.h>
#include <ModbusRtu.h>

#define TOUCH_CS  34
#define TOUCH_IRQ 35
#define MODBUS_BAUD 9600
#define MODBUS_TIMEOUT 1000
#define MODBUS_POLLING 1000
#define MODBUS_RETRY 10
#define MODBUS_TXEN -1
#define MODBUS_TX 25
#define MODBUS_RX 26

uint16_t scandata[2];
uint16_t senddata[1];
uint16_t scandata1[2];
uint16_t resdata[8];
Modbus master(0,Serial1,0);
modbus_t telegram;
unsigned long scanwait;
unsigned long scandelay = 1000;
unsigned long reswait;
unsigned long resdelay = 1000;
bool modbusrun;
int CurSlaves = 1;

int SlaveID = 0;
int Function;
int RegAdd;
int RegNo;
uint16_t *ResVar;

typedef struct {
	int SlaveID;
  int Function;
  int RegAdd;
  int RegNo;
  uint16_t *ResVar;
} ModbusParam;

ModbusParam Param;

char ntpServer[] = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;
char ssid[32];
char password[32];

const int MY_DISP_HOR_RES = 320;
const int MY_DISP_VER_RES = 240;

TFT_eSPI tft = TFT_eSPI(); 
XPT2046_Touchscreen ts(TOUCH_CS);
RTC_DS1307 rtc;

float xCalM = 0.0, yCalM = 0.0;
float xCalC = 0.0, yCalC = 0.0;

const int blPin = 32;
const int blFreq = 5000;
const int blChannel = 0;
const int blResolution = 8;
int curDuty = 0;
int setDuty = 255;
int blDuration = 20000;
int blTimeout = 0;

bool WiFiState;
bool NTPState;
bool SlaveConf;

uint8_t WiFiStatus;

lv_obj_t * tabview;
lv_obj_t * tab1;
lv_obj_t * tab2;
lv_obj_t * keyboard;
lv_obj_t * MsgBox;

lv_obj_t * WiFiSetBtn;
lv_obj_t * WiFiSetLabel;
lv_obj_t * SlaveSetBtn;
lv_obj_t * SlaveSetLabel;
lv_obj_t * SysSetBtn;
lv_obj_t * SysSetLabel;
lv_obj_t * FunctSetBtn;
lv_obj_t * FunctSetLabel;
lv_obj_t * CalBtn;
lv_obj_t * CalLabel;
lv_obj_t * SaveBtn;
lv_obj_t * SaveLabel;

lv_obj_t * WiFiSSID;
lv_obj_t * WiFiSSIDLabel;
lv_obj_t * WiFiPass;
lv_obj_t * WiFiPassLabel;
lv_obj_t * WiFiConnected;
lv_obj_t * WiFiFailed;
lv_obj_t * WiFiSetBkBtn;
lv_obj_t * WiFiSetBkLabel;
lv_obj_t * WiFiCnctBtn;
lv_obj_t * WiFiCnctLabel;

lv_obj_t * SlaveSelect;
lv_obj_t * SlaveSetBkBtn;
lv_obj_t * SlaveSetBkLabel;
lv_obj_t * SlaveScanBtn;
lv_obj_t * SlaveScanLabel;

lv_obj_t * SysSetBkBtn;
lv_obj_t * SysSetBkLabel;
lv_obj_t * NTPsw;
lv_obj_t * NTPlabel;
lv_obj_t * WiFisw;
lv_obj_t * WiFilabel;

lv_obj_t * FunctSetBkBtn;
lv_obj_t * FunctSetBkLabel;
lv_obj_t * FunctionSelect;
lv_obj_t * NewFunctBtn;
lv_obj_t * NewFunctLabel;
lv_obj_t * ConfFunctBtn;
lv_obj_t * ConfFunctLabel;

lv_obj_t * TempLabel;
lv_obj_t * HumLabel;

TaskHandle_t NTPHandle;

static lv_disp_draw_buf_t disp_buf;
static lv_color_t buf_1[MY_DISP_HOR_RES * 10];
static lv_disp_drv_t disp_drv;
static lv_disp_t *disp;
static lv_indev_drv_t indev_drv;

struct tm timeinfo;

union Pun {float f; uint32_t u;};

void ConfigureSlave(void * parameters1);
void initWiFi(void * parameters2);
void UpdateSlct (void * parameters3);
void CheckRTC(void * parameters4);
void TFTUpdate(void * parameters5);
void disWiFi(void * parameters6);
void SaveSettings(void * parameters7);
void ModbusWorker(void * parameters8);
void MainWork(void * Parameters9);
void PairSlave(void * Parameters10);
void UpdateFunct(void * Parameters11);

class ScreenPoint {

  public:
  int16_t x;
  int16_t y;
 
  ScreenPoint(){}
 
  ScreenPoint(int16_t xIn, int16_t yIn){
    x = xIn;
    y = yIn;
    }
};

ScreenPoint getScreenCoords(int16_t x, int16_t y){
int16_t xCoord = round((x * xCalM) + xCalC);
int16_t yCoord = round((y * yCalM) + yCalC);
if(xCoord < 0) xCoord = 0;
if(xCoord >= tft.width()) xCoord = tft.width() - 1;
if(yCoord < 0) yCoord = 0;
if(yCoord >= tft.height()) yCoord = tft.height() - 1;
return(ScreenPoint(xCoord, yCoord));
}

float decodeFloat(uint16_t *regs)
{
    union Pun pun;
    pun.u = ((uint32_t)regs[0] << 16) | regs[1];
    return pun.f;
}

String decodeAbility(String AbilityCode){
  if(AbilityCode == "0"){
    String a = "Not Detected";
    return a;
  }
  else if(AbilityCode == "6"){
    String a = "1 x DHT22 Sensor";
    return a;
  }
  else if(AbilityCode == "82"){
    String a = "2 x Relay";
    return a;
  }
  else{
    String a = "Error - Not Defined!";
    return a;
  }
}

void calibrateTouchScreen(){
  TS_Point p;
  int16_t x1,y1,x2,y2;
 
  tft.fillScreen(ILI9341_BLACK);
  while(ts.touched());
  tft.drawFastHLine(10,20,20,ILI9341_RED);
  tft.drawFastVLine(20,10,20,ILI9341_RED);
  while(!ts.touched());
  delay(50);
  p = ts.getPoint();
  x1 = p.x;
  y1 = p.y;
  tft.drawFastHLine(10,20,20,ILI9341_BLACK);
  tft.drawFastVLine(20,10,20,ILI9341_BLACK);
  delay(500);
  while(ts.touched());
  tft.drawFastHLine(tft.width() - 30,tft.height() - 20,20,ILI9341_RED);
  tft.drawFastVLine(tft.width() - 20,tft.height() - 30,20,ILI9341_RED);
  while(!ts.touched());
  delay(50);
  p = ts.getPoint();
  x2 = p.x;
  y2 = p.y;
  tft.drawFastHLine(tft.width() - 30,tft.height() - 20,20,ILI9341_BLACK);
  tft.drawFastVLine(tft.width() - 20,tft.height() - 30,20,ILI9341_BLACK);
  int16_t xDist = tft.width() - 40;
  int16_t yDist = tft.height() - 40;

  xCalM = (float)xDist / (float)(x2 - x1);
  xCalC = 20.0 - ((float)x1 * xCalM);

  yCalM = (float)yDist / (float)(y2 - y1);
  yCalC = 20.0 - ((float)y1 * yCalM);

  lv_obj_invalidate(tabview);
}

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors(&color_p->full, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(disp);
}

void touchpad_read(lv_indev_drv_t * drv, lv_indev_data_t*data){
  if (ts.touched()) {
    ScreenPoint sp = ScreenPoint();
    TS_Point p = ts.getPoint();
    sp = getScreenCoords(p.x, p.y);
    data->point.x = sp.x;
    data->point.y = sp.y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED; 
  }
}

static void event_handler_btn(lv_event_t * e){
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t * obj = lv_event_get_target(e);
    if(code == LV_EVENT_CLICKED && obj == CalBtn){
      calibrateTouchScreen();
    }
    else if(code == LV_EVENT_CLICKED && obj == SaveBtn){
      MsgBox = lv_msgbox_create(NULL, NULL, "Settings Saved!", NULL, true);
      lv_obj_center(MsgBox);
      xTaskCreate(SaveSettings, "Save Settings", 1500, NULL, 2, NULL);
    }
    else if(code == LV_EVENT_CLICKED && obj == WiFiSetBtn){
      lv_textarea_set_placeholder_text(WiFiSSID, ssid);
      lv_textarea_set_placeholder_text(WiFiPass, password);
      lv_obj_add_flag(CalBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(SaveBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(WiFiSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(SlaveSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(SysSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(FunctSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(WiFiSetBkBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(WiFiCnctBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(WiFiSSID, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(WiFiSSIDLabel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(WiFiPass, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(WiFiPassLabel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    else if(code == LV_EVENT_CLICKED && obj == SlaveSetBtn){
      MsgBox = lv_msgbox_create(NULL, NULL, "Loading!", NULL, false);
      lv_obj_center(MsgBox);
      xTaskCreate(UpdateSlct, "Update Slave Select", 1000, NULL, 5, NULL);
    }
    else if(code == LV_EVENT_CLICKED && obj == SlaveScanBtn){
      MsgBox = lv_msgbox_create(NULL, NULL, "Scanning!", NULL, false);
      lv_obj_center(MsgBox);
      xTaskCreate(PairSlave, "Pair New Slave", 800, NULL, 5, NULL);
    }
    else if(code == LV_EVENT_CLICKED && obj == SlaveSetBkBtn){
      lv_obj_clear_flag(CalBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(SaveBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(WiFiSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(SlaveSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(SysSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(FunctSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(SlaveSetBkBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(SlaveSelect, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(SlaveScanBtn, LV_OBJ_FLAG_HIDDEN);
    }
    else if(code == LV_EVENT_CLICKED && obj == WiFiSetBkBtn){
      lv_obj_clear_flag(CalBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(SaveBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(WiFiSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(SlaveSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(SysSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(FunctSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(WiFiSetBkBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(WiFiCnctBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(WiFiSSID, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(WiFiSSIDLabel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(WiFiPass, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(WiFiPassLabel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
      lv_textarea_set_placeholder_text(WiFiSSID, ssid);
      lv_textarea_set_placeholder_text(WiFiPass, password);
    }
    else if(code == LV_EVENT_CLICKED && obj == SysSetBkBtn){
      lv_obj_add_flag(WiFisw, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(WiFilabel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(NTPsw, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(NTPlabel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(SysSetBkBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(SysSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(CalBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(SaveBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(WiFiSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(SlaveSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(FunctSetBtn, LV_OBJ_FLAG_HIDDEN);
    }
    else if(code == LV_EVENT_CLICKED && obj == FunctSetBkBtn){
      lv_obj_add_flag(FunctSetBkBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(FunctionSelect, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(NewFunctBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ConfFunctBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(SysSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(CalBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(SaveBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(WiFiSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(SlaveSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(FunctSetBtn, LV_OBJ_FLAG_HIDDEN);
    }
    else if(code == LV_EVENT_CLICKED && obj == SysSetBtn){
      lv_obj_clear_flag(WiFisw, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(WiFilabel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(NTPsw, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(NTPlabel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(SysSetBkBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(SysSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(FunctSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(CalBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(SaveBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(WiFiSetBtn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(SlaveSetBtn, LV_OBJ_FLAG_HIDDEN);
    }
    else if(code == LV_EVENT_CLICKED && obj == FunctSetBtn){
      xTaskCreate(UpdateFunct, "Update Function Select", 800, NULL, 5, NULL);
    }
    else if(code == LV_EVENT_CLICKED && obj == WiFiCnctBtn){
      strcpy(ssid, lv_textarea_get_text(WiFiSSID));
      strcpy(password, lv_textarea_get_text(WiFiPass));
      WiFiStatus = 0;
      xTaskCreate(initWiFi, "Initialize WiFi", 1400, NULL, 4, NULL);
      
      while(WiFiStatus == 0){
        vTaskDelay(50);
      }

      switch(WiFiStatus) {
        case 1: 
          lv_obj_clear_flag(WiFiConnected, LV_OBJ_FLAG_HIDDEN);
        break;
        case 2: 
          lv_obj_clear_flag(WiFiFailed, LV_OBJ_FLAG_HIDDEN);
        break;
      }
      WiFiStatus = 0;
    } 
}

static void event_handler_sw(lv_event_t * e){

    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    if(code == LV_EVENT_VALUE_CHANGED) {
      if(obj == WiFisw && lv_obj_has_state(obj, LV_STATE_CHECKED)) {
        lv_obj_clear_state(NTPsw, LV_STATE_DISABLED);
        lv_obj_clear_state(WiFiSetBtn, LV_STATE_DISABLED);
        xTaskCreate(initWiFi, "Initialize WiFi", 1400, NULL, 4, NULL);
        WiFiState = true;
      }
      else if(obj == WiFisw){
        lv_obj_clear_state(NTPsw, LV_STATE_CHECKED);
        lv_obj_add_state(NTPsw, LV_STATE_DISABLED);
        lv_obj_add_state(WiFiSetBtn, LV_STATE_DISABLED);
        xTaskCreate(disWiFi, "Disable WiFi", 1200, NULL, 4, NULL);
        WiFiState = false;
        if(NTPState == true){
          vTaskDelete(NTPHandle);
          NTPState = false;
        }
      }
      else if(obj == NTPsw && lv_obj_has_state(obj, LV_STATE_CHECKED)) {
        xTaskCreate(CheckRTC, "Check RTC", 1200, NULL, 2, &NTPHandle);      
      }
      else if (obj == NTPsw){
        vTaskDelete(NTPHandle);
        NTPState = false;
      }
    }
}

static void ta_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target(e);
    if(code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        if(keyboard != NULL) lv_keyboard_set_textarea(keyboard, ta);
    }
}

static void event_cb_mbox(lv_event_t * e)
{
  lv_obj_t * obj = lv_event_get_current_target(e);
  Serial.println(lv_msgbox_get_active_btn_text(obj));
  lv_msgbox_close(MsgBox);
  String slavestr;
  bool SlaveSet = 1;
  while (SlaveSet == 1){
    if (modbusrun == 0){
      senddata[0] = ++CurSlaves;
      SlaveID = 1;
      Function = 6;
      RegAdd = 0;
      RegNo = 2;
      Param.SlaveID = SlaveID;
      Param.Function = Function;
      Param.RegAdd = RegAdd;
      Param.RegNo = RegNo;
      Param.ResVar = senddata+0;
      xTaskCreate(ModbusWorker, "Modbus Worker", 800, &Param, 4, NULL);
      vTaskDelay(10);
      EEPROM.put(82, CurSlaves);
      EEPROM.commit();
      slavestr = String("ID: " + String(CurSlaves) + " - " + decodeAbility(String(scandata[1])));
      lv_dropdown_add_option(SlaveSelect, slavestr.c_str(), LV_DROPDOWN_POS_LAST);
      scandata[0] = 0;
      SlaveSet = 0;
    }
  vTaskDelay(10);
  }
}

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600, SERIAL_8N1, MODBUS_RX, MODBUS_TX);
  tft.init();
  tft.setRotation(1);
  ts.begin();
  ts.setRotation(1);

  ledcSetup(blChannel, blFreq, blResolution);
  ledcAttachPin(blPin, blChannel);
  blTimeout = millis()+blDuration;

  EEPROM.begin(95);
  EEPROM.get(0, xCalM);
  EEPROM.get(4, yCalM);
  EEPROM.get(8, xCalC);
  EEPROM.get(12, yCalC);
  EEPROM.get(16, WiFiState);
  EEPROM.get(17, NTPState);
  EEPROM.get(18, ssid);
  EEPROM.get(50, password);
  //EEPROM.put(82, 1);
  //EEPROM.commit();
  EEPROM.get(82, CurSlaves);

  lv_init();
  lv_disp_draw_buf_init(&disp_buf, buf_1, NULL, MY_DISP_HOR_RES*10);
  lv_disp_drv_init(&disp_drv);
  disp_drv.draw_buf = &disp_buf;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.hor_res = MY_DISP_HOR_RES;
  disp_drv.ver_res = MY_DISP_VER_RES;
  disp = lv_disp_drv_register(&disp_drv);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touchpad_read;
  lv_indev_drv_register(&indev_drv);

  tabview = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 30);

  tab1 = lv_tabview_add_tab(tabview, "Test");
  tab2 = lv_tabview_add_tab(tabview, "Settings");
    
  WiFisw = lv_switch_create(tab2);
  lv_obj_add_event_cb(WiFisw, event_handler_sw, LV_EVENT_ALL, NULL);
  lv_obj_align(WiFisw, LV_ALIGN_TOP_LEFT, 10, 0);
  lv_obj_add_flag(WiFisw, LV_OBJ_FLAG_HIDDEN);

  WiFilabel = lv_label_create(tab2);
  lv_label_set_text(WiFilabel, "WiFi");
  lv_obj_align_to(WiFilabel, WiFisw, LV_ALIGN_OUT_RIGHT_MID, 20, 0);
  lv_obj_add_flag(WiFilabel, LV_OBJ_FLAG_HIDDEN);

  WiFiSetBtn = lv_btn_create(tab2);
  lv_obj_add_event_cb(WiFiSetBtn, event_handler_btn, LV_EVENT_ALL, NULL);
  lv_obj_align(WiFiSetBtn, LV_ALIGN_TOP_RIGHT, -10, 0);

  WiFiSetLabel = lv_label_create(WiFiSetBtn);
  lv_label_set_text(WiFiSetLabel, "WiFi Settings");
  lv_obj_center(WiFiSetLabel);

  SlaveSetBtn = lv_btn_create(tab2);
  lv_obj_add_event_cb(SlaveSetBtn, event_handler_btn, LV_EVENT_ALL, NULL);
  lv_obj_align_to(SlaveSetBtn, WiFiSetBtn, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);

  SlaveSetLabel = lv_label_create(SlaveSetBtn);
  lv_label_set_text(SlaveSetLabel, "Slave Settings");
  lv_obj_center(SlaveSetLabel);

  NTPsw = lv_switch_create(tab2);
  lv_obj_align_to(NTPsw, WiFisw, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
  lv_obj_add_event_cb(NTPsw, event_handler_sw, LV_EVENT_ALL, NULL);
  lv_obj_add_flag(NTPsw, LV_OBJ_FLAG_HIDDEN);

  NTPlabel = lv_label_create(tab2);
  lv_label_set_text(NTPlabel, "NTP");
  lv_obj_align_to(NTPlabel, NTPsw, LV_ALIGN_OUT_RIGHT_MID, 20, 0);
  lv_obj_add_flag(NTPlabel, LV_OBJ_FLAG_HIDDEN);

  SysSetBtn = lv_btn_create(tab2);
  lv_obj_add_event_cb(SysSetBtn, event_handler_btn, LV_EVENT_ALL, NULL);
  lv_obj_align(SysSetBtn, LV_ALIGN_TOP_LEFT, 10, 0);

  SysSetLabel = lv_label_create(SysSetBtn);
  lv_label_set_text(SysSetLabel, "System Settings");
  lv_obj_center(SysSetLabel);

  SysSetBkBtn = lv_btn_create(tab2);
  lv_obj_add_event_cb(SysSetBkBtn, event_handler_btn, LV_EVENT_ALL, NULL);
  lv_obj_align(SysSetBkBtn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  lv_obj_add_flag(SysSetBkBtn, LV_OBJ_FLAG_HIDDEN);

  SysSetBkLabel = lv_label_create(SysSetBkBtn);
  lv_label_set_text(SysSetBkLabel, "Back");
  lv_obj_center(SysSetBkLabel);

  CalBtn = lv_btn_create(tab2);
  lv_obj_add_event_cb(CalBtn, event_handler_btn, LV_EVENT_ALL, NULL);
  lv_obj_align_to(CalBtn, SysSetBtn, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);

  CalLabel = lv_label_create(CalBtn);
  lv_label_set_text(CalLabel, "Calibrate Touch");
  lv_obj_center(CalLabel);

  SaveBtn = lv_btn_create(tab2);
  lv_obj_add_event_cb(SaveBtn, event_handler_btn, LV_EVENT_ALL, NULL);
  lv_obj_align(SaveBtn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);

  SaveLabel = lv_label_create(SaveBtn);
  lv_label_set_text(SaveLabel, "Save Settings");
  lv_obj_center(SaveLabel);

  FunctSetBtn = lv_btn_create(tab2);
  lv_obj_add_event_cb(FunctSetBtn, event_handler_btn, LV_EVENT_ALL, NULL);
  lv_obj_align_to(FunctSetBtn, CalBtn, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);

  FunctSetLabel = lv_label_create(FunctSetBtn);
  lv_label_set_text(FunctSetLabel, "Functions");
  lv_obj_center(FunctSetLabel);

  FunctSetBkBtn = lv_btn_create(tab2);
  lv_obj_add_event_cb(FunctSetBkBtn, event_handler_btn, LV_EVENT_ALL, NULL);
  lv_obj_align(FunctSetBkBtn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  lv_obj_add_flag(FunctSetBkBtn, LV_OBJ_FLAG_HIDDEN);

  FunctSetBkLabel = lv_label_create(FunctSetBkBtn);
  lv_label_set_text(FunctSetBkLabel, "Back");
  lv_obj_center(FunctSetBkLabel);

  FunctionSelect = lv_dropdown_create(tab2);
  lv_obj_set_width(FunctionSelect, MY_DISP_HOR_RES-50); 
  lv_obj_align(FunctionSelect, LV_ALIGN_TOP_LEFT, 10, 10);
  lv_obj_add_event_cb(FunctionSelect, NULL, LV_EVENT_ALL, NULL);
  lv_obj_add_flag(FunctionSelect, LV_OBJ_FLAG_HIDDEN);

  NewFunctBtn = lv_btn_create(tab2);
  lv_obj_add_event_cb(NewFunctBtn, event_handler_btn, LV_EVENT_ALL, NULL);
  lv_obj_align(NewFunctBtn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  lv_obj_add_flag(NewFunctBtn, LV_OBJ_FLAG_HIDDEN);

  NewFunctLabel = lv_label_create(NewFunctBtn);
  lv_label_set_text(NewFunctLabel, "New");
  lv_obj_center(NewFunctLabel);

  ConfFunctBtn = lv_btn_create(tab2);
  lv_obj_add_event_cb(ConfFunctBtn, event_handler_btn, LV_EVENT_ALL, NULL);
  lv_obj_align(ConfFunctBtn, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_add_flag(ConfFunctBtn, LV_OBJ_FLAG_HIDDEN);

  ConfFunctLabel = lv_label_create(ConfFunctBtn);
  lv_label_set_text(ConfFunctLabel, "Configure");
  lv_obj_center(ConfFunctLabel);

  WiFiSSID = lv_textarea_create(tab2);
  lv_textarea_set_one_line(WiFiSSID, true);
  lv_textarea_set_password_mode(WiFiSSID, false);
  lv_obj_set_width(WiFiSSID, lv_pct(60));
  lv_textarea_set_max_length(WiFiSSID, 32);
  lv_obj_add_event_cb(WiFiSSID, ta_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_align(WiFiSSID, LV_ALIGN_TOP_LEFT, 0, 10);
  lv_obj_add_flag(WiFiSSID, LV_OBJ_FLAG_HIDDEN);

  WiFiSSIDLabel = lv_label_create(tab2);
  lv_label_set_text(WiFiSSIDLabel, "WiFi SSID:");
  lv_obj_align_to(WiFiSSIDLabel, WiFiSSID, LV_ALIGN_OUT_TOP_LEFT, 0, 0);
  lv_obj_add_flag(WiFiSSIDLabel, LV_OBJ_FLAG_HIDDEN);

  WiFiPass = lv_textarea_create(tab2);
  lv_textarea_set_one_line(WiFiPass, true);
  lv_textarea_set_password_mode(WiFiPass, false);
  lv_obj_set_width(WiFiPass, lv_pct(60));
  lv_textarea_set_max_length(WiFiPass, 32);
  lv_obj_add_event_cb(WiFiPass, ta_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_align_to(WiFiPass, WiFiSSID, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);
  lv_obj_add_flag(WiFiPass, LV_OBJ_FLAG_HIDDEN);

  WiFiPassLabel = lv_label_create(tab2);
  lv_label_set_text(WiFiPassLabel, "WiFi Password:");
  lv_obj_align_to(WiFiPassLabel, WiFiPass, LV_ALIGN_OUT_TOP_LEFT, 0, 0);
  lv_obj_add_flag(WiFiPassLabel, LV_OBJ_FLAG_HIDDEN);

  WiFiCnctBtn = lv_btn_create(tab2);
  lv_obj_add_event_cb(WiFiCnctBtn, event_handler_btn, LV_EVENT_ALL, NULL);
  lv_obj_align(WiFiCnctBtn, LV_ALIGN_TOP_RIGHT, 0, 20);
  lv_obj_add_flag(WiFiCnctBtn, LV_OBJ_FLAG_HIDDEN);

  WiFiCnctLabel = lv_label_create(WiFiCnctBtn);
  lv_label_set_text(WiFiCnctLabel, "Connect");
  lv_obj_center(WiFiCnctLabel);

  WiFiSetBkBtn = lv_btn_create(tab2);
  lv_obj_add_event_cb(WiFiSetBkBtn, event_handler_btn, LV_EVENT_ALL, NULL);
  lv_obj_align_to(WiFiSetBkBtn, WiFiCnctBtn, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);
  lv_obj_add_flag(WiFiSetBkBtn, LV_OBJ_FLAG_HIDDEN);

  WiFiSetBkLabel = lv_label_create(WiFiSetBkBtn);
  lv_label_set_text(WiFiSetBkLabel, "Back");
  lv_obj_center(WiFiSetBkLabel);

  keyboard = lv_keyboard_create(tab2);
  lv_obj_set_size(keyboard, LV_HOR_RES, LV_VER_RES / 3.3);
  lv_keyboard_set_textarea(keyboard, WiFiSSID);
  lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);

  WiFiConnected = lv_msgbox_create(tab2, NULL, "Connection Sucessfull!", NULL, true);
  lv_obj_center(WiFiConnected);
  lv_obj_add_flag(WiFiConnected, LV_OBJ_FLAG_HIDDEN);

  WiFiFailed = lv_msgbox_create(tab2, NULL, "Connection Failed!", NULL, true);
  lv_obj_center(WiFiFailed);
  lv_obj_add_flag(WiFiFailed, LV_OBJ_FLAG_HIDDEN);

  SlaveSelect = lv_dropdown_create(tab2);
  lv_obj_set_width(SlaveSelect, MY_DISP_HOR_RES-50); 
  lv_obj_align(SlaveSelect, LV_ALIGN_TOP_LEFT, 10, 10);
  lv_obj_add_event_cb(SlaveSelect, NULL, LV_EVENT_ALL, NULL);
  lv_obj_add_flag(SlaveSelect, LV_OBJ_FLAG_HIDDEN);

  SlaveSetBkBtn = lv_btn_create(tab2);
  lv_obj_add_event_cb(SlaveSetBkBtn, event_handler_btn, LV_EVENT_ALL, NULL);
  lv_obj_align(SlaveSetBkBtn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  lv_obj_add_flag(SlaveSetBkBtn, LV_OBJ_FLAG_HIDDEN);

  SlaveSetBkLabel = lv_label_create(SlaveSetBkBtn);
  lv_label_set_text(SlaveSetBkLabel, "Back");
  lv_obj_center(SlaveSetBkLabel);

  SlaveScanBtn = lv_btn_create(tab2);
  lv_obj_add_event_cb(SlaveScanBtn, event_handler_btn, LV_EVENT_ALL, NULL);
  lv_obj_align(SlaveScanBtn, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_add_flag(SlaveScanBtn, LV_OBJ_FLAG_HIDDEN);

  SlaveScanLabel = lv_label_create(SlaveScanBtn);
  lv_label_set_text(SlaveScanLabel, "Pair Slave");
  lv_obj_center(SlaveScanLabel);

  TempLabel = lv_label_create(tab1);
  lv_obj_align(TempLabel, LV_ALIGN_TOP_LEFT, 0, 20);
    
  HumLabel = lv_label_create(tab1);
  lv_obj_align_to(HumLabel, TempLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);

  if(!rtc.begin()) {
      Serial.println("Couldn't find RTC!");
      Serial.flush();
      while (1) delay(10);
  }

  if(WiFiState == true){
    lv_obj_add_state(WiFisw, LV_STATE_CHECKED);
    xTaskCreate(initWiFi, "Initialize WiFi", 2200, NULL, 4, NULL);
  }
  else if(WiFiState == false){
    lv_obj_add_state(NTPsw, LV_STATE_DISABLED);
    lv_obj_add_state(WiFiSetBtn, LV_STATE_DISABLED);
  }
  if(NTPState == true){
    lv_obj_add_state(NTPsw, LV_STATE_CHECKED);
    xTaskCreate(CheckRTC, "Check RTC", 1200, NULL, 2, &NTPHandle);
  }

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  xTaskCreate(TFTUpdate, "TFT Update", 2700, NULL, 5, NULL);
  xTaskCreate(MainWork, "Main Worker", 800, NULL, 4, NULL);
}

void initWiFi(void * parameters2) {
  if (WiFi.status() == WL_CONNECTED){
    xTaskCreate(disWiFi, "Disable WiFi", 1200, NULL, 4, NULL);
  }
  while (WiFi.status() == WL_CONNECTED){
    vTaskDelay(100);
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() == WL_CONNECTED) {
    WiFiStatus = 1;  
  } 
    else {
    WiFiStatus = 2;
  }
  vTaskDelete(NULL);
}

void disWiFi(void * parameters6) {
  WiFi.disconnect();
  Serial.print("Disconnecting WiFi ..");
  while (WiFi.status() == WL_CONNECTED) {
    Serial.print('.');
    delay(500);
  }
  WiFi.mode(WIFI_OFF);
  Serial.println("WiFi Disconnected!");
  vTaskDelete(NULL);
}

void ConfigureSlave(void * parameters_1){
  lv_msgbox_close(MsgBox);
  static const char * btns[] ={"Configure", ""};
  MsgBox = lv_msgbox_create(NULL, NULL, "A new slave has been detected!", btns, false);
  lv_obj_add_event_cb(MsgBox, event_cb_mbox, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_center(MsgBox);
  vTaskDelete(NULL);
}

void TFTUpdate(void * parameters5) {
  TickType_t xLastWakeTime1;
  const portTickType xFrequency1 = 100 / portTICK_RATE_MS;
  xLastWakeTime1 = xTaskGetTickCount ();
  for(;;) {
    vTaskDelayUntil( &xLastWakeTime1, xFrequency1 );
    lv_timer_handler();

    if (ts.touched()){
      vTaskDelay(10);
      if (ts.touched()){
        if (blTimeout == 0){
        setDuty=255;
        }
        blTimeout = millis()+blDuration;
      }
    }

    if (millis() > blTimeout && blTimeout != 0){
      blTimeout = 0;
      setDuty = 70;
    }

    if (setDuty != curDuty) {
      ledcWrite(blChannel, setDuty);
      curDuty = setDuty;
    }

    lv_label_set_text_fmt(TempLabel, "Temperature: %.2f", decodeFloat(&resdata[4]));
    lv_label_set_text_fmt(HumLabel, "Humidity: %.2f", decodeFloat(&resdata[6]));
  }
}

void CheckRTC(void * parameters4) {
  NTPState = true;
  Serial.println("NTP Client Running");
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(100);
  }
  TickType_t xLastWakeTime;
  const portTickType xFrequency = 3600000 / portTICK_RATE_MS;
  xLastWakeTime = xTaskGetTickCount ();
  for(;;){
    vTaskDelayUntil( &xLastWakeTime, xFrequency );
    if (! rtc.isrunning()) {
      Serial.println("RTC is NOT running, let's set the time!");
      if(!getLocalTime(&timeinfo)){
        Serial.println("Failed to obtain time");
      }
      Serial.println("Updating RTC..");
      rtc.adjust(DateTime(timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
    }
  }
}

void UpdateSlct(void * parameters3) {
  int count;
  String slavestr;
  for(count = 2;count <= CurSlaves;count++) {
    scandata1[1] = 0;
    if (count != CurSlaves){
      bool Slavescan = 1;
      while (Slavescan == 1){
        vTaskDelay(10);
        if (modbusrun == 0){
          SlaveID = count;
          Function = 3;
          RegAdd = 0;
          RegNo = 2;
          Param.SlaveID = SlaveID;
          Param.Function = Function;
          Param.RegAdd = RegAdd;
          Param.RegNo = RegNo;
          Param.ResVar = scandata1;
          xTaskCreate(ModbusWorker, "Modbus Worker", 800, &Param, 4, NULL);
          Slavescan = 0;
        }
      }
      vTaskDelay(20);
      while (modbusrun == 1){
        vTaskDelay(20);
      }
      slavestr = String(slavestr + "ID: " + count + " - " + decodeAbility(String(scandata1[1])) + "\n");
    }
    if (count == CurSlaves){
      bool Slavescan = 1;
      while (Slavescan == 1){
        vTaskDelay(10);
        if (modbusrun == 0){
          SlaveID = count;
          Function = 3;
          RegAdd = 0;
          RegNo = 2;
          Param.SlaveID = SlaveID;
          Param.Function = Function;
          Param.RegAdd = RegAdd;
          Param.RegNo = RegNo;
          Param.ResVar = scandata1;
          xTaskCreate(ModbusWorker, "Modbus Worker", 800, &Param, 4, NULL);
          Slavescan = 0;
        }
      }
      vTaskDelay(20);
      while (modbusrun == 1){
        vTaskDelay(10);
      }
      slavestr = String(slavestr + "ID: " + count + " - " + decodeAbility(String(scandata1[1])));
    }
  }
  lv_dropdown_set_options(SlaveSelect, slavestr.c_str());
  lv_msgbox_close(MsgBox);
  lv_obj_add_flag(FunctSetBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(CalBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(SaveBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(WiFiSetBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(SlaveSetBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(SysSetBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(SlaveSetBkBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(SlaveScanBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(SlaveSelect, LV_OBJ_FLAG_HIDDEN);
  vTaskDelay(10);
  vTaskDelete(NULL);
}

void UpdateFunct(void * Parameters11) {
  String FunctString;
  lv_dropdown_set_options(FunctionSelect, FunctString.c_str());
  lv_obj_clear_flag(FunctSetBkBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(FunctionSelect, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(NewFunctBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ConfFunctBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(FunctSetBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(SysSetBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(CalBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(SaveBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(WiFiSetBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(SlaveSetBtn, LV_OBJ_FLAG_HIDDEN);
  vTaskDelay(10);
  vTaskDelete(NULL);
}

void SaveSettings(void * parameters7) {
  EEPROM.put(0, xCalM);
  EEPROM.put(4, yCalM);
  EEPROM.put(8, xCalC);
  EEPROM.put(12, yCalC);
  EEPROM.put(16, WiFiState);
  EEPROM.put(17, NTPState);
  EEPROM.put(18, ssid);
  EEPROM.put(50, password);
  EEPROM.commit();
  Serial.println("Settings Saved");
  vTaskDelete(NULL);
}

void PairSlave(void * Parameters10){
  scanwait = millis() + scandelay;
  scandata[0] = 0;
  SlaveConf = 1;
  int count = 1;
  vTaskDelay(10);
  while(SlaveConf == 1){
    vTaskDelay(10);
    if (modbusrun == 0 && count <= 10 && scandata[0] == 0 && millis() > scanwait){
      SlaveID = 1;
      Function = 3;
      RegAdd = 0;
      RegNo = 2;
      Param.SlaveID = SlaveID;
      Param.Function = Function;
      Param.RegAdd = RegAdd;
      Param.RegNo = RegNo;
      Param.ResVar = scandata;
      xTaskCreate(ModbusWorker, "Modbus Worker", 800, &Param, 4, NULL);
      vTaskDelay(10);
      scanwait = millis() + scandelay;
      count++;
    }
    if (scandata[0] == 1){
      xTaskCreate(ConfigureSlave, "Configure Slave", 2000, NULL, 4, NULL);
      SlaveConf = 0;
    }
    if (count == 11){
      lv_msgbox_close(MsgBox);
      MsgBox = lv_msgbox_create(NULL, NULL, "Failed to find new slave!", NULL, true);
      lv_obj_center(MsgBox);
      SlaveConf = 0;
    }
  }
  vTaskDelete(NULL);
}

void MainWork(void * Parameters9){
  reswait = millis() + resdelay;
  for(;;){
    vTaskDelay(10);
    if (modbusrun == 0 && millis() > reswait){
      SlaveID = CurSlaves;
      Function = 3;
      RegAdd = 0;
      RegNo = 8;
      Param.SlaveID = SlaveID;
      Param.Function = Function;
      Param.RegAdd = RegAdd;
      Param.RegNo = RegNo;
      Param.ResVar = resdata;
      xTaskCreate(ModbusWorker, "Modbus Worker", 800, &Param, 4, NULL);
      vTaskDelay(10);
      reswait = millis() + resdelay;
    }
  }
}

void ModbusWorker(void * parameters8){
  modbusrun = 1;
  ModbusParam modbus_config = *(ModbusParam *) parameters8;
  master.start();
  master.setTimeOut(1000);
  uint8_t u8state = 0;
  while(modbusrun == 1){
    vTaskDelay(50);
    switch(u8state) {
      case 0: 
        telegram.u8id = modbus_config.SlaveID;
        telegram.u8fct = modbus_config.Function;
        telegram.u16RegAdd = modbus_config.RegAdd;
        telegram.u16CoilsNo = modbus_config.RegNo;
        telegram.au16reg = modbus_config.ResVar;

        master.query(telegram);
        u8state++;
      break;
      case 1:
        master.poll();
        if (master.getState() == COM_IDLE) {
          u8state = 0;
          modbusrun = 0; 
        }
      break;
    }
  }
  vTaskDelete(NULL);
}

void loop(){
  vTaskDelay(10);
}