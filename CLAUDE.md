# CLAUDE.md

## Quy tắc bắt buộc: SEARCH FIRST trước khi implement

Trước khi implement BẤT KỲ thứ gì (driver, API, thanh ghi, pin, Kconfig, cấu hình ESP-SR, ...):

1. **Search trước** — tra cứu đồng thời nhiều nguồn:
   - Tài liệu chính thức Espressif (ESP-IDF, ESP-SR, esp-bsp, ESP Component Registry)
   - Wiki / docs / repo của Waveshare cho board
   - Các repo GitHub có code thực tế chạy được (ví dụ: `waveshareteam/ESP32-S3-Touch-AMOLED-1.8`, `espressif/esp-bsp`, `espressif/esp-iot-solution`, `78/xiaozhi-esp32`)
   - Datasheet của chip (CO5300, ES8311, AXP2101, ...)
2. **Không tự nghĩ ra** định nghĩa, tên API, địa chỉ thanh ghi, giá trị cấu hình, pinout hay command init. Mọi giá trị phải có nguồn cụ thể.
3. Ghi rõ nguồn tham khảo (link) trong commit message hoặc comment khi dùng giá trị "magic" (thanh ghi, lệnh init màn hình, pin).
4. Nếu không tìm được nguồn đáng tin cậy → hỏi lại người dùng, không đoán.

## Mục tiêu hiện tại: hỗ trợ board Waveshare ESP32-S3-Touch-AMOLED-1.8

- Người dùng có bản màn hình **CO5300** (bản V2, touch **CST820** @ I2C 0x15) — không phải SH8601/FT3168.
- Giữ tương thích với board bbTalkie gốc (SSD1327 + I2S mic/amp); chọn board qua Kconfig trong `esp-idf/components/hardware_driver`.
- Các thông tin board (pinout, địa chỉ I2C, ES8311, AXP2101, TCA9554) phải được xác minh lại từ nguồn chính thức trước khi dùng.

## Cấu trúc dự án

- Firmware: `esp-idf/src` (ESP-IDF v5.4.3), board drivers: `esp-idf/components/hardware_driver/boards/`
- Driver màn hình hiện tại: `esp-idf/src/components/esp32-spi-ssd1327`

## Trạng thái hỗ trợ Waveshare AMOLED 1.8 (CO5300)

Chọn board: `idf.py menuconfig` → Audio Media HAL → "Waveshare ESP32-S3-Touch-AMOLED-1.8 (CO5300)".

- Audio ES8311: `esp-idf/components/hardware_driver/boards/esp32s3-waveshare-amoled-1.8/bsp_board.c` (AFE input "MR")
- PMU AXP2101: `.../esp32s3-waveshare-amoled-1.8/bsp_power.c` (API chung `bsp_power_*` trong `bsp_board.h`)
- Màn hình CO5300: `esp-idf/src/components/esp32-spi-ssd1327/amoled_co5300.c` — giữ framebuffer 128x128 4bpp,
  phóng 2x ra giữa màn 368x448. Code UI trong `main.c` không đổi.
- Chưa build/test trên phần cứng thật trong môi trường Claude (không có toolchain ESP-IDF).
