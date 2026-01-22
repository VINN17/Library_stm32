#include <ST7735.h>

int16_t _width;       ///< Display width as modified by current rotation
int16_t _height;      ///< Display height as modified by current rotation
int16_t cursor_x;     ///< x location to start print()ing text
int16_t cursor_y;     ///< y location to start print()ing text
uint8_t rotation;     ///< Display rotation (0 thru 3)
uint8_t _colstart;    ///< Some displays need this changed to offset
uint8_t _rowstart;    ///< Some displays need this changed to offset
uint8_t _xstart;
uint8_t _ystart;

extern volatile uint8_t spi_dma_busy;

#define TFT_BUF_PIXELS  1024   // Sesuaikan dengan RAM yang tersedia

static uint16_t tftBuf[2][TFT_BUF_PIXELS];
static uint16_t bufIndex[2] = { 0, 0 };
static uint8_t activeBuf = 0;

// Wait untuk DMA selesai
static inline void SPI_WaitDMA(void) {
    while (spi_dma_busy);
}

// Fungsi untuk push pixel ke buffer (dengan double buffering)
static void ST7735_PushPixel(uint16_t color) {
    uint8_t drawBuf = activeBuf ^ 1;
    // Swap byte order untuk LCD (big-endian)
    tftBuf[drawBuf][bufIndex[drawBuf]++] = (color >> 8) | (color << 8);

    if (bufIndex[drawBuf] >= TFT_BUF_PIXELS) {
        ST7735_Flush();
    }
}

// Flush buffer ke display via DMA
void ST7735_Flush(void) {
    SPI_WaitDMA();  // Tunggu DMA sebelumnya selesai

    uint8_t sendBuf = activeBuf ^ 1;
    if (bufIndex[sendBuf] == 0) return;

    // Switch buffer
    activeBuf = sendBuf;
    spi_dma_busy = 1;

    HAL_GPIO_WritePin(DC_PORT, DC_PIN, GPIO_PIN_SET);
    HAL_SPI_Transmit_DMA(&hspi1, (uint8_t*)tftBuf[activeBuf], bufIndex[activeBuf] * 2);

    bufIndex[activeBuf] = 0;
}

const uint8_t init_cmds1[] = {
    15,
    ST7735_SWRESET, DELAY, 150,
    ST7735_SLPOUT, DELAY, 255,
    ST7735_FRMCTR1, 3, 0x01, 0x2C, 0x2D,
    ST7735_FRMCTR2, 3, 0x01, 0x2C, 0x2D,
    ST7735_FRMCTR3, 6, 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D,
    ST7735_INVCTR, 1, 0x07,
    ST7735_PWCTR1, 3, 0xA2, 0x02, 0x84,
    ST7735_PWCTR2, 1, 0xC5,
    ST7735_PWCTR3, 2, 0x0A, 0x00,
    ST7735_PWCTR4, 2, 0x8A, 0x2A,
    ST7735_PWCTR5, 2, 0x8A, 0xEE,
    ST7735_VMCTR1, 1, 0x0E,
    ST7735_INVOFF, 0,
    ST7735_COLMOD, 1, 0x05
};

#if (defined(ST7735_IS_128X128) || defined(ST7735_IS_160X128))
const uint8_t init_cmds2[] = {
    2,
    ST7735_CASET, 4, 0x00, 0x00, 0x00, 0x7F,
    ST7735_RASET, 4, 0x00, 0x00, 0x00, 0x7F
};
#endif

#ifdef ST7735_IS_160X80
const uint8_t init_cmds2[] = {
    3,
    ST7735_CASET, 4, 0x00, 0x00, 0x00, 0x4F,
    ST7735_RASET, 4, 0x00, 0x00, 0x00, 0x9F,
    ST7735_INVON, 0
};
#endif

const uint8_t init_cmds3[] = {
    4,
    ST7735_GMCTRP1, 16,
    0x02, 0x1c, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2d,
    0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10,
    ST7735_GMCTRN1, 16,
    0x03, 0x1d, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
    0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10,
    ST7735_NORON, DELAY, 10,
    ST7735_DISPON, DELAY, 100
};

void ST7735_Select() {
    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_RESET);
}

void ST7735_Unselect() {
    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_SET);
}

void ST7735_Reset() {
    HAL_GPIO_WritePin(RST_PORT, RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(5);
    HAL_GPIO_WritePin(RST_PORT, RST_PIN, GPIO_PIN_SET);
}

void ST7735_WriteCommand(uint8_t cmd) {
    SPI_WaitDMA();
    HAL_GPIO_WritePin(DC_PORT, DC_PIN, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&ST7735_SPI_PORT, &cmd, 1, HAL_MAX_DELAY);
}

void ST7735_WriteData(uint8_t *buff, size_t size) {
    SPI_WaitDMA();
    HAL_GPIO_WritePin(DC_PORT, DC_PIN, GPIO_PIN_SET);
    HAL_SPI_Transmit(&ST7735_SPI_PORT, buff, size, HAL_MAX_DELAY);
}

void DisplayInit(const uint8_t *addr) {
    uint8_t numCommands, numArgs;
    uint16_t ms;

    numCommands = *addr++;
    while (numCommands--) {
        uint8_t cmd = *addr++;
        ST7735_WriteCommand(cmd);

        numArgs = *addr++;
        ms = numArgs & DELAY;
        numArgs &= ~DELAY;

        if (numArgs) {
            ST7735_WriteData((uint8_t*)addr, numArgs);
            addr += numArgs;
        }

        if (ms) {
            ms = *addr++;
            if (ms == 255) ms = 500;
            HAL_Delay(ms);
        }
    }
}

void ST7735_SetAddressWindow(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {
    ST7735_WriteCommand(ST7735_CASET);
    uint8_t data[] = { 0x00, x0 + _xstart, 0x00, x1 + _xstart };
    ST7735_WriteData(data, sizeof(data));

    ST7735_WriteCommand(ST7735_RASET);
    data[1] = y0 + _ystart;
    data[3] = y1 + _ystart;
    ST7735_WriteData(data, sizeof(data));

    ST7735_WriteCommand(ST7735_RAMWR);
}

void ST7735_Init(uint8_t rot) {
    ST7735_Select();
    ST7735_Reset();
    DisplayInit(init_cmds1);
    DisplayInit(init_cmds2);
    DisplayInit(init_cmds3);

#if ST7735_IS_160X80
    _colstart = 24;
    _rowstart = 0;
    uint8_t data = 0xC0;
    ST7735_WriteCommand(ST7735_MADCTL);
    ST7735_WriteData(&data, 1);
#elif ST7735_IS_128X128
    _colstart = 2;
    _rowstart = 3;
#else
    _colstart = 0;
    _rowstart = 0;
#endif

    ST7735_SetRotation(rot);
    ST7735_Unselect();
}

void ST7735_SetRotation(uint8_t m) {
    uint8_t madctl = 0;
    rotation = m % 4;

    switch (rotation) {
    case 0:
#if ST7735_IS_160X80
        madctl = ST7735_MADCTL_MX | ST7735_MADCTL_MY | ST7735_MADCTL_BGR;
#else
        madctl = ST7735_MADCTL_MX | ST7735_MADCTL_MY | ST7735_MADCTL_RGB;
        _height = ST7735_HEIGHT;
        _width = ST7735_WIDTH;
        _xstart = _colstart;
        _ystart = _rowstart;
#endif
        break;
    case 1:
#if ST7735_IS_160X80
        madctl = ST7735_MADCTL_MY | ST7735_MADCTL_MV | ST7735_MADCTL_BGR;
#else
        madctl = ST7735_MADCTL_MY | ST7735_MADCTL_MV | ST7735_MADCTL_RGB;
        _width = ST7735_HEIGHT;
        _height = ST7735_WIDTH;
        _ystart = _colstart;
        _xstart = _rowstart;
#endif
        break;
    case 2:
#if ST7735_IS_160X80
        madctl = ST7735_MADCTL_BGR;
#else
        madctl = ST7735_MADCTL_RGB;
        _height = ST7735_HEIGHT;
        _width = ST7735_WIDTH;
        _xstart = _colstart;
        _ystart = _rowstart;
#endif
        break;
    case 3:
#if ST7735_IS_160X80
        madctl = ST7735_MADCTL_MX | ST7735_MADCTL_MV | ST7735_MADCTL_BGR;
#else
        madctl = ST7735_MADCTL_MX | ST7735_MADCTL_MV | ST7735_MADCTL_RGB;
        _width = ST7735_HEIGHT;
        _height = ST7735_WIDTH;
        _ystart = _colstart;
        _xstart = _rowstart;
#endif
        break;
    }

    ST7735_Select();
    ST7735_WriteCommand(ST7735_MADCTL);
    ST7735_WriteData(&madctl, 1);
    ST7735_Unselect();
}

void ST7735_DrawPixel(uint16_t x, uint16_t y, uint16_t color) {
    if ((x >= _width) || (y >= _height)) return;

    ST7735_Select();
    ST7735_SetAddressWindow(x, y, x, y);
    ST7735_PushPixel(color);
    ST7735_Flush();
    ST7735_Unselect();
}

void ST7735_WriteChar(uint16_t x, uint16_t y, char ch, FontDef font,
                      uint16_t color, uint16_t bgcolor) {
    uint32_t i, b, j;

    ST7735_SetAddressWindow(x, y, x + font.width - 1, y + font.height - 1);

    for (i = 0; i < font.height; i++) {
        b = font.data[(ch - 32) * font.height + i];
        for (j = 0; j < font.width; j++) {
            if ((b << j) & 0x8000) {
                ST7735_PushPixel(color);
            } else {
                ST7735_PushPixel(bgcolor);
            }
        }
    }
}

void ST7735_WriteString(uint16_t x, uint16_t y, const char *str, FontDef font,
                        uint16_t color, uint16_t bgcolor) {
    ST7735_Select();

    while (*str) {
        if (x + font.width >= _width) {
            x = 0;
            y += font.height;
            if (y + font.height >= _height) break;
            if (*str == ' ') {
                str++;
                continue;
            }
        }

        ST7735_WriteChar(x, y, *str, font, color, bgcolor);
        x += font.width;
        str++;
    }

    ST7735_Flush();
    ST7735_Unselect();
}

void ST7735_FillRectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                          uint16_t color) {
    if ((x >= _width) || (y >= _height)) return;
    if ((x + w) > _width) w = _width - x;
    if ((y + h) > _height) h = _height - y;

    ST7735_Select();
    ST7735_SetAddressWindow(x, y, x + w - 1, y + h - 1);

    uint32_t pixels = (uint32_t)w * h;
    for (uint32_t i = 0; i < pixels; i++) {
        ST7735_PushPixel(color);
    }

    ST7735_Flush();
    ST7735_Unselect();
}

void ST7735_FillScreen(uint16_t color) {
    ST7735_FillRectangle(0, 0, _width, _height, color);
}

void ST7735_DrawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      const uint16_t *img) {
    if ((x >= _width) || (y >= _height)) return;
    if ((x + w) > _width || (y + h) > _height) return;

    ST7735_Select();
    ST7735_SetAddressWindow(x, y, x + w - 1, y + h - 1);

    // Untuk image, perlu swap byte order juga
    uint32_t totalPixels = w * h;
    static uint16_t imgBuf[128]; // Buffer untuk swap

    SPI_WaitDMA();
    HAL_GPIO_WritePin(DC_PORT, DC_PIN, GPIO_PIN_SET);

    for (uint32_t i = 0; i < totalPixels; i += 128) {
        uint16_t chunk = (totalPixels - i > 128) ? 128 : (totalPixels - i);

        // Swap byte order
        for (uint16_t j = 0; j < chunk; j++) {
            uint16_t pixel = img[i + j];
            imgBuf[j] = (pixel >> 8) | (pixel << 8);
        }

        spi_dma_busy = 1;
        HAL_SPI_Transmit_DMA(&hspi1, (uint8_t*)imgBuf, chunk * 2);
        SPI_WaitDMA();
    }

    ST7735_Unselect();
}

void ST7735_InvertColors(bool invert) {
    ST7735_Select();
    ST7735_WriteCommand(invert ? ST7735_INVON : ST7735_INVOFF);
    ST7735_Unselect();
}

// Tambahan: fungsi untuk menggambar garis horizontal (optimized)
void ST7735_DrawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color) {
    if (x >= _width || y >= _height) return;
    if (x + w > _width) w = _width - x;

    ST7735_Select();
    ST7735_SetAddressWindow(x, y, x + w - 1, y);

    for (uint16_t i = 0; i < w; i++) {
        ST7735_PushPixel(color);
    }

    ST7735_Flush();
    ST7735_Unselect();
}

// Tambahan: fungsi untuk menggambar garis vertikal (optimized)
void ST7735_DrawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color) {
    if (x >= _width || y >= _height) return;
    if (y + h > _height) h = _height - y;

    ST7735_Select();
    ST7735_SetAddressWindow(x, y, x, y + h - 1);

    for (uint16_t i = 0; i < h; i++) {
        ST7735_PushPixel(color);
    }

    ST7735_Flush();
    ST7735_Unselect();
}

// Helper: Convert RGB888 to RGB565 dengan byte swap
uint16_t ST7735_Color565(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    // Tidak perlu swap di sini, nanti di PushPixel
    return color;
}
