/* USER CODE BEGIN Header */
/**
  * @file           : app_test_as7341.cpp
  * @brief          : Môi trường Test AS7341 & LED 1W độc lập (Dùng để lấy tham số Calibration)
  */
/* USER CODE END Header */

#include "app_test_as7341.h"
#include "main.h"
#include <stdio.h>
#include <math.h>

/* --- INCLUDE CÁC MODULE --- */
#include "as7341.h"
#include "lcd_i2c.h"

/* --- NHẬP KHẨU BIẾN TỪ MAIN.C --- */
extern "C" I2C_HandleTypeDef hi2c1;
extern "C" I2C_HandleTypeDef hi2c2;
extern "C" TIM_HandleTypeDef htim2; // TIM2 CH3 - LED PWM 10kHz (PA2)

/* --- KHỞI TẠO OBJECT CẢM BIẾN & LCD --- */
static Adafruit_AS7341 as7341;
static CLCD_I2C_Name lcd1;
extern "C" IWDG_HandleTypeDef hiwdg; // <--- KHAI BÁO WATCHDOG ĐỂ FEED

/* --- THUẬT TOÁN NHẬN DIỆN MÀU NO2 (SERA KIT) --- */
typedef struct {
    float concentration;
    float norm_F4; // Kênh Xanh lá (515nm)
    float norm_F6; // Kênh Cam (590nm)
    float norm_F8; // Kênh Đỏ (680nm)
} Sera_Color_Profile;

// Bảng Calibration
static Sera_Color_Profile NO2_Profiles[5] = {
    {0.0,  0.441, 0.576, 0.159}, // Vàng 
    {0.5,  0.404, 0.623, 0.190}, // Vàng cam
    {1.0,  0.320, 0.659, 0.236}, // Cam
    {2.0,  0.250, 0.666, 0.284}, // Cam đậm 
    {5.0,  0.135, 0.649, 0.391}  // Đỏ 
};

static float Tinh_Nong_Do_NO2(uint16_t f4, uint16_t f6, uint16_t f8, uint16_t clear, float *out_dist) {
    if (clear == 0) {
        if (out_dist) *out_dist = -1.0;
        return -1.0;
    }
    float current_F4 = (float)f4 / clear;
    float current_F6 = (float)f6 / clear;
    float current_F8 = (float)f8 / clear;

    float min_distance = 9999.0;
    float matched_val = 0.0;

    for (int i = 0; i < 5; i++) {
        float d_f4 = current_F4 - NO2_Profiles[i].norm_F4;
        float d_f6 = current_F6 - NO2_Profiles[i].norm_F6;
        float d_f8 = current_F8 - NO2_Profiles[i].norm_F8;
        float distance = sqrt(d_f4*d_f4 + d_f6*d_f6 + d_f8*d_f8);

        if (distance < min_distance) {
            min_distance = distance;
            matched_val = NO2_Profiles[i].concentration;
        }
    }
    if (out_dist) *out_dist = min_distance;
    return matched_val;
}

/* --- ĐỊNH NGHĨA MÁY TRẠNG THÁI --- */
typedef enum {
    STATE_IDLE,
    STATE_COUNTDOWN,
    STATE_MEASURE,
    STATE_RESULT
} SystemState;

static SystemState currentState = STATE_IDLE;
static uint32_t state_timer = 0;
static char lcd_buf[32];

/* --- HÀM CHẠY CHÍNH --- */
void Test_AS7341_App(void) {
    printf("\r\n==== HE THONG TEST AS7341 & LED 1W DOC LAP ====\r\n");

    /* 1. Khởi tạo LCD 1602 */
    CLCD_I2C_Init(&lcd1, &hi2c1, 0x4E, 16, 2);
    CLCD_I2C_Clear(&lcd1);
    CLCD_I2C_SetCursor(&lcd1, 0, 0);
    CLCD_I2C_WriteString(&lcd1, "HUST - CALIB AS");
    CLCD_I2C_SetCursor(&lcd1, 0, 1);
    CLCD_I2C_WriteString(&lcd1, "Khoi tao...");
    HAL_Delay(1000);

    /* 2. Khởi tạo AS7341 */
    if (!as7341.begin(AS7341_I2CADDR_DEFAULT, &hi2c2)) {
        printf("CRITICAL: Khong tim thay AS7341!\r\n");
        CLCD_I2C_SetCursor(&lcd1, 0, 1);
        CLCD_I2C_WriteString(&lcd1, "Loi Cam Bien!   ");
        while(1);
    }
    as7341.setGain(AS7341_GAIN_16X);
    as7341.setATIME(100);
    as7341.setASTEP(999);

    // Đảm bảo tắt LED ban đầu
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, LED_PWM_OFF); 

    currentState = STATE_IDLE;
    CLCD_I2C_Clear(&lcd1);
    CLCD_I2C_SetCursor(&lcd1, 0, 0);
    CLCD_I2C_WriteString(&lcd1, "San sang Test...");
    CLCD_I2C_SetCursor(&lcd1, 0, 1);
    CLCD_I2C_WriteString(&lcd1, "Nhan START de do");

    while (1) {
        HAL_IWDG_Refresh(&hiwdg); // Feed IWDG

        switch (currentState) {
            case STATE_IDLE:
            {
                if (HAL_GPIO_ReadPin(BTN_START_GPIO_Port, BTN_START_Pin) == GPIO_PIN_RESET) {
                    HAL_Delay(50); // Chống dội
                    uint32_t press1_timeout = HAL_GetTick();
                    while (HAL_GPIO_ReadPin(BTN_START_GPIO_Port, BTN_START_Pin) == GPIO_PIN_RESET) {
                        if (HAL_GetTick() - press1_timeout > 3000) break;
                    }
                    if (HAL_GetTick() - press1_timeout >= 3000) break; // Kẹt phím

                    CLCD_I2C_Clear(&lcd1);
                    CLCD_I2C_SetCursor(&lcd1, 0, 0);
                    CLCD_I2C_WriteString(&lcd1, "Chuan bi do AS...");
                    printf("\r\n=> BAT DAU DEM NGUOC 10 GIAY...\r\n");

                    state_timer = HAL_GetTick();
                    currentState = STATE_COUNTDOWN;
                }
                break;
            }

            case STATE_COUNTDOWN:
            {
                uint32_t elapsed = (HAL_GetTick() - state_timer) / 1000;
                uint32_t remaining = 10 - elapsed;
                
                static uint32_t last_remaining = 999;
                if (last_remaining != remaining) {
                    last_remaining = remaining;
                    sprintf(lcd_buf, "Dem nguoc: %lus ", (unsigned long)remaining);
                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, lcd_buf);
                    printf("  Cho: %lu s\r\n", (unsigned long)remaining);
                }

                if (HAL_GetTick() - state_timer >= 10000) {
                    last_remaining = 999;
                    CLCD_I2C_Clear(&lcd1);
                    CLCD_I2C_SetCursor(&lcd1, 0, 0);
                    CLCD_I2C_WriteString(&lcd1, "Dang doc gia tri");
                    printf("=> BAT DAU DOC QUANG PHO...\r\n");
                    
                    currentState = STATE_MEASURE;
                }
                break;
            }

            case STATE_MEASURE:
            {
                // Re-init AS7341 phòng trường hợp cảm biến bị reset cứng
                as7341.begin(AS7341_I2CADDR_DEFAULT, &hi2c2);
                as7341.setGain(AS7341_GAIN_16X);
                as7341.setATIME(100);
                as7341.setASTEP(999);

                // Bật LED đo màu qua TIM2 PWM
                __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, LED_PWM_ON);
                HAL_Delay(150); // Chờ LED sáng ổn định

                if (as7341.readAllChannels()) {
                    // Tắt LED ngay sau khi đọc
                    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, LED_PWM_OFF); 

                    uint16_t f4 = as7341.getChannel(AS7341_CHANNEL_515nm_F4);
                    uint16_t f6 = as7341.getChannel(AS7341_CHANNEL_590nm_F6);
                    uint16_t f8 = as7341.getChannel(AS7341_CHANNEL_680nm_F8);
                    uint16_t clear = as7341.getChannel(AS7341_CHANNEL_CLEAR);

                    float min_dist = 0;
                    float no2_val = Tinh_Nong_Do_NO2(f4, f6, f8, clear, &min_dist);

                    CLCD_I2C_Clear(&lcd1);

                    if (no2_val < 0) {
                        CLCD_I2C_SetCursor(&lcd1, 0, 0);
                        CLCD_I2C_WriteString(&lcd1, "Loi tinh toan!  ");
                        printf(">> RAW DATA: F4:%u, F6:%u, F8:%u, Clear:%u\r\n", f4, f6, f8, clear);
                        printf(">> KET QUA: Loi (Clear=0)\r\n");
                    } else {
                        float current_F4 = (float)f4 / clear;
                        float current_F6 = (float)f6 / clear;
                        float current_F8 = (float)f8 / clear;

                        int dist_int = (int)min_dist;
                        int dist_frac = (int)(min_dist * 1000) % 1000;
                        
                        // Hàng 1: Hiển thị F4, F6 NORM (chuẩn hoá) để điền vào mảng Sera_Color_Profile
                        // VD: 4:0.250 6:0.325
                        int f4_f1 = (int)current_F4; int f4_f2 = (int)(current_F4 * 1000) % 1000;
                        int f6_f1 = (int)current_F6; int f6_f2 = (int)(current_F6 * 1000) % 1000;
                        sprintf(lcd_buf, "4:%d.%03d 6:%d.%03d", f4_f1, f4_f2, f6_f1, f6_f2);
                        CLCD_I2C_SetCursor(&lcd1, 0, 0);
                        CLCD_I2C_WriteString(&lcd1, lcd_buf);
                        
                        // Hàng 2: Hiển thị F8 NORM và khoảng cách (Dist) để tiện theo dõi
                        // VD: 8:0.400 D:0.012
                        int f8_f1 = (int)current_F8; int f8_f2 = (int)(current_F8 * 1000) % 1000;
                        sprintf(lcd_buf, "8:%d.%03d D:%d.%03d", f8_f1, f8_f2, dist_int, dist_frac);
                        CLCD_I2C_SetCursor(&lcd1, 0, 1);
                        CLCD_I2C_WriteString(&lcd1, lcd_buf);
                        
                        // In ra Console chi tiết để tiện việc COPY/PASTE vào bảng Calibration
                        printf(">> RAW DATA: F4:%u, F6:%u, F8:%u, Clear:%u\r\n", f4, f6, f8, clear);
                        printf(">> NORM DATA: norm_F4: %d.%03d, norm_F6: %d.%03d, norm_F8: %d.%03d\r\n", f4_f1, f4_f2, f6_f1, f6_f2, f8_f1, f8_f2);
                        printf(">> NO2 MAP : %d.%02d mg/L (Distance: %d.%03d)\r\n", (int)no2_val, (int)(no2_val * 100) % 100, dist_int, dist_frac);
                        printf("---------------------------------------------------\r\n");
                    }
                } else {
                    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, LED_PWM_OFF);
                    CLCD_I2C_Clear(&lcd1);
                    CLCD_I2C_WriteString(&lcd1, "Loi Doc AS7341!");
                    printf(">> LOI GIAO TIEP AS7341\r\n");
                }

                state_timer = HAL_GetTick();
                currentState = STATE_RESULT;
                break;
            }

            case STATE_RESULT:
            {
                // Nhấn nút START lần nữa để quay lại trạng thái IDLE
                if (HAL_GPIO_ReadPin(BTN_START_GPIO_Port, BTN_START_Pin) == GPIO_PIN_RESET) {
                    HAL_Delay(50);
                    uint32_t press1_timeout = HAL_GetTick();
                    while (HAL_GPIO_ReadPin(BTN_START_GPIO_Port, BTN_START_Pin) == GPIO_PIN_RESET) {
                        if (HAL_GetTick() - press1_timeout > 3000) break;
                    }
                    if (HAL_GetTick() - press1_timeout >= 3000) break;
                    
                    CLCD_I2C_Clear(&lcd1);
                    CLCD_I2C_SetCursor(&lcd1, 0, 0);
                    CLCD_I2C_WriteString(&lcd1, "San sang Test...");
                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, "Nhan START de do");
                    printf("=> QUAY LAI SAN SANG DO...\r\n");
                    
                    currentState = STATE_IDLE;
                }
                break;
            }
        }
    }
}
