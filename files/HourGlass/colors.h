// colors.h

// ===== COLOR PALETTE =====
// A list of named colors to choose from in the web UI.
// The index (0, 1, 2...) is what we save in EEPROM.
// These are raw 16-bit RGB565 values.
#define COLOR_PALETTE_COUNT 14
const uint16_t colorPalette[COLOR_PALETTE_COUNT] = {
  0x0000,      // 0: BLACK
  0xFFFF,      // 1: WHITE
  0xF800,      // 2: RED
  0x07E0,      // 3: GREEN
  0x001F,      // 4: BLUE
  0xFFE0,      // 5: YELLOW
  0x07FF,      // 6: CYAN
  0xF81F,      // 7: MAGENTA
  0xFC00,      // 8: ORANGE
  0x780F,      // 9: PURPLE
  0xd94f,      // 10: PINK
  0x04FF,      // 11: TURQUOISE
  0xbd2e,      // 12: SAND
  0x8410       // 13: GREY
};

// ===== COLOR NAMES FOR WEB UI =====
// These must match the palette above in order.
const char* colorNames[COLOR_PALETTE_COUNT] = {
  "Black", "White", "Red", "Green", "Blue", "Yellow", "Cyan",
  "Magenta", "Orange", "Purple", "Pink", "Turquoise", "Sand", "Grey"
};