/* USER CODE BEGIN Header */
/**
  * @file           : app_main.cpp
  * @brief          : He thong do NO2 Tu Dong - Kien truc State Machine
  */
/* USER CODE END Header */

#include "app_main.h"
#include "main.h"
#include <stdio.h>
#include <math.h>

/* --- INCLUDE CÁC MODULE --- */
#include "module_fluidics.h"
#include "module_stirrer.h"
#include "as7341.h"
#include "lcd_i2c.h"

/* --- NHẬP KHẨU BIẾN TỪ MAIN.C --- */
extern "C" I2C_HandleTypeDef hi2c1;
extern "C" I2C_HandleTypeDef hi2c2;

/* --- KHỞI TẠO OBJECT CẢM BIẾN & LCD --- */
Adafruit_AS7341 as7341;
CLCD_I2C_Name lcd1;

/* --- THUẬT TOÁN NHẬN DIỆN MÀU NO2 (SERA KIT) --- */
typedef struct {
    float concentration;
    float norm_F4; // Kênh Xanh lá (515nm)
    float norm_F6; // Kênh Cam (590nm)
    float norm_F8; // Kênh Đỏ (680nm)
} Sera_Color_Profile;

// Bảng Calibration (Cần hiệu chuẩn lại với buồng đo thực tế)
Sera_Color_Profile NO2_Profiles[5] = {
    {0.0,  0.25, 0.30, 0.40},
    {0.5,  0.20, 0.32, 0.45},
    {1.0,  0.15, 0.35, 0.50},
    {2.0,  0.08, 0.35, 0.55},
    {5.0,  0.02, 0.20, 0.65}
};

float Tinh_Nong_Do_NO2(uint16_t f4, uint16_t f6, uint16_t f8, uint16_t clear) {
    if (clear == 0) return -1.0;
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
    return matched_val;
}

/* --- ĐỊNH NGHĨA MÁY TRẠNG THÁI --- */
typedef enum {
    STATE_IDLE,
    STATE_PUMP_IN,
    STATE_DROP_1,
    STATE_DROP_2,
    STATE_STIR_WAIT,
    STATE_MEASURE,
    STATE_PUMP_OUT
} SystemState;

SystemState currentState = STATE_IDLE;
uint32_t state_timer = 0;
uint32_t timeout_timer = 0; // Chống treo bơm nhu động
char lcd_buf[32];           // Buffer in chữ lên LCD (đủ lớn để tránh format overflow warning)

/* --- HÀM CHẠY CHÍNH (GỌI TỪ MAIN.C) --- */
void Main_App(void) {
    printf("\r\n==== HE THONG DO NO2 TỰ ĐỘNG ====\r\n");

    /* 1. Khởi tạo LCD 1602 */
    CLCD_I2C_Init(&lcd1, &hi2c1, 0x4E, 16, 2);
    CLCD_I2C_Clear(&lcd1);
    CLCD_I2C_SetCursor(&lcd1, 0, 0);
    CLCD_I2C_WriteString(&lcd1, "HUST - NO2 SYS");
    CLCD_I2C_SetCursor(&lcd1, 0, 1);
    CLCD_I2C_WriteString(&lcd1, "Khoi tao...");
    HAL_Delay(1000);

    /* 2. Khởi tạo AS7341 */
    if (!as7341.begin(AS7341_I2CADDR_DEFAULT, &hi2c2)) {
        printf("CRITICAL: Khong tim thay AS7341!\r\n");
        CLCD_I2C_SetCursor(&lcd1, 0, 1);
        CLCD_I2C_WriteString(&lcd1, "Loi Cam Bien!   ");
        while(1); // Treo hệ thống nếu không có cảm biến
    }
    as7341.setGain(AS7341_GAIN_256X);
    as7341.setATIME(100);
    as7341.setASTEP(999);

    // Đảm bảo tắt mọi thiết bị cơ khí ban đầu
    Pump_Water_In(false);
    Pump_Water_Out(false);
    Pump_Reagent_1(false);
    Pump_Reagent_2(false);
    Stirrer_Enable(false);
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET); // Tắt LED đo màu

    currentState = STATE_IDLE;
    CLCD_I2C_Clear(&lcd1);
    CLCD_I2C_SetCursor(&lcd1, 0, 0);
    CLCD_I2C_WriteString(&lcd1, "He Thong SanSang");
    CLCD_I2C_SetCursor(&lcd1, 0, 1);
    CLCD_I2C_WriteString(&lcd1, "Nhan nut START..");

    /* VÒNG LẶP CHÍNH MÁY TRẠNG THÁI (NON-BLOCKING) */
    while (1) {

        // Luôn duy trì xung khuấy từ nếu đang ở trạng thái chờ phản ứng
        if (currentState == STATE_STIR_WAIT) {
            Stirrer_Update(800); // Phát xung chạy động cơ bước (800us/bước)
        }

        switch (currentState) {

            case STATE_IDLE:
                // Kích hoạt chu trình khi nhấn nút ở chân PE7
                if (HAL_GPIO_ReadPin(BTN_START_GPIO_Port, BTN_START_Pin) == GPIO_PIN_RESET) {
                    HAL_Delay(50); // Chống dội phím
                    
                    uint32_t btn_timeout = HAL_GetTick();
                    uint8_t is_valid = 1;
                    
                    // Thêm Timeout 3 giây chống kẹt do nhiễu EMI
                    while(HAL_GPIO_ReadPin(BTN_START_GPIO_Port, BTN_START_Pin) == GPIO_PIN_RESET) {
                        if (HAL_GetTick() - btn_timeout > 3000) {
                            is_valid = 0; // Đánh dấu là lỗi kẹt phím
                            break;        // Tự thoát vòng lặp tử thần
                        }
                    }

                    // Chỉ tiếp tục nếu không bị kẹt
                    if (is_valid) {
                        CLCD_I2C_Clear(&lcd1);
                        CLCD_I2C_SetCursor(&lcd1, 0, 0);
                        CLCD_I2C_WriteString(&lcd1, "1. Bom mau nuoc");
                        printf("=> B1: Bom 5ml nuoc vao cuvet...\r\n");

                        Pump_Water_In(true);
                        state_timer = HAL_GetTick();
                        currentState = STATE_PUMP_IN;
                    }
                }
                break;

            case STATE_PUMP_IN:
                // Cấp nước trong 1800ms
                if (HAL_GetTick() - state_timer >= 1800) {
                    Pump_Water_In(false); // Tắt bơm cấp
                    Reset_Drop_Counters();

                    CLCD_I2C_SetCursor(&lcd1, 0, 0);
                    CLCD_I2C_WriteString(&lcd1, "2. Nho thuoc L1");
                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, "Giot: 0/3       ");
                    printf("=> B2: Nho thuoc thu Lo 1...\r\n");

                    Pump_Reagent_1(true);
                    timeout_timer = HAL_GetTick();
                    currentState = STATE_DROP_1;
                }
                break;

            case STATE_DROP_1:
            {
                // Hiển thị số giọt realtime lên LCD
                static int last_drop_1 = -1;
                if (last_drop_1 != count_drop_1) {
                    last_drop_1 = count_drop_1;
                    sprintf(lcd_buf, "Giot: %d/3      ", count_drop_1);
                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, lcd_buf);
                }

                if (count_drop_1 >= 3) {
                    last_drop_1 = -1; // Reset cho lan sau
                    Pump_Reagent_1(false); // Tắt bơm lọ 1
                    Reset_Drop_Counters();

                    CLCD_I2C_SetCursor(&lcd1, 0, 0);
                    CLCD_I2C_WriteString(&lcd1, "3. Nho thuoc L2");
                    printf("=> B3: Nho thuoc thu Lo 2...\r\n");

                    Pump_Reagent_2(true);
                    timeout_timer = HAL_GetTick();
                    currentState = STATE_DROP_2;
                }
                // Timeout chống tắc ống
                else if (HAL_GetTick() - timeout_timer > 20000) {
                    last_drop_1 = -1; // Reset khi thoat ra ngoai
                    Pump_Reagent_1(false);
                    printf("WARN: Timeout Lo 1! Huy chu trinh.\r\n");
                    currentState = STATE_PUMP_OUT; // Ép xả nước
                }
                break;
            }

            case STATE_DROP_2:
            {
                static int last_drop_2 = -1;
                if (last_drop_2 != count_drop_2) {
                    last_drop_2 = count_drop_2;
                    sprintf(lcd_buf, "Giot: %d/3      ", count_drop_2);
                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, lcd_buf);
                }

                if (count_drop_2 >= 3) {
                    last_drop_2 = -1; // Reset cho lan sau
                    Pump_Reagent_2(false);

                    CLCD_I2C_Clear(&lcd1);
                    CLCD_I2C_SetCursor(&lcd1, 0, 0);
                    CLCD_I2C_WriteString(&lcd1, "4. Khuay&PhanUng");
                    printf("=> B4: Bat dau khuay tu va cho phan ung...\r\n");

                    Stirrer_Enable(true); // Bật IC TMC2209
                    state_timer = HAL_GetTick();
                    currentState = STATE_STIR_WAIT;
                }
                else if (HAL_GetTick() - timeout_timer > 20000) {
                    last_drop_2 = -1; // Reset
                    Pump_Reagent_2(false);
                    printf("WARN: Timeout Lo 2! Huy chu trinh.\r\n");
                    currentState = STATE_PUMP_OUT;
                }
                break;
            }

            case STATE_STIR_WAIT:
            {    // Cập nhật LCD đếm lùi (Mô phỏng chờ 10 giây, thực tế đổi thành 300000ms)
                uint32_t elapsed_sec = (HAL_GetTick() - state_timer) / 1000;
                static uint32_t last_elapsed_sec = 999;
                if (last_elapsed_sec != elapsed_sec) {
                    last_elapsed_sec = elapsed_sec;
                    sprintf(lcd_buf, "Cho: %us/10s  ", (unsigned int)elapsed_sec);
                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, lcd_buf);
                }

                if (HAL_GetTick() - state_timer >= 10000) { // Đợi 10 giây
                    last_elapsed_sec = 999; // Reset cho lan sau
                    Stirrer_Enable(false); // Ngắt động cơ bước

                    CLCD_I2C_Clear(&lcd1);
                    CLCD_I2C_SetCursor(&lcd1, 0, 0);
                    CLCD_I2C_WriteString(&lcd1, "5. Doc quang pho");
                    printf("=> B5: Đoc quang pho AS7341...\r\n");

                    currentState = STATE_MEASURE;
                }
                break;
            }

            case STATE_MEASURE:
                // Bật LED đo màu
                HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
                HAL_Delay(150); // Chờ LED sáng ổn định

                if (as7341.readAllChannels()) {
                    // Tắt LED đo màu ngay sau khi đọc xong
                    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

                    uint16_t f4 = as7341.getChannel(AS7341_CHANNEL_515nm_F4);
                    uint16_t f6 = as7341.getChannel(AS7341_CHANNEL_590nm_F6);
                    uint16_t f8 = as7341.getChannel(AS7341_CHANNEL_680nm_F8);
                    uint16_t clear = as7341.getChannel(AS7341_CHANNEL_CLEAR);

                    float no2_val = Tinh_Nong_Do_NO2(f4, f6, f8, clear);

                    // In kết quả lên màn hình
                    CLCD_I2C_Clear(&lcd1);
                    CLCD_I2C_SetCursor(&lcd1, 0, 0);
                    CLCD_I2C_WriteString(&lcd1, "Nong Do NO2:");

                    sprintf(lcd_buf, "%.2f mg/L", no2_val);
                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, lcd_buf);

                    printf(">> RAW DATA: F4:%u, F6:%u, F8:%u, Clear:%u\r\n", f4, f6, f8, clear);
                    printf(">> KET QUA NO2: %.2f mg/L\r\n", no2_val);
                } else {
                    // Đảm bảo tắt LED kể cả khi lỗi
                    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
                    CLCD_I2C_Clear(&lcd1);
                    CLCD_I2C_WriteString(&lcd1, "Loi Doc AS7341!");
                }

                // Giữ màn hình kết quả 5 giây trước khi xả nước
                HAL_Delay(5000);

                CLCD_I2C_Clear(&lcd1);
                CLCD_I2C_SetCursor(&lcd1, 0, 0);
                CLCD_I2C_WriteString(&lcd1, "6. Xa nuoc thai");
                printf("=> B6: Xa sach cuvet...\r\n");

                Pump_Water_Out(true);
                state_timer = HAL_GetTick();
                currentState = STATE_PUMP_OUT;
                break;

            case STATE_PUMP_OUT:
                // Xả rỗng trong 3500ms
                if (HAL_GetTick() - state_timer >= 3500) {
                    Pump_Water_Out(false);

                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, "Chu trinh xong! ");
                    printf("--- HOAN THANH CHU TRINH ---\r\n\r\n");
                    HAL_Delay(2000);

                    CLCD_I2C_Clear(&lcd1);
                    CLCD_I2C_SetCursor(&lcd1, 0, 0);
                    CLCD_I2C_WriteString(&lcd1, "He Thong SanSang");
                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, "Nhan nut START..");
                    currentState = STATE_IDLE; // Quay về vạch xuất phát
                }
                break;
        }
    }
}
