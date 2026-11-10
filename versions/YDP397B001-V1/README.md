<p align="left"><img alt="OSPTEK" src="../../images/logo.png" width="200" /></p>

<h1 align="center">OSPTEK 3.97″ TFT 显示屏 480×800（YDP397B001-V1）</h1>

<p align="center"><b>TFT 模组 · MIPI · GC9503CV</b></p>

<p align="center"><a href="./README_EN.md">English</a> | 简体中文 · <a href="../../README.md">规格族索引</a></p>

<p align="center">
  <img alt="Interface: MIPI" src="https://img.shields.io/badge/Interface-MIPI-27AE60?style=flat-square" />
  <img alt="Driver: GC9503CV" src="https://img.shields.io/badge/Driver-GC9503CV-E7352C?style=flat-square" />
</p>

## 目录

- [规格参数](#规格参数)
- [相关资料](#相关资料)
- [仓库结构](#仓库结构)
- [购买链接](#购买链接)
- [技术支持](#技术支持)

---

## 规格参数

| 项目 | 规格 |
| ---- | ---- |
| 尺寸 | 3.97 英寸 |
| 类型 | TFT |
| 分辨率 | 480×800 |
| 外形尺寸 | 55.44 x 96.17 x 2.1 mm |
| 有效显示区域 | 51.84 x 86.40 mm |
| 接口 | MIPI |
| 驱动 IC | GC9503CV |
| 触摸驱动 | — |

> 完整外形尺寸、FPC 定义、供电与时序以产品规格书 / 驱动手册为准。

## 相关资料

| 分类 | 文件 |
| ---- | ---- |
| 规格文档 | [`GC9503CV DataSheet V1.0(1).pdf`](./datasheets/GC9503CV%20DataSheet%20V1.0%281%29.pdf) |
| 规格文档 | [`YDP397B001-V1.pdf`](./datasheets/YDP397B001-V1.pdf) |
| 初始化代码 | [`GC9503V_BOE3.97_BV040WVQ-N80_20200513_AN.txt`](./code/GC9503V_BOE3.97_BV040WVQ-N80_20200513_AN.txt) |

## 仓库结构

```text
display-tft-3.97inch-480x800/ # 仓库根（导航见 ../../README.md，示例工程见根目录 examples/）
└── versions/
    └── YDP397B001-V1/        # 本料号完整资料
        ├── README.md
        ├── README_EN.md
        ├── osptek.yml
        ├── images/
        ├── datasheets/       # 产品规格书 + 驱动/触摸 IC 数据手册
        └── code/             # 初始化参考代码
```

## 购买链接

见[仓库首页](../../README.md#购买链接)。

## 技术支持

见[仓库首页](../../README.md#技术支持)。

---

<p align="center"><sub>© 2026 OSPTEK 鱼鹰光电 · 本仓库资料采用 CC BY 4.0 许可</sub></p>
