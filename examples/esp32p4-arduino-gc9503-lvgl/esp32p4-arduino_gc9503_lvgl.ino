#pragma GCC push_options
#pragma GCC optimize("O3")

#include <Arduino.h>
#include "lvgl.h"
#include "demos/lv_demos.h"
#include "pins_config.h"
#include "src/lcd/gc9503_lcd.h"

gc9503_lcd lcd = gc9503_lcd(LCD_RST);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf;
static lv_color_t *buf1;

// 显示刷新
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  const int offsetx1 = area->x1;
  const int offsetx2 = area->x2;
  const int offsety1 = area->y1;
  const int offsety2 = area->y2;
  lcd.lcd_draw_bitmap(offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, &color_p->full);
  lv_disp_flush_ready(disp); // 告诉lvgl刷新完成
}

void setup()
{
  lcd.begin();

  lv_init();
  size_t buffer_size = sizeof(lv_color_t) * LCD_H_RES * LCD_V_RES;
  buf = (lv_color_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
  buf1 = (lv_color_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
  assert(buf);
  assert(buf1);

  lv_disp_draw_buf_init(&draw_buf, buf, buf1, LCD_H_RES * LCD_V_RES);

  static lv_disp_drv_t disp_drv;
  /*Initialize the display*/
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = LCD_H_RES;
  disp_drv.ver_res = LCD_V_RES;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  disp_drv.full_refresh = true;
  lv_disp_drv_register(&disp_drv);

  // lv_demo_widgets(); /* 小部件示例 */
  // lv_demo_music();        /* 类似智能手机的现代音乐播放器演示 */
  // lv_demo_stress();       /* LVGL 压力测试 */
  lv_demo_benchmark();    /* 用于测量 LVGL 性能或比较不同设置的演示 */
}

void loop()
{
  lv_timer_handler();
  delay(5);
}
