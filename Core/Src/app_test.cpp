/* USER CODE BEGIN Header */
/**
  * @file           : app_test.cpp
  * @brief          : He thong do NO2 Tu Dong - Môi trường Test Kịch Bản
  */
/* USER CODE END Header */

#include "app_test.h"
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
extern "C" TIM_HandleTypeDef htim2; // TIM2 CH3 - LED PWM 10kHz (PA2)

/* --- KHỞI TẠO OBJECT CẢM BIẾN & LCD --- */
static Adafruit_AS7341 as7341;
static CLCD_I2C_Name lcd1;

/* --- THUẬT TOÁN NHẬN DIỆN MÀU NO2 (SERA KIT) --- */
typedef struct {
    float concentration;
    float norm_F4; // Kênh Xanh lá (515nm)
    float norm_F6; // Kênh Cam (590nm)
    float norm_F8; // Kênh Đỏ (680nm)
} Sera_Color_Profile;

// Bảng Calibration (Cần hiệu chuẩn lại với buồng đo thực tế)
static Sera_Color_Profile NO2_Profiles[5] = {
    {0.0,  0.25, 0.30, 0.40},
    {0.5,  0.20, 0.32, 0.45},
    {1.0,  0.15, 0.35, 0.50},
    {2.0,  0.08, 0.35, 0.55},
    {5.0,  0.02, 0.20, 0.65}
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
    STATE_PUMP_IN,
    STATE_DROP_1,
    STATE_DROP_2,
    STATE_STIR_WAIT,
    STATE_MEASURE,
    STATE_PUMP_OUT,
    // --- CHẾ ĐỘ TRÁNG ỐNG NGHIỆM (RINSE MODE) ---
    STATE_RINSE_PUMP_IN,   // Bơm nước vào để tráng
    STATE_RINSE_STIR,      // Khuấy từ để tráng đều
    STATE_RINSE_PUMP_OUT   // Xả nước tráng ra
} SystemState;

static SystemState currentState = STATE_IDLE;
static uint32_t state_timer = 0;
static uint32_t timeout_timer = 0; // Chống treo bơm nhu động
static char lcd_buf[32];           // Buffer in chữ lên LCD (đủ lớn để tránh format overflow warning)
static uint8_t rinse_count = 0;   // Đếm số lần tráng (tráng 2 lần)

/* --- HÀM CHẠY CHÍNH (GỌI TỪ MAIN.C) --- */
void Test_App(void) {
    printf("\r\n==== HE THONG DO NO2 TỰ ĐỘNG (TEST MODE) ====\r\n");

    /* 1. Khởi tạo LCD 1602 */
    CLCD_I2C_Init(&lcd1, &hi2c1, 0x4E, 16, 2);
    CLCD_I2C_Clear(&lcd1);
    CLCD_I2C_SetCursor(&lcd1, 0, 0);
    CLCD_I2C_WriteString(&lcd1, "HUST - NO2 SYS");
    CLCD_I2C_SetCursor(&lcd1, 0, 1);
    CLCD_I2C_WriteString(&lcd1, "Khoi tao (Test)");
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
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, LED_PWM_OFF); // Tắt LED đo màu (PA2 PWM)

    currentState = STATE_IDLE;
    CLCD_I2C_Clear(&lcd1);
    CLCD_I2C_SetCursor(&lcd1, 0, 0);
    CLCD_I2C_WriteString(&lcd1, "He Thong SanSang");
    CLCD_I2C_SetCursor(&lcd1, 0, 1);
    CLCD_I2C_WriteString(&lcd1, "Nhan nut START..");

    /* VÒNG LẶP CHÍNH MÁY TRẠNG THÁI (NON-BLOCKING) */
    while (1) {

        // Luôn duy trì xung khuấy từ nếu đang ở trạng thái chờ phản ứng hoặc tráng ống
        if (currentState == STATE_STIR_WAIT || currentState == STATE_RINSE_STIR) {
            Stirrer_Update(50); // Phát xung chạy động cơ bước nhanh hơn (50us/bước)
        }

        switch (currentState) {

            case STATE_IDLE:
            {
                // Kích hoạt khi phát hiện nhấn nút ở chân PE7
                if (HAL_GPIO_ReadPin(BTN_START_GPIO_Port, BTN_START_Pin) == GPIO_PIN_RESET) {
                    HAL_Delay(50); // Chống dội phím lần 1

                    // Chờ nhả nút lần 1 (với timeout 3s chống kẹt phím)
                    uint32_t press1_timeout = HAL_GetTick();
                    while (HAL_GPIO_ReadPin(BTN_START_GPIO_Port, BTN_START_Pin) == GPIO_PIN_RESET) {
                        if (HAL_GetTick() - press1_timeout > 3000) break; // Bỏ qua nếu kẹt nút
                    }

                    // Nếu bị kẹt phím (giữ quá 3s) → bỏ qua, không làm gì
                    if (HAL_GetTick() - press1_timeout >= 3000) {
                        break;
                    }

                    // --- PHÁT HIỆN DOUBLE-CLICK ---
                    // Sau khi thả nút lần 1, chờ tối đa 500ms xem có nhấn lần 2 không
                    uint8_t is_double_click = 0;
                    uint32_t wait_start = HAL_GetTick();
                    while (HAL_GetTick() - wait_start < 500) {
                        if (HAL_GPIO_ReadPin(BTN_START_GPIO_Port, BTN_START_Pin) == GPIO_PIN_RESET) {
                            HAL_Delay(50); // Chống dội phím lần 2
                            // Xác nhận là nhấn thật (không phải nhiễu)
                            if (HAL_GPIO_ReadPin(BTN_START_GPIO_Port, BTN_START_Pin) == GPIO_PIN_RESET) {
                                is_double_click = 1;
                                // Chờ nhả nút lần 2 (timeout 3s chống kẹt)
                                uint32_t press2_timeout = HAL_GetTick();
                                while (HAL_GPIO_ReadPin(BTN_START_GPIO_Port, BTN_START_Pin) == GPIO_PIN_RESET) {
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
                // Cấp nước trong 18000ms
                if (HAL_GetTick() - state_timer >= 5000) {
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
                // TIM6 ISR tự động băm xung ON=5ms / OFF=400ms cho bơm LO_1
                // Không cần viết code PWM thủ công ở đây nữa

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
                // Test Mode: Bỏ qua cảm biến đếm giọt, chạy bơm L2 trong 2 giây
                if (HAL_GetTick() - timeout_timer > 2000) {
                    Pump_Reagent_2(false);

                    CLCD_I2C_Clear(&lcd1);
                    CLCD_I2C_SetCursor(&lcd1, 0, 0);
                    CLCD_I2C_WriteString(&lcd1, "4. Khuay&PhanUng");
                    printf("=> B4: Bat dau khuay tu va cho phan ung (Test)...\r\n");

                    Stirrer_Enable(true); // Bật IC TMC2209
                    state_timer = HAL_GetTick();
                    currentState = STATE_STIR_WAIT;
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
                // Bật LED đo màu qua TIM2 PWM (PA2, 10kHz, duty 80%)
                __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, LED_PWM_ON);
                HAL_Delay(150); // Chờ LED sáng ổn định

                if (as7341.readAllChannels()) {
                    // Tắt LED đo màu ngay sau khi đọc xong
                    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, LED_PWM_OFF);

                    uint16_t f4 = as7341.getChannel(AS7341_CHANNEL_515nm_F4);
                    uint16_t f6 = as7341.getChannel(AS7341_CHANNEL_590nm_F6);
                    uint16_t f8 = as7341.getChannel(AS7341_CHANNEL_680nm_F8);
                    uint16_t clear = as7341.getChannel(AS7341_CHANNEL_CLEAR);

                    float min_dist = 0;
                    float no2_val = Tinh_Nong_Do_NO2(f4, f6, f8, clear, &min_dist);

                    // In kết quả lên màn hình
                    CLCD_I2C_Clear(&lcd1);

                    if (no2_val < 0) {
                        CLCD_I2C_SetCursor(&lcd1, 0, 0);
                        CLCD_I2C_WriteString(&lcd1, "Loi tinh toan!  ");
                        printf(">> RAW DATA: F4:%u, F6:%u, F8:%u, Clear:%u\r\n", f4, f6, f8, clear);
                        printf(">> KET QUA NO2: Loi (Clear=0)\r\n");
                    } else {
                        int dist_int = (int)min_dist;
                        int dist_frac = (int)(min_dist * 1000) % 1000;
                        
                        sprintf(lcd_buf, "4:%-5u 6:%-5u", f4, f6);
                        CLCD_I2C_SetCursor(&lcd1, 0, 0);
                        CLCD_I2C_WriteString(&lcd1, lcd_buf);
                        
                        sprintf(lcd_buf, "8:%-5u D:%d.%03d", f8, dist_int, dist_frac);
                        CLCD_I2C_SetCursor(&lcd1, 0, 1);
                        CLCD_I2C_WriteString(&lcd1, lcd_buf);
                        
                        printf(">> RAW DATA: F4:%u, F6:%u, F8:%u, Clear:%u\r\n", f4, f6, f8, clear);
                        printf(">> KET QUA NO2: %d.%02d mg/L (Dist: %d.%03d)\r\n", (int)no2_val, (int)(no2_val * 100) % 100, dist_int, dist_frac);
                    }
                } else {
                    // Đảm bảo tắt LED kể cả khi lỗi
                    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, LED_PWM_OFF);
                    CLCD_I2C_Clear(&lcd1);
                    CLCD_I2C_WriteString(&lcd1, "Loi Doc AS7341!");
                }

                // Giữ màn hình kết quả 20 giây trước khi xả nước
                HAL_Delay(20000);

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

            // ============================================
            // === CHẾ ĐỘ TRÁNG ỐNG NGHIỆM (RINSE MODE) ===
            // Kích hoạt bằng cách nhấn đôi nút START
            // Chu trình: Bơm nước vào (3s) → Khuấy từ tốc độ cao (10s) → Xả nước ra (4s)
            // Lặp lại 2 lần để tráng sạch
            // ============================================

            case STATE_RINSE_PUMP_IN:
                // Bơm nước vào cuvet để tráng trong 3000ms
                if (HAL_GetTick() - state_timer >= 3000) {
                    Pump_Water_In(false); // Tắt bơm cấp

                    rinse_count++;
                    sprintf(lcd_buf, "Khuay trang %d/2", (unsigned int)rinse_count);
                    CLCD_I2C_SetCursor(&lcd1, 0, 0);
                    CLCD_I2C_WriteString(&lcd1, ">>>TRANG ONG<<<");
                    CLCD_I2C_SetCursor(&lcd1, 0, 1);
                    CLCD_I2C_WriteString(&lcd1, lcd_buf);
                    printf("=> RINSE [%d/2]: Bat dau khuay tu trang...\r\n", (unsigned int)rinse_count);

                    Stirrer_Enable(true);
                    state_timer = HAL_GetTick();
                    currentState = STATE_RINSE_STIR;
                }
                break;

            case STATE_RINSE_STIR:
                // Duy trì xung bước động cơ khuấy từ tốc độ cao (non-blocking)
                Stirrer_Update(20); // 20us/bước — nhanh hơn 2.5× so với chế độ đo

                // Hiển thị đếm lùi thời gian khuấy
                {
                    uint32_t rinse_elapsed = (HAL_GetTick() - state_timer) / 1000;
                    static uint32_t last_rinse_sec = 999;
                    if (last_rinse_sec != rinse_elapsed) {
                        last_rinse_sec = rinse_elapsed;
                        sprintf(lcd_buf, "Cho: %us/10s  ", (unsigned int)rinse_elapsed);
                        CLCD_I2C_SetCursor(&lcd1, 0, 1);
                        CLCD_I2C_WriteString(&lcd1, lcd_buf);
                    }

                    // Khuấy đủ 10 giây thì chuyển sang xả nước tráng
                    if (HAL_GetTick() - state_timer >= 10000) {
                        last_rinse_sec = 999; // Reset cho lần sau
                        Stirrer_Enable(false);

                        CLCD_I2C_SetCursor(&lcd1, 0, 1);
                        CLCD_I2C_WriteString(&lcd1, "Xa nuoc trang...");
                        printf("=> RINSE [%d/2]: Xa nuoc trang ra...\r\n", (unsigned int)rinse_count);

                        Pump_Water_Out(true);
                        state_timer = HAL_GetTick();
                        currentState = STATE_RINSE_PUMP_OUT;
                    }
                }
                break;

            case STATE_RINSE_PUMP_OUT:
                // Xả nước tráng ra trong 4000ms
                if (HAL_GetTick() - state_timer >= 4000) {
                    Pump_Water_Out(false);

                    if (rinse_count < 2) {
                        // Còn lần tráng tiếp theo → bơm nước vào lần nữa
                        CLCD_I2C_SetCursor(&lcd1, 0, 0);
                        CLCD_I2C_WriteString(&lcd1, ">>>TRANG ONG<<<");
                        CLCD_I2C_SetCursor(&lcd1, 0, 1);
                        CLCD_I2C_WriteString(&lcd1, "Bom nuoc vao...");
                        printf("=> RINSE: Tiep tuc trang lan %d/2...\r\n", (unsigned int)(rinse_count + 1));

                        Pump_Water_In(true);
                        state_timer = HAL_GetTick();
                        currentState = STATE_RINSE_PUMP_IN;
                    } else {
                        // Đã tráng đủ 2 lần → Kết thúc
                        CLCD_I2C_Clear(&lcd1);
                        CLCD_I2C_SetCursor(&lcd1, 0, 0);
                        CLCD_I2C_WriteString(&lcd1, "TRANG XONG!");
                        CLCD_I2C_SetCursor(&lcd1, 0, 1);
                        CLCD_I2C_WriteString(&lcd1, "He thong sach.");
                        printf("--- HOAN THANH TRANG ONG NGHIEM (2/2) ---\r\n\r\n");
                        HAL_Delay(2000);

                        CLCD_I2C_Clear(&lcd1);
                        CLCD_I2C_SetCursor(&lcd1, 0, 0);
                        CLCD_I2C_WriteString(&lcd1, "He Thong SanSang");
                        CLCD_I2C_SetCursor(&lcd1, 0, 1);
                        CLCD_I2C_WriteString(&lcd1, "Nhan nut START..");
                        currentState = STATE_IDLE; // Quay về chờ lệnh
                    }
                }
                break;
        }
    }
}
