# Hệ thống Đo nồng độ NO2 Tự động (Automated NO2 Measurement System)

Dự án firmware cho hệ thống lấy mẫu, pha trộn hóa chất và đo nồng độ NO2 tự động sử dụng vi điều khiển STM32. Hệ thống mô phỏng lại quá trình thử nước bằng **Sera NO2 Test Kit** một cách tự động hóa hoàn toàn thông qua bơm màng, bơm nhu động, máy khuấy từ và cảm biến quang phổ AS7341.

## 🌟 Tính năng chính

- **Tự động hóa hoàn toàn quy trình đo:** Bơm chính xác 5ml mẫu nước, nhỏ tự động 5 giọt dung dịch thử (có cảm biến hồng ngoại đếm giọt), tự động khuấy từ trộn đều và ủ màu.
- **Phân tích quang phổ (Spectrometry):** Sử dụng cảm biến 11 kênh màu AS7341 kết hợp đèn LED chiếu sáng để đo màu dung dịch.
- **Thuật toán thông minh:** Tích hợp thuật toán K-Nearest Neighbors (KNN, K=1) phân tích cường độ phản xạ của các dải sóng Xanh (515nm), Cam (590nm) và Đỏ (680nm) sau khi đã chuẩn hóa bù sáng (kênh Clear) để tính toán chính xác nồng độ NO2 theo bảng màu chuẩn.
- **Chế độ tráng rửa (Rinse Mode):** Tích hợp chu trình tự làm sạch cuvet và bơm xả bằng thao tác nhấp đúp (double-click) nút bấm.
- **Hệ thống cảnh báo & an toàn nâng cao:** Tích hợp tính năng chống treo MCU, kẹt cơ khí, và chống nhiễu điện từ (EMI).

---

## 🛠️ Cấu trúc phần cứng

Hệ thống được thiết kế dựa trên các thành phần sau:
- **Vi điều khiển:** STM32 (sử dụng thư viện HAL).
- **Cảm biến quang phổ:** Adafruit AS7341 (giao tiếp I2C).
- **Cảm biến đếm giọt:** 2x Cảm biến vật cản hồng ngoại (ngắt EXTI) lắp trên vòi bơm.
- **Cơ cấu chất lỏng (Fluidics):**
  - 2x Bơm màng (Diaphragm Pump) dùng cho Cấp mẫu nước & Xả nước thải (điều khiển qua rơ-le/MOSFET).
  - 2x Bơm nhu động (Peristaltic Pump) dùng để nhỏ thuốc thử (điều khiển tốc độ bằng thuật toán Software PWM qua Timer 6).
- **Cơ cấu khuấy (Stirrer):** Động cơ bước + IC TMC2209 tạo từ trường xoay cá từ.
- **Đèn rọi mẫu:** LED trắng độ sáng cao (Hardware PWM 10kHz qua Timer 2).
- **Giao diện người dùng (UI):**
  - Màn hình LCD 16x2 (giao tiếp qua I2C PCF8574).
  - Nút bấm điều khiển duy nhất.

---

## ⚙️ Kiến trúc Phần mềm (Software Architecture)

Hệ thống được lập trình theo kiến trúc **Máy trạng thái không chặn (Non-blocking State Machine)**, cho phép STM32 thực thi liên tục các tác vụ song song như tạo xung điều khiển động cơ bước, hiển thị LCD, và giám sát an toàn.

### Máy Trạng Thái (State Machine Flow)
1. `STATE_IDLE`: Chờ lệnh người dùng.
2. `STATE_PUMP_IN`: Cấp mẫu nước vào cuvet (chuẩn 5ml).
3. `STATE_DROP_1`: Bơm nhu động nhỏ 5 giọt thuốc thử số 1.
4. `STATE_STIR_WAIT_1`: Khuấy từ dung dịch lần 1.
5. `STATE_DROP_2`: Bơm nhu động nhỏ 5 giọt thuốc thử số 2.
6. `STATE_STIR_WAIT_2`: Khuấy trộn lần 2 và chờ ủ phản ứng hóa học (30s).
7. `STATE_MEASURE`: Kích hoạt đèn LED, đọc dữ liệu phổ quang AS7341 và tính nồng độ.
8. `STATE_PUMP_OUT`: Xả toàn bộ nước thải ra ngoài.

---

## 🛡️ Cơ chế Bảo vệ & An toàn (Reliability Features)

Dự án này đặc biệt chú trọng vào khả năng hoạt động bền bỉ, chống chịu nhiễu EMI sinh ra từ các rơ-le/động cơ:

- **Bảo vệ tắc ống/cạn thuốc (Fluidics Timeout):** Nếu cảm biến giọt nước không nhận diện đủ 5 giọt trong vòng 30 giây, hệ thống lập tức hủy chu trình, báo lỗi lên LCD và tự động kích hoạt bơm xả rỗng buồng đo để chống tràn nước.
- **Phục hồi I2C tự động (LCD Watchdog):** Do LCD dễ bị nhiễu EMI làm treo đường truyền I2C, phần mềm liên tục ping mạch LCD mỗi 10 giây. Nếu rớt kết nối, MCU sẽ tự động ép reset bus I2C (Force Reset) và khôi phục màn hình mà không làm sập tiến trình.
- **Fail-Fast & AS7341 Retry:** Tự động re-init lại cảm biến AS7341 trước mỗi lần đo để đề phòng MCU của cảm biến bị treo. Nếu đọc thất bại, hệ thống tự động thử lại 3 lần (Retry Logic) trước khi bỏ cuộc.
- **Nút bấm đa nhiệm (Multi-function Button):**
  - *Nhấn 1 lần:* Chạy chu trình đo.
  - *Nhấn đúp (Double-click):* Chạy chu trình tráng ống.
  - *Nhấn giữ 5 giây (Long-press):* Emergency Reset (Ngắt ngay lập tức toàn bộ động cơ, phục hồi hệ thống về IDLE).
- **Independent Watchdog (IWDG):** Tích hợp hardware watchdog với thời gian timeout 4s để tự động khởi động lại STM32 nếu xảy ra lỗi Crash phần mềm nghiêm trọng.

---

## 🚀 Hướng dẫn sử dụng

1. **Chuẩn bị:** 
   - Nối ống cấp nước mẫu vào đầu vào bơm cấp. 
   - Đặt 2 dây hút của bơm nhu động vào 2 lọ dung dịch thử Sera NO2 tương ứng.
   - Bật nguồn cấp cho hệ thống. Màn hình sẽ hiển thị `He Thong SanSang`.
2. **Đo nồng độ NO2:** Nhấn nút START 1 lần. Đợi khoảng 5.5 phút để máy tự động thao tác, kết quả (`mg/L`) sẽ hiển thị trên LCD.
3. **Tráng rửa cuvet:** Sau 1 lần đo, nhấn đúp (2 lần liên tiếp) nút START. Máy sẽ bơm nước và khuấy tốc độ cao để đánh bay cặn màu còn bám dính.
4. **Dừng khẩn cấp:** Trong trường hợp xảy ra sự cố cơ khí, nhấn và giữ nút START 5 giây để ngắt toàn bộ nguồn động cơ.

---

*Phát triển bởi [QuangAnh - SEEE HUST - 2026]*
