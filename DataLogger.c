
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>

//  Chân kết nối
#define I2C_SDA   21
#define I2C_SCL   22
#define SD_CS      5
#define SD_SCK    18
#define SD_MISO   19
#define SD_MOSI   23

#define MPU_ADDR  0x68
#define LOG_FILE  "/data.csv"

// Dữ liệu gửi giữa 2 task 
typedef struct {
  uint32_t t_ms;
  int16_t  ax, ay, az;
  int16_t  gx, gy, gz;
  float    temp;
} SensorData_t;

static QueueHandle_t sensorQueue;
static SPIClass spiSD(VSPI);

//  Hàm I2C 
static bool mpuInit() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);          // PWR_MGMT_1
  Wire.write(0x00);          // thoát sleep mode
  return Wire.endTransmission() == 0;
}

static bool mpuRead(SensorData_t &d) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);          // ACCEL_XOUT_H
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)14) != 14) return false;

  d.ax = (Wire.read() << 8) | Wire.read();
  d.ay = (Wire.read() << 8) | Wire.read();
  d.az = (Wire.read() << 8) | Wire.read();
  int16_t rawT = (Wire.read() << 8) | Wire.read();
  d.gx = (Wire.read() << 8) | Wire.read();
  d.gy = (Wire.read() << 8) | Wire.read();
  d.gz = (Wire.read() << 8) | Wire.read();
  d.temp = rawT / 340.0f + 36.53f;
  d.t_ms = millis();
  return true;
}

//  Task 1: đọc cảm biến I2C 
static void taskSensor(void *pv) {
  TickType_t lastWake = xTaskGetTickCount();
  SensorData_t d;

  for (;;) {
    if (mpuRead(d)) {
      // Không chờ nếu queue đầy -> bỏ mẫu, không làm treo task đọc
      if (xQueueSend(sensorQueue, &d, 0) != pdTRUE) {
        Serial.println("[Sensor] Queue day, bo mau");
      }
    } else {
      Serial.println("[Sensor] Doc I2C loi");
    }
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(200));   // 5 Hz
  }
}

//  Task 2: ghi thẻ SD qua SPI 
static void taskLogger(void *pv) {
  SensorData_t d;
  uint8_t count = 0;

  for (;;) {
    if (xQueueReceive(sensorQueue, &d, portMAX_DELAY) == pdTRUE) {
      File f = SD.open(LOG_FILE, FILE_APPEND);
      if (!f) {
        Serial.println("[SD] Khong mo duoc file");
        continue;
      }
      f.printf("%lu,%d,%d,%d,%d,%d,%d,%.2f\n",
               (unsigned long)d.t_ms, d.ax, d.ay, d.az,
               d.gx, d.gy, d.gz, d.temp);
      f.close();

      Serial.printf("[SD] t=%lu ax=%d ay=%d az=%d T=%.1fC\n",
                    (unsigned long)d.t_ms, d.ax, d.ay, d.az, d.temp);

      // In kích thước file mỗi 10 mẫu để kiểm tra trên Serial Monitor
      if (++count >= 10) {
        count = 0;
        File r = SD.open(LOG_FILE);
        Serial.printf("[SD] Kich thuoc file: %u bytes\n", (unsigned)r.size());
        r.close();
      }
    }
  }
}

//  setup / loop 
void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  if (!mpuInit()) {
    Serial.println("Khong tim thay MPU6050!");
    while (1) delay(1000);
  }

  spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, spiSD)) {
    Serial.println("Khoi tao the SD that bai!");
    while (1) delay(1000);
  }
  Serial.println("SD OK");

  // Tạo header nếu file chưa có
  if (!SD.exists(LOG_FILE)) {
    File f = SD.open(LOG_FILE, FILE_WRITE);
    f.println("t_ms,ax,ay,az,gx,gy,gz,temp_c");
    f.close();
  }

  sensorQueue = xQueueCreate(16, sizeof(SensorData_t));
  if (!sensorQueue) {
    Serial.println("Tao queue that bai!");
    while (1) delay(1000);
  }


  xTaskCreatePinnedToCore(taskSensor, "Sensor", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(taskLogger, "Logger", 6144, NULL, 1, NULL, 1);
}

void loop() {
  vTaskDelete(NULL);   // loop không dùng, mọi việc do FreeRTOS task đảm nhiệm
}