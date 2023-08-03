#include "psram_display.hpp"

#include <cstdlib>
#include <math.h>
#include <string.h>

namespace pimoroni {

  enum reg {
    WRITE = 0x02,
    READ = 0x03,
    RESET_ENABLE = 0x66,
    RESET = 0x99
  };

  void PSRamDisplay::init() {
    uint baud = spi_init(spi, 31'250'000);
    printf("PSRam connected at %u\n", baud);
    gpio_set_function(CS, GPIO_FUNC_SIO);
    gpio_set_dir(CS, GPIO_OUT);
    gpio_put(CS, 1);

    gpio_set_function(SCK,  GPIO_FUNC_SPI);
    gpio_set_function(MOSI, GPIO_FUNC_SPI);
    gpio_set_function(MISO, GPIO_FUNC_SPI);

    gpio_put(CS, 0);
    uint8_t command_buffer[2] = {RESET_ENABLE, RESET};
    spi_write_blocking(spi, command_buffer, 2);
    gpio_put(CS, 1);
  }
  
  void PSRamDisplay::write(uint32_t address, size_t len, const uint16_t *data)
  {
    gpio_put(CS, 0);
    uint8_t command_buffer[4] = {WRITE, (uint8_t)((address >> 16) & 0xFF), (uint8_t)((address >> 8) & 0xFF), (uint8_t)(address & 0xFF)};
    spi_write_blocking(spi, command_buffer, 4);
    spi_write_blocking(spi, (const uint8_t*)data, len*2);
    gpio_put(CS, 1);
  }

  void PSRamDisplay::write(uint32_t address, size_t len, const uint16_t data)
  {
    uint8_t byte8[2] = {
      (uint8_t)((data >> 8) & 0xFF),
      (uint8_t)(data & 0xFF)
    };
    gpio_put(CS, 0);
    uint8_t command_buffer[4] = {WRITE, (uint8_t)((address >> 16) & 0xFF), (uint8_t)((address >> 8) & 0xFF), (uint8_t)(address & 0xFF)};
    spi_write_blocking(spi, command_buffer, 4);
    SpiSetBlocking(byte8[0], len);
    SpiSetBlocking(byte8[1], len);
    gpio_put(CS, 1);
  }


  void PSRamDisplay::read(uint32_t address, size_t len, uint16_t *data)
  {
    uint8_t data8[len * 2];
    gpio_put(CS, 0);
    uint8_t command_buffer[4] = {READ, (uint8_t)((address >> 16) & 0xFF), (uint8_t)((address >> 8) & 0xFF), (uint8_t)(address & 0xFF)};
    spi_write_blocking(spi, command_buffer, 4);
    spi_read_blocking(spi, 0, data8, len*2);
    gpio_put(CS, 1);
    for (size_t i = 0; i < len; i++) {
      data[i] = data8[(2 * i)] << 8;
      data[i] |= data8[(2 * i) + 1] & 0xFF;
    }
  }

  void PSRamDisplay::write_pixel(const Point &p, uint16_t colour)
  {
    write(pointToAddress(p), 1, colour);
  }

  void PSRamDisplay::write_pixel_span(const Point &p, uint l, uint16_t colour)
  {
    write(pointToAddress(p), l, colour);
  }

  void PSRamDisplay::read_pixel_span(const Point &p, uint l, uint16_t *data)
  {
    read(pointToAddress(p), l, data);
  }
}
