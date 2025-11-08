class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Light_PWM _light_instance;

public:
  LGFX(void) {
    {  // ---- SPI bus config ----
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;  // ESP32-C3 uses FSPI (SPI2_HOST)
      cfg.spi_mode = 3;
      cfg.freq_write = 40000000;  // matches your TFT_eSPI SPI_FREQUENCY
      cfg.freq_read = 20000000;   // matches your TFT_eSPI SPI_READ_FREQUENCY
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;

      cfg.pin_sclk = 4;   // TFT_SCLK
      cfg.pin_mosi = 2;   // TFT_MOSI
      cfg.pin_miso = -1;  // no MISO on ST7789
      cfg.pin_dc = 1;     // TFT_DC

      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {  // ---- Panel config ----
      auto cfg = _panel_instance.config();
      cfg.pin_cs = -1;  // no CS defined in TFT_eSPI setup
      cfg.pin_rst = 3;  // TFT_RST
      cfg.pin_busy = -1;
      //quirk of this display: It has to be set to 240x320, but only 240x280 are actually visible
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 20;
      cfg.offset_rotation = 0;
      cfg.invert = true;  // ST7789 usually needs this
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;

      _panel_instance.config(cfg);
    }
    {  // ---- Backlight (if you have one) ----
      auto cfg = _light_instance.config();
      cfg.pin_bl = 0;  // backlight pin
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 0;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }
    setPanel(&_panel_instance);
  }
};

LGFX tft;
