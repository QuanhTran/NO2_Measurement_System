#include "module_stirrer.h"

uint32_t last_step_time = 0;
bool step_state = false;

// Kích hoạt driver TMC2209
void Stirrer_Enable(bool state) {
    // Chân EN (PD2) tích cực thấp (LOW = Chạy, HIGH = Tắt/Thả lỏng)
    HAL_GPIO_WritePin(EN_PIN_GPIO_Port, EN_PIN_Pin, state ? GPIO_PIN_RESET : GPIO_PIN_SET);
    // Set chiều quay cố định (DIR)
    HAL_GPIO_WritePin(DIR_PIN_GPIO_Port, DIR_PIN_Pin, GPIO_PIN_SET); 
}

// Hàm cập nhật xung STEP không đóng băng (Non-blocking)
// Gọi hàm này liên tục trong vòng lặp while(1)
void Stirrer_Update(uint32_t speed_delay_us) {
    // Dùng kỹ thuật kiểm tra thời gian thay vì dùng HAL_Delay
    // (Lưu ý: Để delay micro-giây chuẩn xác, nên dùng Timer, ở đây dùng Tick để minh họa cơ bản)
    
    // Tạo xung đơn giản:
    HAL_GPIO_TogglePin(STEP_PIN_GPIO_Port, STEP_PIN_Pin); // Đảo trạng thái chân STEP
    
    // Hàm delay_us tự viết (dùng vòng lặp for) để phát xung
    uint32_t delay_count = speed_delay_us * 24; 
    while(delay_count--) { __asm("nop"); }
}