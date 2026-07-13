/* USER CODE BEGIN Header */
/**
  * @file           : app_main.cpp
  * @brief          : He thong do NO2 Tu Dong
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

/* --- BIẾN TỪ MAIN.C --- */
extern "C" I2C_HandleTypeDef hi2c1;
extern "C" I2C_HandleTypeDef hi2c2;
extern "C" TIM_HandleTypeDef htim2; // TIM2 CH3 - LED PWM 10kHz (PA2)
extern "C" TIM_HandleTypeDef htim6; // TIM6 - Software PWM Bơm
extern "C" IWDG_HandleTypeDef hiwdg; // IWDG Watchdog

/* --- KHỞI TẠO OBJECT CẢM BIẾN & LCD --- */
Adafruit_AS7341 as7341;
CLCD_I2C_Name lcd1;

/* ========================================================================== */
/* THUẬT TOÁN NHẬN DIỆN MÀU NO2 (NỘI SUY TUYẾN TÍNH)                        */
/* ========================================================================== */
typedef struct {
    float concentration;
    float norm_F4; // Kênh Xanh lá (515nm)
    float norm_F6; // Kênh Cam (590nm)
    float norm_F8; // Kênh Đỏ (680nm)
} Sera_Color_Profile;

// Bảng Calibration
#define NUM_PROFILES 5
static Sera_Color_Profile NO2_Profiles[5] = {
    {0.0,  0.441, 0.576, 0.159}, // Vàng 
    {0.5,  0.404, 0.623, 0.190}, // Cam nhạt 
    {1.0,  0.320, 0.659, 0.236}, // Cam
    {2.0,  0.250, 0.666, 0.284}, // Cam đậm 
    {5.0,  0.135, 0.649, 0.391}  // Đỏ 
};

/**
 * @brief  Tính nồng độ NO2 bằng thuật toán KNN (K-Nearest Neighbor, K=1)
 *
 *         Thuật toán:
 *         1. Chuẩn hóa các kênh F4, F6, F8 theo kênh Clear (bù sáng môi trường)
 *         2. Tính khoảng cách Euclid từ mẫu đo tới mỗi điểm trong bảng hiệu chuẩn
 *         3. Trả về nồng độ của điểm có khoảng cách nhỏ nhất
 *
 *         Lý do dùng KNN thay vì nội suy: Bộ kit Sera test NO2 sử dụng bảng so màu
 *         với các mức nồng độ rời rạc (0, 0.5, 1, 2, 5 mg/L). Phản ứng tạo màu
 *         không tuyến tính theo nồng độ nên nội suy giữa 2 mức sẽ cho kết quả sai.
 *
 * @param  f4, f6, f8: Giá trị raw từ kênh 515nm, 590nm, 680nm
 * @param  clear: Giá trị kênh Clear (dùng để chuẩn hóa)
 * @param  out_dist: (output) Khoảng cách Euclid nhỏ nhất, dùng đánh giá độ tin cậy
 * @retval Nồng độ NO2 (mg/L), hoặc -1.0 nếu lỗi (clear=0)
 */
float Tinh_Nong_Do_NO2(uint16_t f4, uint16_t f6, uint16_t f8, uint16_t clear, float *out_dist) {
    if (clear == 0) {
        if (out_dist) *out_dist = -1.0;
        return -1.0;
    }
    float current_F4 = (float)f4 / clear;
    float current_F6 = (float)f6 / clear;
    float current_F8 = (float)f8 / clear;

    float min_distance = 9999.0;
    float matched_val = 0.0;

    for (int i = 0; i < NUM_PROFILES; i++) {
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

/* ========================================================================== */
/* ĐỊNH NGHĨA MÁY TRẠNG THÁI                                                 */
/* ========================================================================== */
typedef enum {
    STATE_IDLE,
    STATE_PUMP_IN,
    STATE_DROP_1,
    STATE_STIR_WAIT_1,
    STATE_DROP_2,
    STATE_STIR_WAIT_2,
    STATE_MEASURE,
    STATE_PUMP_OUT,
    // --- CHẾ ĐỘ TRÁNG ỐNG NGHIỆM (RINSE MODE) ---
    STATE_RINSE_PUMP_IN,   // Bơm nước vào để tráng
    STATE_RINSE_STIR,      // Khuấy từ tốc độ cao để tráng đều
    STATE_RINSE_PUMP_OUT   // Xả nước tráng ra
} SystemState;

SystemState currentState = STATE_IDLE;
uint32_t state_timer = 0;
uint32_t timeout_timer = 0; // Chống treo bơm nhu động
char lcd_buf[32];           // Buffer LCD
uint8_t rinse_count = 0;   // Đếm số lần tráng (tráng 1 lần)

/* --- BIẾN CHO LCD WATCHDOG & LONG-PRESS RESET --- */
static uint32_t lcd_watchdog_timer = 0;   // Thời điểm kiểm tra LCD lần cuối
#define LCD_WATCHDOG_INTERVAL_MS  10000    // Kiểm tra LCD mỗi 10 giây
#define LONG_PRESS_RESET_MS       5000     // Nhấn giữ 5 giây để reset toàn bộ
#define AS7341_MAX_RETRIES        3        // Số lần thử đọc lại AS7341

/* ========================================================================== */
/* HÀM AN TOÀN HỆ THỐNG (SAFETY FUNCTIONS)                                    */
/* ========================================================================== */

/**
 * @brief  Tắt an toàn TẤT CẢ thiết bị cơ khí trong 1 lệnh duy nhất.
 *         Gọi hàm này khi cần dừng khẩn cấp hệ thống.
 */
static void Safe_Shutdown_All(void) {
    Pump_Water_In(false);
    Pump_Water_Out(false);
    Pump_Reagent_1(false);
    Pump_Reagent_2(false);
    Stirrer_Enable(false);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, LED_PWM_OFF);
    printf(">> SAFE SHUTDOWN: Tat tat ca thiet bi co khi.\r\n");
}

/**
 * @brief  Reset toàn bộ hệ thống: Tắt thiết bị, recovery I2C bus,
 *         khởi tạo lại LCD và AS7341, đưa state machine về IDLE.
 */
static void Full_System_Reset(void) {
    printf("\r\n!!! FULL SYSTEM RESET !!!\r\n");

    // 1. Tắt hết thiết bị
    Safe_Shutdown_All();

    // 2. Reset LCD (I2C bus recovery + re-init)
    printf(">> Reset LCD...\r\n");
    CLCD_I2C_ForceReset(&lcd1);

    // 3. Re-init AS7341
    printf(">> Re-init AS7341...\r\n");
    as7341.begin(AS7341_I2CADDR_DEFAULT, &hi2c2);
    as7341.setGain(AS7341_GAIN_16X);
    as7341.setATIME(100);
    as7341.setASTEP(999);

    // 4. Hiển thị trạng thái sẵn sàng
    CLCD_I2C_SetCursor(&lcd1, 0, 0);
    CLCD_I2C_WriteString(&lcd1, "He Thong SanSang");
    CLCD_I2C_SetCursor(&lcd1, 0, 1);
    CLCD_I2C_WriteString(&lcd1, "Nhan nut START..");

    // 5. Reset state
    currentState = STATE_IDLE;
    lcd_watchdog_timer = HAL_GetTick();
    printf(">> System Reset DONE. San sang.\r\n");
}

/**
 * @brief  Kiểm tra nhấn giữ nút START 5 giây để reset toàn bộ hệ thống.
 *         Chỉ hoạt động khi KHÔNG ở STATE_IDLE (ở IDLE đã có logic riêng).
 * @retval 1 nếu đã reset thành công, 0 nếu không nhấn giữ đủ lâu.
 */
static uint8_t Check_Long_Press_Reset(void) {
    static uint32_t press_start = 0;
    static uint8_t is_pressing = 0;

    // Chỉ kiểm tra khi KHÔNG ở STATE_IDLE (ở IDLE đã có logic riêng)
    if (currentState == STATE_IDLE) {
        is_pressing = 0;
        return 0;
    }

    if (HAL_GPIO_ReadPin(BTN_START_GPIO_Port, BTN_START_Pin) == GPIO_PIN_RESET) {
        if (!is_pressing) {
            is_pressing = 1;
            press_start = HAL_GetTick();
        } else {
            if (HAL_GetTick() - press_start >= LONG_PRESS_RESET_MS) {
                // Hiển thị thông báo đang reset
                CLCD_I2C_Clear(&lcd1);
                CLCD_I2C_SetCursor(&lcd1, 0, 0);
                CLCD_I2C_WriteString(&lcd1, "!! RESETTING !!");
                CLCD_I2C_SetCursor(&lcd1, 0, 1);
                CLCD_I2C_WriteString(&lcd1, "Xin cho...");
                printf(">> LONG PRESS DETECTED: Reset toan bo he thong!\r\n");

                Full_System_Reset();
                is_pressing = 0;
                return 1;
            }
        }
    } else {
        is_pressing = 0; // Nhả nút
    }
    return 0;
}

/**
 * @brief  Kiểm tra sức khỏe LCD qua I2C mỗi 10 giây.
 *         Nếu mất kết nối → recovery bus I2C + reinit LCD.
 */
static void LCD_Watchdog_Check(void) {
    if (HAL_GetTick() - lcd_watchdog_timer < LCD_WATCHDOG_INTERVAL_MS) {
        return; // Chưa đến lúc kiểm tra
    }
    lcd_watchdog_timer = HAL_GetTick();

    // Thử giao tiếp I2C với PCF8574
    if (HAL_I2C_IsDeviceReady(&hi2c1, lcd1.ADDRESS, 2, 50) != HAL_OK) {
        printf("!! LCD WATCHDOG: Mat ket noi LCD! Dang recovery...\r\n");
        CLCD_I2C_ForceReset(&lcd1);

        // Hiển thị lại nội dung phù hợp với state hiện tại
        switch (currentState) {
            case STATE_IDLE:
                CLCD_I2C_SetCursor(&lcd1, 0, 0);
                CLCD_I2C_WriteString(&lcd1, "He Thong SanSang");
                CLCD_I2C_SetCursor(&lcd1, 0, 1);
                CLCD_I2C_WriteString(&lcd1, "Nhan nut START..");
                break;
            default:
                CLCD_I2C_SetCursor(&lcd1, 0, 0);
                CLCD_I2C_WriteString(&lcd1, "LCD Recovered!  ");
                CLCD_I2C_SetCursor(&lcd1, 0, 1);
                CLCD_I2C_WriteString(&lcd1, "Dang chay...    ");
                break;
        }
        printf(">> LCD WATCHDOG: Recovery thanh cong!\r\n");
    }
}

/* ========================================================================== */
/* HÀM CHẠY CHÍNH (GỌI TỪ MAIN.C)                                            */
/* ========================================================================== */
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
    as7341.setGain(AS7341_GAIN_16X);
    as7341.setATIME(100);
    as7341.setASTEP(999);

    // Đảm bảo tắt mọi thiết bị cơ khí ban đầu
    Safe_Shutdown_All();

    currentState = STATE_IDLE;
    CLCD_I2C_Clear(&lcd1);
    CLCD_I2C_SetCursor(&lcd1, 0, 0);
    CLCD_I2C_WriteString(&lcd1, "He Thong SanSang");
    CLCD_I2C_SetCursor(&lcd1, 0, 1);
    CLCD_I2C_WriteString(&lcd1, "Nhan nut START..");

    lcd_watchdog_timer = HAL_GetTick();

    /* VÒNG LẶP CHÍNH MÁY TRẠNG THÁI (NON-BLOCKING) */
    while (1) {

        // === IWDG: Feed dog mỗi vòng lặp (chống MCU treo) ===
        HAL_IWDG_Refresh(&hiwdg);

        // === KIỂM TRA NHẤN GIỮ NÚT ĐỂ RESET TOÀN BỘ ===
        if (Check_Long_Press_Reset()) {
            continue; // Đã reset xong, quay lại đầu vòng lặp
        }

        // === LCD WATCHDOG: Kiểm tra sức khỏe LCD định kỳ ===
        LCD_Watchdog_Check();

        // Luôn duy trì xung khuấy từ ở các trạng thái khuấy
        if (currentState == STATE_STIR_WAIT_1 || currentState == STATE_STIR_WAIT_2
            || currentState == STATE_RINSE_STIR) {
            // Tráng ống quay cực nhanh (50us), khuấy thuốc thử quay vừa phải (60us)
            Stirrer_Update((currentState == STATE_RINSE_STIR) ? 50 : 60);
        }

        switch (currentState) {

            case STATE_IDLE:
            {
                // Kích hoạt khi phát hiện nhấn nút ở chân PE7
                if (HAL_GPIO_ReadPin(BTN_START_GPIO_Port, BTN_START_Pin) == GPIO_PIN_RESET) {
                    HAL_Delay(50); // Chống dội phím lần 1

                    // Chờ nhả nút lần 1 (timeout 3s chống kẹt phím)
                    uint32_t press1_timeout = HAL_GetTick();
                    while (HAL_GPIO_ReadPin(BTN_START_GPIO_Port, BTN_START_Pin) == GPIO_PIN_RESET) {
                        HAL_IWDG_Refresh(&hiwdg);
                        if (HAL_GetTick() - press1_timeout > 3000) break;
                    }

                    // Nếu bị kẹt phím (giữ quá 3s) → bỏ qua
                    if (HAL_GetTick() - press1_timeout >= 3000) {
                        break;
                    }

                    // --- PHÁT HIỆN DOUBLE-CLICK ---
                    // Sau khi thả nút lần 1, chờ tối đa 500ms xem có nhấn lần 2 không
                    uint8_t is_double_click = 0;
                    uint32_t wait_start = HAL_GetTick();
                    while (HAL_GetTick() - wait_start < 500) {
                        HAL_IWDG_Refresh(&hiwdg);
                        if (HAL_GPIO_ReadPin(BTN_START_GPIO_Port, BTN_START_Pin) == GPIO_PIN_RESET) {
                            HAL_Delay(50); // Chống dội phím lần 2
                            if (HAL_GPIO_ReadPin(BTN_START_GPIO_Port, BTN_START_Pin) == GPIO_PIN_RESET) {
                                is_double_click = 1;
                                uint32_t press2_timeout = HAL_GetTick();
                                while (HAL_GPIO_ReadPin(BTN_START_GPIO_Port, BTN_START_Pin) == GPIO_PIN_RESET) {
                                    HAL_IWDG_Refresh(&hiwdg);
                                    if (HAL_GetTick() - press2_timeout > 3000) break;
                                }
                            }
                            break;
                        }
                    }

                    if (is_double_click) {
                        // === CHẾ ĐỘ TRÁNG ỐNG NGHIỆM (RINSE MODE) ===
                        rinse_count = 0;
                        CLCD_I2C_Clear(&lcd1);
                        CLCD_I2C_SetCursor(&lcd1, 0, 0);
                        CLCD_I2C_WriteString(&lcd1, ">>>TRANG ONG<<<");
                        CLCD_I2C_SetCursor(&lcd1, 0, 1);
                        CLCD_I2C_WriteString(&lcd1, "Bom nuoc vao...");
                        printf("=> RINSE MODE: Bat dau trang ong nghiem...\r\n");

                        Pump_Water_In(true);
                        state_timer = HAL_GetTick();
                        currentState = STATE_RINSE_PUMP_IN;
                    } else {
                        // === CHU TRÌNH ĐO BÌNH THƯỜNG ===
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
            }

            case STATE_PUMP_IN:
                // Cấp nước trong 500ms
                if (HAL_GetTick() - state_timer >= 500) {
                    Pump_Water_In(false); // Tắt bơm cấp
                    Reset_Drop_Counters();

                    CLCD_I2C_SetCursor(&lcd1, 0, 0);
                    CLCD_I2C_WriteString(&lcd1, "2. Nho thuoc L1");
                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, "Giot: 0/5       ");
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
                    sprintf(lcd_buf, "Giot: %d/5      ", count_drop_1);
                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, lcd_buf);
                }

                if (count_drop_1 >= 5) {
                    last_drop_1 = -1; // Reset cho lan sau
                    Pump_Reagent_1(false); // Tắt bơm lọ 1
                    Reset_Drop_Counters();

                    CLCD_I2C_Clear(&lcd1);
                    CLCD_I2C_SetCursor(&lcd1, 0, 0);
                    CLCD_I2C_WriteString(&lcd1, "Khuay Lo 1...   ");
                    printf("=> B2.5: Bat dau khuay thuoc thu Lo 1...\r\n");

                    Stirrer_Enable(true); // Bật IC TMC2209
                    state_timer = HAL_GetTick();
                    currentState = STATE_STIR_WAIT_1;
                }
                // Timeout chống tắc ống
                else if (HAL_GetTick() - timeout_timer > 30000) {
                    last_drop_1 = -1; // Reset khi thoat ra ngoai
                    Pump_Reagent_1(false);
                    printf("WARN: Timeout Lo 1! Huy chu trinh.\r\n");
                    currentState = STATE_PUMP_OUT; // Ép xả nước
                }
                break;
            }

            case STATE_STIR_WAIT_1:
            {    // Cập nhật LCD đếm lùi
                uint32_t elapsed_sec = (HAL_GetTick() - state_timer) / 1000;
                static uint32_t last_elapsed_sec_1 = 999;
                if (last_elapsed_sec_1 != elapsed_sec) {
                    last_elapsed_sec_1 = elapsed_sec;
                    sprintf(lcd_buf, "Cho: %us/10s  ", (unsigned int)elapsed_sec);
                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, lcd_buf);
                }

                if (HAL_GetTick() - state_timer >= 10000) { // Đợi 10 giây
                    last_elapsed_sec_1 = 999; // Reset cho lan sau
                    Stirrer_Enable(false); // Ngắt động cơ bước

                    CLCD_I2C_Clear(&lcd1);
                    CLCD_I2C_SetCursor(&lcd1, 0, 0);
                    CLCD_I2C_WriteString(&lcd1, "3. Nho thuoc L2");
                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, "Giot: 0/5       ");
                    printf("=> B3: Nho thuoc thu Lo 2...\r\n");

                    Pump_Reagent_2(true);
                    timeout_timer = HAL_GetTick();
                    currentState = STATE_DROP_2;
                }
                break;
            }

            case STATE_DROP_2:
            {
                static int last_drop_2 = -1;
                
                if (last_drop_2 != count_drop_2) {
                    last_drop_2 = count_drop_2;
                    sprintf(lcd_buf, "Giot: %d/5      ", count_drop_2);
                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, lcd_buf);
                }

                if (count_drop_2 >= 5) {
                    last_drop_2 = -1; // Reset cho lan sau
                    Pump_Reagent_2(false);

                    CLCD_I2C_Clear(&lcd1);
                    CLCD_I2C_SetCursor(&lcd1, 0, 0);
                    CLCD_I2C_WriteString(&lcd1, "4. Khuay&PhanUng");
                    printf("=> B4: Bat dau khuay tu va cho phan ung...\r\n");

                    Stirrer_Enable(true); // Bật IC TMC2209
                    state_timer = HAL_GetTick();
                    currentState = STATE_STIR_WAIT_2;
                }
                else if (HAL_GetTick() - timeout_timer > 30000) {
                    last_drop_2 = -1; // Reset
                    Pump_Reagent_2(false);
                    printf("WARN: Timeout Lo 2! Huy chu trinh.\r\n");
                    currentState = STATE_PUMP_OUT;
                }
                break;
            }

            case STATE_STIR_WAIT_2:
            {    // Cập nhật LCD đếm lùi — chờ 30 giây cho phản ứng hóa học
                uint32_t elapsed_sec = (HAL_GetTick() - state_timer) / 1000;
                static uint32_t last_elapsed_sec = 999;
                
                // Ngắt khuấy từ sau 10s đầu tiên để tiến hành ủ màu 20s còn lại
                if (HAL_GetTick() - state_timer > 10000) {
                    Stirrer_Enable(false); // Ngắt động cơ bước (ủ màu tĩnh)
                }

                if (last_elapsed_sec != elapsed_sec) {
                    last_elapsed_sec = elapsed_sec;
                    sprintf(lcd_buf, "Cho: %us/30s  ", (unsigned int)elapsed_sec);
                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, lcd_buf);
                }

                if (HAL_GetTick() - state_timer >= 30000) { // Đợi tổng 30 giây
                    last_elapsed_sec = 999; // Reset cho lan sau
                    Stirrer_Enable(false); // Đảm bảo đã ngắt động cơ bước

                    CLCD_I2C_Clear(&lcd1);
                    CLCD_I2C_SetCursor(&lcd1, 0, 0);
                    CLCD_I2C_WriteString(&lcd1, "5. Doc quang pho");
                    printf("=> B5: Đoc quang pho AS7341...\r\n");

                    currentState = STATE_MEASURE;
                }
                break;
            }

            case STATE_MEASURE:
            {
                // === BẢO VỆ EMI: Re-init lại AS7341 ===
                as7341.begin(AS7341_I2CADDR_DEFAULT, &hi2c2);
                as7341.setGain(AS7341_GAIN_16X);
                as7341.setATIME(100);
                as7341.setASTEP(999);

                // Bật LED đo màu qua TIM2 PWM (PA2, 10kHz, duty 80%)
                __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, LED_PWM_ON);
                HAL_Delay(150); // Chờ LED sáng ổn định

                // Tạm tắt TIM6 (băm xung bơm)
                HAL_TIM_Base_Stop_IT(&htim6);

                uint8_t read_success = 0;

                // Kiểm tra kết nối vật lý I2C trước khi đọc (Fail-fast)
                if (HAL_I2C_IsDeviceReady(&hi2c2, (AS7341_I2CADDR_DEFAULT << 1), 3, 100) != HAL_OK) {
                    printf("!! LỖI VẬT LÝ: AS7341 mat ket noi I2C!\r\n");
                } else {
                    // === RETRY LOGIC: Thử đọc tối đa AS7341_MAX_RETRIES lần ===
                    for (uint8_t retry = 0; retry < AS7341_MAX_RETRIES; retry++) {
                        HAL_IWDG_Refresh(&hiwdg);
                        printf(">> Doc AS7341 lan %d/%d...\r\n", retry + 1, AS7341_MAX_RETRIES);
                        if (as7341.readAllChannels()) {
                            read_success = 1;
                            break;
                        }
                        printf("!! AS7341: Lan %d that bai.\r\n", retry + 1);

                        // Hiển thị thông báo retry trên LCD
                        if (retry < AS7341_MAX_RETRIES - 1) {
                            sprintf(lcd_buf, "Retry %d/%d...   ", retry + 2, AS7341_MAX_RETRIES);
                            CLCD_I2C_SetCursor(&lcd1, 0, 1);
                            CLCD_I2C_WriteString(&lcd1, lcd_buf);
                            HAL_Delay(500); // Đợi ngắn trước khi thử lại
                        }
                    }
                }

                // Tắt LED đo màu ngay sau khi đọc (dù thành công hay thất bại)
                __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, LED_PWM_OFF);

                // BẬT lại TIM6 sau khi giao tiếp I2C xong
                HAL_TIM_Base_Start_IT(&htim6);

                if (read_success) {
                    uint16_t f4 = as7341.getChannel(AS7341_CHANNEL_515nm_F4);
                    uint16_t f6 = as7341.getChannel(AS7341_CHANNEL_590nm_F6);
                    uint16_t f8 = as7341.getChannel(AS7341_CHANNEL_680nm_F8);
                    uint16_t clear = as7341.getChannel(AS7341_CHANNEL_CLEAR);

                    float min_dist = 0;
                    float no2_val = Tinh_Nong_Do_NO2(f4, f6, f8, clear, &min_dist);

                    // In kết quả lên màn hình
                    CLCD_I2C_Clear(&lcd1);
                    CLCD_I2C_SetCursor(&lcd1, 0, 0);
                    CLCD_I2C_WriteString(&lcd1, "Nong Do NO2:");

                    if (no2_val < 0) {
                        sprintf(lcd_buf, "Loi tinh toan!  ");
                        printf(">> RAW DATA: F4:%u, F6:%u, F8:%u, Clear:%u\r\n", f4, f6, f8, clear);
                        printf(">> KET QUA NO2: Loi (Clear=0)\r\n");
                    } else {
                        int no2_int = (int)no2_val;
                        int no2_frac = (int)(no2_val * 100) % 100;
                        sprintf(lcd_buf, "%d.%02d mg/L", no2_int, no2_frac);
                        printf(">> RAW DATA: F4:%u, F6:%u, F8:%u, Clear:%u\r\n", f4, f6, f8, clear);
                        printf(">> KET QUA NO2: %d.%02d mg/L (Dist: %d.%03d)\r\n",
                               no2_int, no2_frac,
                               (int)min_dist, (int)(min_dist * 1000) % 1000);
                    }

                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, lcd_buf);
                } else {
                    // TẤT CẢ RETRY ĐỀU THẤT BẠI → bỏ qua đo, chuyển sang xả
                    CLCD_I2C_Clear(&lcd1);
                    CLCD_I2C_SetCursor(&lcd1, 0, 0);
                    CLCD_I2C_WriteString(&lcd1, "Loi Doc AS7341!");
                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, "Bo qua -> Xa...");
                    printf("!! AS7341: THAT BAI SAU %d LAN. Bo qua buoc do.\r\n", AS7341_MAX_RETRIES);
                }

                // Giữ màn hình kết quả 15 giây trước khi xả nước
                // Dùng vòng lặp nhỏ để vẫn kiểm tra long-press + feed IWDG
                {
                    uint32_t display_start = HAL_GetTick();
                    while (HAL_GetTick() - display_start < 15000) {
                        HAL_IWDG_Refresh(&hiwdg);

                        if (Check_Long_Press_Reset()) {
                            break; // Đã reset, thoát ra
                        }
                        HAL_Delay(50);
                    }
                    // Nếu đã bị reset bởi long-press, không chuyển state nữa
                    if (currentState == STATE_IDLE) break;
                }

                CLCD_I2C_Clear(&lcd1);
                CLCD_I2C_SetCursor(&lcd1, 0, 0);
                CLCD_I2C_WriteString(&lcd1, "6. Xa nuoc thai");
                printf("=> B6: Xa sach cuvet...\r\n");

                Pump_Water_Out(true);
                state_timer = HAL_GetTick();
                currentState = STATE_PUMP_OUT;
                break;
            }

            case STATE_PUMP_OUT:
                // Xả rỗng trong 2500ms
                if (HAL_GetTick() - state_timer >= 2500) {
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

            // ============================================
            // === CHẾ ĐỘ TRÁNG ỐNG NGHIỆM (RINSE MODE) ===
            // Kích hoạt bằng cách nhấn đôi nút START
            // Chu trình: Bơm nước vào (3s) → Khuấy từ tốc độ cao (10s) → Xả nước (4s)
            // Lặp lại 1 lần để tráng sạch
            // ============================================

            case STATE_RINSE_PUMP_IN:
                // Bơm nước vào cuvet để tráng trong 700ms
                if (HAL_GetTick() - state_timer >= 700) {
                    Pump_Water_In(false);

                    rinse_count++;
                    sprintf(lcd_buf, "Khuay trang %d/1", (unsigned int)rinse_count);
                    CLCD_I2C_SetCursor(&lcd1, 0, 0);
                    CLCD_I2C_WriteString(&lcd1, ">>>TRANG ONG<<<");
                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, lcd_buf);
                    printf("=> RINSE [%d/1]: Bat dau khuay tu toc cao...\r\n", (unsigned int)rinse_count);

                    Stirrer_Enable(true);
                    state_timer = HAL_GetTick();
                    currentState = STATE_RINSE_STIR;
                }
                break;

            case STATE_RINSE_STIR:
            {
                // Khuấy từ tốc độ cao 10 giây (xung 20us/bước) — xử lý ở đầu while(1)
                uint32_t rinse_elapsed = (HAL_GetTick() - state_timer) / 1000;
                static uint32_t last_rinse_sec = 999;
                if (last_rinse_sec != rinse_elapsed) {
                    last_rinse_sec = rinse_elapsed;
                    sprintf(lcd_buf, "Cho: %us/10s  ", (unsigned int)rinse_elapsed);
                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, lcd_buf);
                }

                if (HAL_GetTick() - state_timer >= 10000) {
                    last_rinse_sec = 999;
                    Stirrer_Enable(false);

                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, "Xa nuoc trang...");
                    printf("=> RINSE [%d/1]: Xa nuoc trang ra...\r\n", (unsigned int)rinse_count);

                    Pump_Water_Out(true);
                    state_timer = HAL_GetTick();
                    currentState = STATE_RINSE_PUMP_OUT;
                }
                break;
            }

            case STATE_RINSE_PUMP_OUT:
                // Xả nước tráng ra trong 2500ms
                if (HAL_GetTick() - state_timer >= 2500) {
                    Pump_Water_Out(false);

                    if (rinse_count < 1) {
                        // Còn lần tráng tiếp theo → bơm nước vào lần nữa
                        CLCD_I2C_SetCursor(&lcd1, 0, 0);
                        CLCD_I2C_WriteString(&lcd1, ">>>TRANG ONG<<<");
                        CLCD_I2C_SetCursor(&lcd1, 0, 1);
                        CLCD_I2C_WriteString(&lcd1, "Bom nuoc vao...");
                        printf("=> RINSE: Tiep tuc trang lan %d/1...\r\n", (unsigned int)(rinse_count + 1));

                        Pump_Water_In(true);
                        state_timer = HAL_GetTick();
                        currentState = STATE_RINSE_PUMP_IN;
                    } else {
                        // Đã tráng đủ 1 lần → Kết thúc
                        CLCD_I2C_Clear(&lcd1);
                        CLCD_I2C_SetCursor(&lcd1, 0, 0);
                        CLCD_I2C_WriteString(&lcd1, "TRANG XONG!");
                        CLCD_I2C_SetCursor(&lcd1, 0, 1);
                        CLCD_I2C_WriteString(&lcd1, "He thong sach.");
                        printf("--- HOAN THANH TRANG ONG NGHIEM (1/1) ---\r\n\r\n");
                        HAL_Delay(2000);

                        CLCD_I2C_Clear(&lcd1);
                        CLCD_I2C_SetCursor(&lcd1, 0, 0);
                        CLCD_I2C_WriteString(&lcd1, "He Thong SanSang");
                        CLCD_I2C_SetCursor(&lcd1, 0, 1);
                        CLCD_I2C_WriteString(&lcd1, "Nhan nut START..");
                        currentState = STATE_IDLE;
                    }
                }
                break;
        }
    }
}
