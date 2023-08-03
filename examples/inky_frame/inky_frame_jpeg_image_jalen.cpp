#include <cstdio>
#include <math.h>

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/rand.h"
#include "hardware/vreg.h"

#include "JPEGDEC.h"

#include "libraries/inky_frame_7/inky_frame_7.hpp"

using namespace pimoroni;

FATFS fs;
FRESULT fr;

InkyFrame inky;
JPEGDEC jpeg;

int fileIndex = 0;

// ----------------------------------------------------------------------------
// Constants
// ----------------------------------------------------------------------------

// Configuration variables
const int INIT_DURATION = 1000;           // provide time for serial monitor to connect
const bool SLEEP_TIME_IS_DURATION = true; // is SLEEP_TIME a delay duration or a scheduled time?
const datetime_t SLEEP_TIME = {           // time to sleep for until next refresh
    .hour = 12,
    .min = 0,
    .sec = 0};
const double GLOBAL_WEIGHT = 1; // global weight multiplier used for error diffusion
const bool USE_LEDS = true;     // use LEDs to indicate busy status and errors?

// Define the Floyd-Steinberg dithering matrix.
// x-offset, y-offset, weight
const double dither_matrix[4][3] = {
    {1, 0, (double)7 / 16},
    {-1, 1, (double)3 / 16},
    {0, 1, (double)5 / 16},
    {1, 1, (double)1 / 16}};

// Define the mapping from built-in display palette colors to custom ones
const RGB paletteMapping[7] = {
    {3, 15, 0},      // Black
    {250, 250, 250}, // White
    {76, 170, 9},    // Green
    {13, 91, 201},   // Blue
    {247, 2, 43},    // Red
    {252, 252, 29},  // Yellow
    {237, 104, 2},   // Orange
};

// Struct for JPEG decoding options
struct
{
  int x, y, w, h;
} jpeg_decode_options;

// ----------------------------------------------------------------------------
// Helper functions
// ----------------------------------------------------------------------------

// Blink LEDs
void blink_leds(int times)
{
  for (int i = 0; i < times; i++)
  {
    inky.led(InkyFrame::LED_ACTIVITY, 255);
    inky.led(InkyFrame::LED_CONNECTION, 0);
    sleep_ms(500);
    inky.led(InkyFrame::LED_ACTIVITY, 0);
    inky.led(InkyFrame::LED_CONNECTION, 255);
    sleep_ms(500);
  }
  inky.led(InkyFrame::LED_ACTIVITY, 0);
  inky.led(InkyFrame::LED_CONNECTION, 0);
}

// Check if a position coordinate is in bounds
bool is_in_bounds(int x, int y, int width, int height)
{
  return x >= 0 && x < width && y >= 0 && y < height;
}

// Clamp an RGB color to the valid 0-255 range for each channel
void clamp(RGB &rgb)
{
  rgb.r = rgb.r < 0 ? 0 : rgb.r > 255 ? 255
                                      : rgb.r;
  rgb.g = rgb.g < 0 ? 0 : rgb.g > 255 ? 255
                                      : rgb.g;
  rgb.b = rgb.b < 0 ? 0 : rgb.b > 255 ? 255
                                      : rgb.b;
}

// Add two datetimes together by adding the seconds, minutes, and hours.
// Carries values over from adding seconds and minutes.
datetime_t add_datetimes(datetime_t dt1, datetime_t dt2)
{
  datetime_t result;
  int carry = 0;

  // Add seconds
  result.sec = dt1.sec + dt2.sec;
  if (result.sec >= 60)
  {
    result.sec -= 60;
    carry = 1;
  }

  // Add minutes
  result.min = dt1.min + dt2.min + carry;
  carry = 0;
  if (result.min >= 60)
  {
    result.min -= 60;
    carry = 1;
  }

  // Add hours
  result.hour = dt1.hour + dt2.hour + carry;
  carry = 0;
  if (result.hour >= 24)
  {
    result.hour -= 24;
  }
  return result;
}

// ----------------------------------------------------------------------------
// Callback functions for JPEG.open()
// ----------------------------------------------------------------------------

void *jpegdec_open_callback(const char *filename, int32_t *size)
{
  FIL *fil = new FIL;
  if (f_open(fil, filename, FA_READ))
  {
    return nullptr;
  }
  *size = f_size(fil);
  return (void *)fil;
}

void jpegdec_close_callback(void *handle)
{
  f_close((FIL *)handle);
  delete (FIL *)handle;
}

int32_t jpegdec_read_callback(JPEGFILE *jpeg, uint8_t *p, int32_t c)
{
  uint br;
  f_read((FIL *)jpeg->fHandle, p, c, &br);
  return br;
}

int32_t jpegdec_seek_callback(JPEGFILE *jpeg, int32_t p)
{
  return f_lseek((FIL *)jpeg->fHandle, p) == FR_OK ? 1 : 0;
}

int jpegdec_draw_callback(JPEGDRAW *draw)
{
  uint16_t *p = draw->pPixels;

  int xo = jpeg_decode_options.x;
  int yo = jpeg_decode_options.y;

  // Iterate for each row
  for (int y = 0; y < draw->iHeight; y++)
  {
    // Iterate for each column
    for (int x = 0; x < draw->iWidth; x++)
    {
      int sx = ((draw->x + x + xo) * jpeg_decode_options.w) / jpeg.getWidth();
      int sy = ((draw->y + y + yo) * jpeg_decode_options.h) / jpeg.getHeight();

      if (is_in_bounds(xo + sx, yo + sy, inky.bounds.w, inky.bounds.h))
      {
        // Set the pixel color
        inky.ramDisplay.write_pixel({xo + sx, yo + sy}, *p);
      }
      p++;
    }
  }
  return 1; // Continue drawing
}

// ----------------------------------------------------------------------------
// Rendering functions
// ----------------------------------------------------------------------------

// Log the PSRAM display by displaying the luminance of each pixel.
// If isPaletteIndex is true, values in PSRAM are treated as palette indices
// instead of RGB values.
void log_ram_display(bool isPaletteIndex)
{
  constexpr int ROW_INCREMENT = 20;
  constexpr int COLUMN_INCREMENT = 10;
  constexpr int LUMINANCE_SCALE = 2550;
  const char opacity[10] = {' ', '.', ':', '-', '=', '+', '*', '#', '%', '@'};

  for (int y = 0; y < inky.height; y += ROW_INCREMENT)
  {
    printf("%03d: ", y);
    uint16_t *data = new uint16_t[inky.width];
    inky.ramDisplay.read_pixel_span({0, y}, inky.width, data);

    for (int x = 0; x < inky.width; x += COLUMN_INCREMENT)
    {
      RGB color = isPaletteIndex ? paletteMapping[data[x] % 7] : RGB(static_cast<uint>(data[x]));
      int idx = color.luminance() / LUMINANCE_SCALE;
      printf("%c", opacity[idx]);
    }
    delete[] data;
    printf("\n");
  }
}

// Perform dithering on the RGB values saved in the PSRAM display by using a
// Floyd-Steinberg error-diffusion algorithm.
void error_diffuse()
{
  // Allocate memory for temp data and cache
  uint16_t data[inky.width];
  RGB errorCache[2][800];

  // Iterate for each row
  for (int y = 0; y < inky.height; y++)
  {
    // Read current row of pixels
    inky.ramDisplay.read_pixel_span({0, y}, inky.width, data);

    // Iterate for each column
    for (int x = 0; x < inky.width; x++)
    {
      // Extract original color of current pixel
      RGB originalColor = RGB(RGB565(data[x]));

      // Adjust the green channel to match the precision of the other channels
      originalColor.g = originalColor.g & 0b11111000;

      // Adjust luminosity
      float adjustment = 0.33;
      originalColor = RGB(
          originalColor.r * (1 + adjustment),
          originalColor.g * (1 + adjustment),
          originalColor.b * (1 + adjustment));

      // Add the error from previous pixels, then clamp the result
      originalColor += errorCache[0][x];
      clamp(originalColor);

      // Find the nearest color in the palette, and write the index value to PSRAM
      int nearestColorIndex = originalColor.closest(paletteMapping, 7);
      inky.ramDisplay.write_pixel({x, y}, nearestColorIndex);

      // Calculate the quantization error
      RGB nearestColor = paletteMapping[nearestColorIndex];
      RGB error = originalColor - nearestColor;

      // Distribute the error to neighboring pixels...
      for (int i = 0; i < 4; i++)
      {
        const double *distribution = dither_matrix[i];
        int dx = (int)distribution[0];
        int dy = (int)distribution[1];
        double weight = distribution[2] * GLOBAL_WEIGHT;

        // only if the neighboring pixel is within bounds
        if (is_in_bounds(x + dx, y + dy, inky.width, inky.height))
        {
          RGB distributedError = RGB(
              error.r * weight,
              error.g * weight,
              error.b * weight);
          errorCache[dy % 2][x + dx] += distributedError;
        }
      }
    }

    // Clear the error cache for the current row
    std::fill(errorCache[0], errorCache[0] + 800, RGB());

    // Swap the errorCache rows so we're using the newer one now
    std::swap(errorCache[0], errorCache[1]);
  }
}

// Decode a JPEG image and draw it on the screen.
// Perform dithering and output a preview in the console.
int draw_jpeg(std::string filename, int x, int y, int w, int h)
{
  jpeg_decode_options.x = x;
  jpeg_decode_options.y = y;
  jpeg_decode_options.w = w;
  jpeg_decode_options.h = h;

  printf("- Opening JPEG... ");
  int openReturnCode = jpeg.open(
      filename.c_str(),
      jpegdec_open_callback,
      jpegdec_close_callback,
      jpegdec_read_callback,
      jpegdec_seek_callback,
      jpegdec_draw_callback);
  if (openReturnCode == 0)
  {
    printf("Error \n");
    return 0;
  }
  printf("\n");

  jpeg.setPixelType(RGB565_BIG_ENDIAN);

  printf("- Decoding JPEG... ");
  int start = millis();
  int decodeReturnCode = jpeg.decode(0, 0, 0); // 0: error, 1: success
  if (decodeReturnCode == 0)
  {
    printf("Error \n");
    return 0;
  }
  printf("\n");

  printf("- Pre-dithering preview: \n");
  log_ram_display(false);

  error_diffuse();

  printf("- Post-dithering preview: \n");
  log_ram_display(true);

  jpeg.close();

  printf("- Done in %d ms \n", int(millis() - start));

  return 1;
}

// ----------------------------------------------------------------------------
// I/O and init functions
// ----------------------------------------------------------------------------

// Initialize Inky Frame, random number generator, and clock
void init()
{
  printf("Initializing... ");
  inky.init();
  inky.clear();
  inky.led(InkyFrame::LED_ACTIVITY, 0);
  inky.led(InkyFrame::LED_CONNECTION, 0);

  datetime_t now = {
      .year = 0, .month = 1, .day = 1, .hour = 0, .min = 0, .sec = 0};
  inky.rtc.set_datetime(&now);
  inky.rtc.clear_alarm_flag();
  inky.rtc.unset_alarm();
  inky.rtc.clear_timer_flag();
  inky.rtc.unset_timer();

  srand(get_rand_32());

  printf("Done \n");
}

// Try to mount SD card file system
int mount_sd()
{
  printf("Mounting SD card... ");
  fr = f_mount(&fs, "", 1);
  if (fr != FR_OK)
  {
    // If fail, flash 3 times and halt
    printf("Error: %d\n", fr);
    blink_leds(3);
    return 0;
  }
  printf("Done \n");
  return 1;
}

// List the contents of the SD card and return a vector of filenames with
// a specified extension
const std::vector<std::string> list_sd_files()
{
  printf("Listing SD card contents... \n");
  const std::vector<std::string> extensions = {".jpg", ".jpeg"};
  std::vector<std::string> fileNames;
  FILINFO file;
  auto dir = new DIR();

  f_opendir(dir, "/");
  while (f_readdir(dir, &file) == FR_OK && file.fname[0])
  {
    std::string fileName = file.fname;
    for (const std::string &ext : extensions)
    {
      if (fileName.size() >= ext.size() && fileName.compare(fileName.size() - ext.size(), ext.size(), ext) == 0)
      {
        printf("- %s (%lld bytes)\n", file.fname, file.fsize);
        fileNames.push_back(fileName);
        break;
      }
    }
  }

  f_closedir(dir);
  return fileNames;
}

// Main entry function
int main()
{
  stdio_init_all();
  sleep_ms(INIT_DURATION);
  init();

  if (mount_sd() == 0)
  {
    printf("Halting. \n");
    return 0;
  }

  const std::vector<std::string> fileNames = list_sd_files();
  const int maxIndex = fileNames.size() - 1;
  fileIndex = rand() % maxIndex;

  // Turn on activity LED
  if (USE_LEDS)
    inky.led(InkyFrame::LED_ACTIVITY, 255);

  // Get filename
  std::string fileName = fileNames[fileIndex];
  printf("Filename: \"%s\", Index: %d \n", fileName.c_str(), fileIndex);

  // Render image
  printf("Rendering file... \n");
  draw_jpeg(fileName, 0, 0, inky.width, inky.height);

  // Update screen
  printf("Updating screen... ");
  inky.update();
  printf("Done \n");

  // Turn off activity LED
  if (USE_LEDS)
    inky.led(InkyFrame::LED_ACTIVITY, 0);

  // Put the Inky Frame to a low-power sleep mode until next cycle
  printf("Sleeping. \n");
  if (SLEEP_TIME_IS_DURATION)
  {
    datetime_t now = inky.rtc.get_datetime();
    datetime_t nextWakeupTime = add_datetimes(now, SLEEP_TIME);
    inky.sleep_until(nextWakeupTime.sec, nextWakeupTime.min, nextWakeupTime.hour);
  }
  else
  {
    inky.sleep_until(SLEEP_TIME.sec, SLEEP_TIME.min, SLEEP_TIME.hour);
  }

  return 0;
}