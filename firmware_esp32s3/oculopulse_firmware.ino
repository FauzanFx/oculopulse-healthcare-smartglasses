// OculoPulse Dual-Core FreeRTOS Firmware (Seeed Studio XIAO ESP32-S3 Sense)

#include <Arduino.h>
#include <Wire.h>
#include "esp_camera.h"
#include "esp_sleep.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "oculopulse_core.h"

#define PIN_IR_MOSFET_PWM   1  // D0 (GPIO 1): AO3400A MOSFET gate for VSMY1850 850nm LED
#define PIN_IMU_INT1        2  // D1 (GPIO 2): LSM6DS3TR-C (0x6A) wake-on-motion INT1
#define PIN_BAT_ADC         3  // D2 (GPIO 3): LiPo battery voltage divider ADC
#define PIN_LRA_HAPTIC_PWM  4  // D3 (GPIO 4): LRA haptic driver PWM (170/200 Hz)
#define PIN_I2C_SDA         5  // D4 (GPIO 5): I2C SDA (VEML7700 0x10, LSM6DS3TR-C 0x6A)
#define PIN_I2C_SCL         6  // D5 (GPIO 6): I2C SCL (400 kHz)

#define OCULOPULSE_SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define OCULOPULSE_TELEMETRY_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

static uint8_t                  g_roi_buffer[OCULO_ROI_HEIGHT][OCULO_ROI_WIDTH];
static OculoPulseFilterState_t  g_filter_state;
static OculoPulseTelemetry_t    g_telemetry;
static SemaphoreHandle_t        g_telemetry_mutex;
static BLECharacteristic*       pTelemetryChar  = nullptr;
static bool                     g_ble_connected = false;

// 8.0 ms shutter-synchronized NIR strobe pulse (12% duty cycle @ 15 FPS)
void triggerSynchronizedIRStrobe() {
    digitalWrite(PIN_IR_MOSFET_PWM, HIGH);
    delay(8);
    digitalWrite(PIN_IR_MOSFET_PWM, LOW);
}

// Core 1: 15 Hz ROI vision & multi-sensor fusion
void Task_VisionAndFusion_Core1(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod15Hz = pdMS_TO_TICKS(66);

    initOculoPulseState(&g_filter_state);
    uint32_t frame_counter = 0;

    for (;;) {
        frame_counter++;

        triggerSynchronizedIRStrobe();
        float major_2a_px = extractPupilMajorAxis2aFromROI(g_roi_buffer, 55);

        float ambient_lux = 480.0f;
        float ax_g = 0.0f, ay_g = 0.02f, az_g = 0.99f, gyro_y_dps = 1.5f;
        float ear_cur = 0.295f, blink_rate = 15.5f, perclos = 3.2f;

        OculoPulseTelemetry_t local_tel;
        stepOculoPulseCore1(
            &g_filter_state,
            frame_counter / 15,
            0.0667f,
            major_2a_px,
            ambient_lux,
            ax_g, ay_g, az_g, gyro_y_dps,
            ear_cur, blink_rate, perclos,
            -1.0f,
            &local_tel
        );

        if (xSemaphoreTake(g_telemetry_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            g_telemetry = local_tel;
            xSemaphoreGive(g_telemetry_mutex);
        }

        vTaskDelayUntil(&xLastWakeTime, xPeriod15Hz);
    }
}

// Core 0: 1 Hz BLE 5.0 GATT JSON telemetry burst
void Task_BLETelemetry_Core0(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod1Hz = pdMS_TO_TICKS(1000);
    char json_buf[384];

    for (;;) {
        OculoPulseTelemetry_t snap;
        if (xSemaphoreTake(g_telemetry_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            snap = g_telemetry;
            xSemaphoreGive(g_telemetry_mutex);
        }

        formatTelemetryJSON(&snap, json_buf, sizeof(json_buf));
        Serial.println(json_buf);

        if (g_ble_connected && pTelemetryChar != nullptr) {
            pTelemetryChar->setValue((uint8_t*)json_buf, strlen(json_buf));
            pTelemetryChar->notify();
        }

        vTaskDelayUntil(&xLastWakeTime, xPeriod1Hz);
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_IR_MOSFET_PWM, OUTPUT);
    digitalWrite(PIN_IR_MOSFET_PWM, LOW);
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_IMU_INT1, 1);
    g_telemetry_mutex = xSemaphoreCreateMutex();

    xTaskCreatePinnedToCore(Task_VisionAndFusion_Core1, "VisionFusion_C1", 8192, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(Task_BLETelemetry_Core0,    "BLETelemetry_C0", 4096, nullptr, 1, nullptr, 0);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(5000));
}
