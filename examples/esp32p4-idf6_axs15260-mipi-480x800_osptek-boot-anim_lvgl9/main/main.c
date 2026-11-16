#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "driver/gpio.h"
#include "esp_ldo_regulator.h"
#include "esp_lv_adapter.h"
#include "lv_demos.h"

#include "esp_lcd_axs15260.h"
#include "axs15260_init_cmds.h"

/* EFC 相关 */
#include "lv_efc.h"
#include "esp_lv_fs.h"
#include "esp_mmap_assets.h"
#include "mmap_generate_efc.h"

/* LCD size */
#define EXAMPLE_LCD_H_RES   (480)
#define EXAMPLE_LCD_V_RES   (800)

#if LV_COLOR_DEPTH == 16
#define MIPI_DPI_PX_FORMAT (LCD_COLOR_FMT_RGB565)
#define BSP_LCD_COLOR_DEPTH (16)
#elif LV_COLOR_DEPTH == 24
#define MIPI_DPI_PX_FORMAT (LCD_COLOR_FMT_RGB888)
#define BSP_LCD_COLOR_DEPTH (24)
#endif

/* “VDD_MIPI_DPHY”应供电 2.5V，可从内部 LDO 稳压器或外部 LDO 芯片获取电源 */
#define EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN 3 // LDO_VO3 连接至 VDD_MIPI_DPHY
#define EXAMPLE_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL 1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_BK_LIGHT -1
#define EXAMPLE_PIN_NUM_LCD_RST  -1

/* Touch settings */
#define EXAMPLE_TOUCH_I2C_NUM       (0)
#define EXAMPLE_TOUCH_I2C_CLK_HZ    (400000)

/* LCD touch pins */
#define EXAMPLE_TOUCH_I2C_SCL       (GPIO_NUM_8)
#define EXAMPLE_TOUCH_I2C_SDA       (GPIO_NUM_7)
#define EXAMPLE_TOUCH_RST           (GPIO_NUM_NC)
#define EXAMPLE_TOUCH_INT           (GPIO_NUM_NC)

/* 旋转与防撕裂（MIPI 默认 TRIPLE_PARTIAL） */
#define EXAMPLE_LV_ROTATION         ESP_LV_ADAPTER_ROTATE_0
#define EXAMPLE_TEAR_AVOID_MODE     ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_MIPI_DSI

static const char *TAG = "EXAMPLE";

/* LCD IO and panel */
static esp_lcd_panel_handle_t lcd_panel = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;

/* LVGL display and touch */
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_indev = NULL;

/* EFC player context */
static mmap_assets_handle_t s_assets;
static esp_lv_fs_handle_t s_fs_handle;
static lv_obj_t *s_efc_anim;
static lv_timer_t *s_finish_check_timer = NULL;

// 挂载 EFC 资产分区并注册 LVGL 文件系统
static esp_err_t mount_efc_assets(void)
{
    mmap_assets_config_t cfg = {
        .partition_label = "efc",
        .max_files = MMAP_EFC_FILES,
        .checksum = MMAP_EFC_CHECKSUM,
        .flags = { .mmap_enable = 1 },
    };

    ESP_RETURN_ON_ERROR(mmap_assets_new(&cfg, &s_assets), TAG, "mmap init failed");

    size_t file_count = mmap_assets_get_stored_files(s_assets);
    if (file_count == 0) {
        mmap_assets_del(s_assets);
        return ESP_ERR_NOT_FOUND;
    }

    fs_cfg_t fs_cfg = {
        .fs_letter = 'E',
        .fs_nums = file_count,
        .fs_assets = s_assets,
    };

    ESP_RETURN_ON_ERROR(esp_lv_adapter_fs_mount(&fs_cfg, &s_fs_handle), TAG, "fs mount failed");
    ESP_LOGI(TAG, "EFC assets mounted on drive E:");

    return ESP_OK;
}

// EFC 播放完成检查回调
static void efc_finish_check_cb(lv_timer_t *timer)
{
    if (!timer) {
        return;
    }

    lv_obj_t *efc_anim = (lv_obj_t *)lv_timer_get_user_data(timer);
    if (!efc_anim || !lv_efc_is_loaded(efc_anim)) {
        return;
    }

    int32_t current = lv_efc_get_current_frame(efc_anim);
    int32_t total = lv_efc_get_total_frames(efc_anim);
    bool loop_enabled = lv_efc_get_loop_enabled(efc_anim);

    // 不循环且已到最后一帧 → 切换到 widgets demo
    if (!loop_enabled && (current >= total - 1 || current < 0)) {
        ESP_LOGI(TAG, "EFC播放完成，启动 lv_demo_widgets");

        lv_timer_delete(timer);
        s_finish_check_timer = NULL;

        if (s_efc_anim) {
            lv_obj_delete(s_efc_anim);
            s_efc_anim = NULL;
        }

        lv_demo_widgets();
    }
}

/**
 * @brief 初始化并启动 EFC 开机动画
 */
static void start_efc_player(lv_display_t *disp)
{
    if (esp_lv_adapter_lock(-1) != ESP_OK) {
        return;
    }

    lv_obj_t *screen = lv_disp_get_scr_act(disp);

    s_efc_anim = lv_efc_create(screen);
    lv_obj_align(s_efc_anim, LV_ALIGN_CENTER, 0, 0);

    const char *file_name = mmap_assets_get_name(s_assets, 0);
    if (file_name) {
        char path[64];
        snprintf(path, sizeof(path), "E:%s", file_name);

        lv_efc_set_src(s_efc_anim, path);

        if (lv_efc_is_loaded(s_efc_anim)) {
            lv_efc_set_frame_delay(s_efc_anim, 20);
            lv_efc_set_loop_enabled(s_efc_anim, false);  // 只播一次

            s_finish_check_timer = lv_timer_create(efc_finish_check_cb, 100, s_efc_anim);
        } else {
            ESP_LOGW(TAG, "EFC load failed");
        }
    } else {
        ESP_LOGW(TAG, "No EFC file found");
    }

    esp_lv_adapter_unlock();
}

/* 独立任务：避免 EFC 解码占用 main 任务栈 */
static void efc_player_task(void *arg)
{
    lv_display_t *disp = (lv_display_t *)arg;
    start_efc_player(disp);
    vTaskDelete(NULL);
}

static void example_bsp_enable_dsi_phy_power(void)
{
    // 打开 MIPI DSI PHY 的电源，使其从“无电”状态进入“关机”状态
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
#ifdef EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = EXAMPLE_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy));
    ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
#endif
}

static void example_bsp_init_lcd_backlight(void)
{
#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT};
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
#endif
}

static void example_bsp_set_lcd_backlight(uint32_t level)
{
#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, level);
#endif
}

static esp_err_t app_lcd_init(void)
{
    esp_err_t ret = ESP_OK;

    example_bsp_enable_dsi_phy_power();
    example_bsp_init_lcd_backlight();
    example_bsp_set_lcd_backlight(EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL);

    // 根据防撕裂模式与旋转计算所需帧缓冲数量
    uint8_t num_fbs = esp_lv_adapter_get_required_frame_buffer_count(
        EXAMPLE_TEAR_AVOID_MODE, EXAMPLE_LV_ROTATION);

    // 首先创建 MIPI DSI 总线，它还将初始化 DSI PHY
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = 0,
        .lane_bit_rate_mbps = 740,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus), err, TAG, "LCD init failed");

    ESP_LOGI(TAG, "Install MIPI DSI LCD control panel");
    // 我们使用DBI接口发送LCD命令和参数
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,                
        .lcd_cmd_bits = 8,                   
        .lcd_param_bits = 8,                 
    };

    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io_handle), err, TAG, "LCD init failed");

    // 创建AXS15260控制面板
    esp_lcd_dpi_panel_config_t dpi_config = {                 \
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,          \
        .dpi_clock_freq_mhz = 35.7,                           \
        .virtual_channel = 0,                                 \
        .in_color_format = MIPI_DPI_PX_FORMAT,                \
        .num_fbs = num_fbs,                                   \
        .video_timing = {                                     \
            .h_size = EXAMPLE_LCD_H_RES,                      \
            .v_size = EXAMPLE_LCD_V_RES,                      \
            .hsync_back_porch = 70,                           \
            .hsync_pulse_width = 10,                          \
            .hsync_front_porch = 70,                          \
            .vsync_back_porch = 20,                           \
            .vsync_pulse_width = 2,                           \
            .vsync_front_porch = 125,                         \
        },                                                    \
    };

    axs15260_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = lcd_init_cmds_size,
        .flags.use_mipi_interface = 1,
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = BSP_LCD_COLOR_DEPTH,
        .vendor_config = &vendor_config,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_axs15260(io_handle, &panel_config, &lcd_panel), err, TAG, "LCD init failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(lcd_panel), err, TAG, "LCD init failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(lcd_panel), err, TAG, "LCD init failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_disp_on_off(lcd_panel, true), err, TAG, "LCD init failed");

    // 打开背光
    example_bsp_set_lcd_backlight(EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);

    return ret;

err:
    if (lcd_panel) {
        esp_lcd_panel_del(lcd_panel);
        lcd_panel = NULL;
    }
    return ret;
}

static esp_err_t app_touch_init(void)
{
    /* Initialize I2C */
    const i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = EXAMPLE_TOUCH_I2C_NUM,
        .scl_io_num = EXAMPLE_TOUCH_I2C_SCL,
        .sda_io_num = EXAMPLE_TOUCH_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t i2c_bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &i2c_bus));

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_AXS15260_CONFIG();
    tp_io_config.scl_speed_hz = EXAMPLE_TOUCH_I2C_CLK_HZ;

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_config, &tp_io_handle));

    /* Initialize touch HW */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = EXAMPLE_TOUCH_RST,
        .int_gpio_num = EXAMPLE_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    return esp_lcd_touch_new_i2c_axs15260(tp_io_handle, &tp_cfg, &touch_handle);
}

static esp_err_t app_lvgl_init(void)
{
    /* 初始化 esp_lvgl_adapter */
    esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    // 如需更大栈或把栈放 PSRAM，可在此修改，例如：
    // adapter_cfg.task_stack_size = 16 * 1024;
    // adapter_cfg.stack_in_psram = true;
    ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&adapter_cfg), TAG, "esp_lv_adapter_init failed");

    /* 注册 MIPI DSI 显示 */
    esp_lv_adapter_display_config_t disp_cfg = ESP_LV_ADAPTER_DISPLAY_MIPI_DEFAULT_CONFIG(
        lcd_panel,
        io_handle,
        EXAMPLE_LCD_H_RES,
        EXAMPLE_LCD_V_RES,
        EXAMPLE_LV_ROTATION
    );
    // 如需自定义 buffer_height / use_psram 等，可在宏展开后修改：
    // disp_cfg.profile.buffer_height = 100;
    // disp_cfg.profile.use_psram = true;
    // disp_cfg.tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL; // 与 EXAMPLE_TEAR_AVOID_MODE 保持一致

    lvgl_disp = esp_lv_adapter_register_display(&disp_cfg);
    if (lvgl_disp == NULL) {
        ESP_LOGE(TAG, "esp_lv_adapter_register_display failed");
        return ESP_FAIL;
    }

    lv_obj_t *default_scr = lv_disp_get_scr_act(lvgl_disp);
    if (default_scr) {
        lv_obj_remove_style_all(default_scr);
        lv_obj_set_style_bg_color(default_scr, lv_color_black(), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(default_scr, LV_OPA_COVER, LV_STATE_DEFAULT);
        lv_obj_set_size(default_scr, LV_PCT(100), LV_PCT(100));
    }

    /* 注册触摸 */
    if (touch_handle) {
        esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(lvgl_disp, touch_handle);
        lvgl_touch_indev = esp_lv_adapter_register_touch(&touch_cfg);
        if (lvgl_touch_indev == NULL) {
            ESP_LOGW(TAG, "esp_lv_adapter_register_touch failed");
        }
    }

    /* 启动 adapter 任务 */
    ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), TAG, "esp_lv_adapter_start failed");

    return ESP_OK;
}

void app_main(void)
{
    /* LCD HW initialization */
    ESP_ERROR_CHECK(app_lcd_init());

    /* Touch initialization */
    ESP_ERROR_CHECK(app_touch_init());

    /* LVGL / adapter initialization */
    ESP_ERROR_CHECK(app_lvgl_init());

    // 挂载 EFC 资产，并在独立任务中启动开机动画
    ESP_ERROR_CHECK(mount_efc_assets());
    xTaskCreatePinnedToCore(efc_player_task, "efc_player", 16384, lvgl_disp, 5, NULL, 0);

    ESP_LOGI(TAG, "系统运行中，等待 EFC 播放完成后进入 widgets demo...");
}