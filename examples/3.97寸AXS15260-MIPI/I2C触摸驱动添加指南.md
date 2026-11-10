# 第一步：将驱动集成到内核源码树

## 1_**放置源码**：

将 `axs15260_touch.c` 复制到 SDK 触摸驱动目录：

```
cp axs15260_touch.c /home/jin/rk3506-work/rk3506_linux6.1_sdk_v1.2.0_evb/kernel-6.1/drivers/input/touchscreen/
```

## 2_**修改目录下的 `Makefile`**：

打开 `/home/jin/rk3506-work/rk3506_linux6.1_sdk_v1.2.0_evb/kernel-6.1/drivers/input/touchscreen/Makefile`，在末尾追加：

```
nano /home/jin/rk3506-work/rk3506_linux6.1_sdk_v1.2.0_evb/kernel-6.1/drivers/input/touchscreen/Makefile
```

```
obj-$(CONFIG_TOUCHSCREEN_AXS15260) += axs15260_touch.o
```

## 3_**修改目录下的 `Kconfig`**：

打开 `/home/jin/rk3506-work/rk3506_linux6.1_sdk_v1.2.0_evb/kernel-6.1/drivers/input/touchscreen/Kconfig`，在末尾添加：

```
nano /home/jin/rk3506-work/rk3506_linux6.1_sdk_v1.2.0_evb/kernel-6.1/drivers/input/touchscreen/Kconfig
```

```
config TOUCHSCREEN_AXS15260
	tristate "AXS15260 touchscreen support"
	depends on I2C
	help
	  Say Y here if you have AXS15260 I2C touchscreen controller.

	  To compile this driver as a module, choose M here: the
	  module will be called axs15260.
```

## 4_在 Kernel Config 中使能该驱动

在 SDK 根目录下执行内核菜单配置：

```
cd /home/jin/rk3506-work/rk3506_linux6.1_sdk_v1.2.0_evb/
./build.sh kernel-config
```

在按 `/` 搜索 `TOUCHSCREEN_AXS15260`，按 1 跳转到对应节点，按 M 键将其选择为模块编译（显示为 `<M>`），然后保存并退出。

# 第二步：修改设备树 DTS 节点

打开 `vanxoak-hd-rk3506-evb-mipi-480x800.dtsi`，找到 `&i2c2` 节点

```
&i2c2 {
	status = "okay";
	pinctrl-names = "default";
	pinctrl-0 = <&rm_io1_i2c2_scl &rm_io0_i2c2_sda>;

    axs15260: axs15260@3b {
		status = "okay";
		compatible = "axs,axs15260-touch";  /* 匹配 axs15260_of_match */
		reg = <0x3b>;                     /* I2C 设备地址 */
		pinctrl-names = "default";

		touchscreen-size-x = <480>;       /* 匹配 3.97 寸 AXS15260 分辨率 */
		touchscreen-size-y = <800>;

		/* 坐标翻转/镜像（按需开启） */
		// touchscreen-inverted-x;
		// touchscreen-inverted-y;
		// touchscreen-swapped-x-y;
	};
};
```

# 第三步：重新编译内核与固件

在 SDK 根目录下重新编译：

```
cd /home/jin/rk3506-work/rk3506_linux6.1_sdk_v1.2.0_evb/
./build.sh 
```

# 第四步：检查触摸坐标

**查看触摸屏对应的设备节点**：

```
cat /proc/bus/input/devices | grep -A 5 -i "axs15260"
```

查看输出中的 `Handlers=eventX`（例如 `event0 或 `event1`）。

**运行 evtest**：

```
evtest /dev/input/event0
```

**测试坐标**： 用手指触摸屏幕，终端会实时刷出类似如下的坐标数据：

```
Event: time 1698234567.123456, type 3 (EV_ABS), code 53 (ABS_MT_POSITION_X), value 240
Event: time 1698234567.123456, type 3 (EV_ABS), code 54 (ABS_MT_POSITION_Y), value 380
Event: time 1698234567.123456, type 1 (EV_KEY), code 330 (BTN_TOUCH), value 1
```

`ABS_MT_POSITION_X` 的 `value` 即为当前 X 轴坐标（0 ~ 479）。

`ABS_MT_POSITION_Y` 的 `value` 即为当前 Y 轴坐标（0 ~ 799）。