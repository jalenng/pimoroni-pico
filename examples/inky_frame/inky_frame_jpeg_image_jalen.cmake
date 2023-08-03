add_executable(
  inky_frame_7_jpeg_image_jalen
  inky_frame_jpeg_image_jalen.cpp
)

# Pull in pico libraries that we need
target_link_libraries(inky_frame_7_jpeg_image_jalen pico_rand pico_stdlib jpegdec inky_frame_7 fatfs hardware_pwm hardware_spi hardware_i2c hardware_rtc fatfs sdcard pico_graphics)

pico_enable_stdio_usb(inky_frame_7_jpeg_image_jalen 1)

# create map/bin/hex file etc.
pico_add_extra_outputs(inky_frame_7_jpeg_image_jalen)

target_compile_definitions(inky_frame_7_jpeg_image_jalen PUBLIC INKY_FRAME_7)
