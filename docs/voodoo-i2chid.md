# VoodooI2CHID.kext 1.0.2

[简体中文](#简体中文) · [English](#english)

## 简体中文

适配本机 I²C HID 触控板并过滤同一次采样的重复报告。使用 VoodooI2C 2.9.1、原版 VoodooI2CServices，以及实机的 I²C、GPIO 和 [VoodooInput](voodoo-input.md) 配置。

完整顺序见 [I2C 触控板注入顺序](opencore.md#i2c-触控板注入顺序)。

### 安装

先按 [OpenCore 通用步骤](opencore.md)备份 EFI。

1. 从 [Kexts](../Kexts/VoodooI2CHID.kext) 复制整个 `VoodooI2CHID.kext` 到 `EFI/OC/Kexts/`。若同时存在旧的嵌套版或独立版，只启用本条目。
2. 按下表更新 `Kernel → Add`，排在 VoodooI2C 和 VoodooInput 后面。

| 字段 | 值 |
| --- | --- |
| `BundlePath` | `VoodooI2CHID.kext` |
| `ExecutablePath` | `Contents/MacOS/VoodooI2CHID` |
| `PlistPath` | `Contents/Info.plist` |
| `Enabled` | `true` |
| `Arch` | `x86_64` |
| `MinKernel` | `24.0.0` |
| `MaxKernel` | `24.99.99` |

### 验证

保存配置并[校验后重启](opencore.md#校验与回退)，在终端运行：

```sh
ioreg -r -c VoodooI2CPrecisionTouchpadHIDEventDriver -d 1
```

确认精确触控板驱动已匹配，测试指针、轻点与双指滚动。如果无匹配，先检查 I²C 控制器和 `PNP0C50` 触控板是否已经枚举。

## English

Supports this model’s I²C HID trackpad and filters duplicate reports for a sample. Use VoodooI2C 2.9.1, the unmodified VoodooI2CServices, and the corresponding I²C, GPIO, and [VoodooInput](voodoo-input.md#english) setup.

See the [I2C trackpad injection order](opencore.md#i2c-trackpad-injection-order).

### Install

[Back up the active EFI](opencore.md#english) before making changes.

1. Copy the entire [VoodooI2CHID.kext](../Kexts/VoodooI2CHID.kext) bundle to `EFI/OC/Kexts/`. If an older standalone or nested copy is present, enable only this entry.
2. Use the table below to update `Kernel → Add` and place the entry after VoodooI2C and VoodooInput.

| Field | Value |
| --- | --- |
| `BundlePath` | `VoodooI2CHID.kext` |
| `ExecutablePath` | `Contents/MacOS/VoodooI2CHID` |
| `PlistPath` | `Contents/Info.plist` |
| `Enabled` | `true` |
| `Arch` | `x86_64` |
| `MinKernel` | `24.0.0` |
| `MaxKernel` | `24.99.99` |

### Verify

Save the configuration, [validate it, and reboot](opencore.md#validation-and-rollback). Then run the following in Terminal.

```sh
ioreg -r -c VoodooI2CPrecisionTouchpadHIDEventDriver -d 1
```

Confirm that the precision trackpad driver matched, then test pointer movement, tapping, and two-finger scrolling. If it did not match, check whether the I²C controller and `PNP0C50` trackpad were enumerated.
