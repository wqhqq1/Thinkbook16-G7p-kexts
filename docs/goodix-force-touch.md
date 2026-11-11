# GoodixForceTouch.kext 1.0.0

[简体中文](#简体中文) · [English](#english)

## 简体中文

适用于 Goodix `27C6:01E9` 触控板。先确认 VoodooI2C 2.9.1 已能识别触控板，并安装本仓库的 [VoodooInput](voodoo-input.md) 和 [VoodooI2CHID](voodoo-i2chid.md)。

完整顺序见 [I2C 触控板注入顺序](opencore.md#i2c-触控板注入顺序)。

### 安装

先按 [OpenCore 通用步骤](opencore.md)备份 EFI。

1. 从 [Kexts](../Kexts/GoodixForceTouch.kext) 复制整个 `GoodixForceTouch.kext` 到 `EFI/OC/Kexts/`。
2. 按下表更新 `Kernel → Add`。实机将本插件排在 `VoodooI2C.kext` 后面、`VoodooI2CHID.kext` 前面。

| 字段 | 值 |
| --- | --- |
| `BundlePath` | `GoodixForceTouch.kext` |
| `ExecutablePath` | `Contents/MacOS/GoodixForceTouch` |
| `PlistPath` | `Contents/Info.plist` |
| `Enabled` | `true` |
| `Arch` | `x86_64` |
| `MinKernel` | `24.0.0` |
| `MaxKernel` | `24.99.99` |

### 验证

保存配置并[校验后重启](opencore.md#校验与回退)，在终端运行：

```sh
ioreg -r -c GoodixForceTouchEngine -l | grep PressureMapping
```

确认匹配到 `GoodixForceTouchEngine`，并在「系统设置 → 触控板」打开用力点按。先试普通点按，再在支持该手势的应用里用力点按。没有匹配时先检查触控板的 Vendor ID 和 Product ID。

## English

For the Goodix `27C6:01E9` trackpad. First confirm that VoodooI2C 2.9.1 detects the trackpad and install this repository’s [VoodooInput](voodoo-input.md#english) and [VoodooI2CHID](voodoo-i2chid.md#english).

See the [I2C trackpad injection order](opencore.md#i2c-trackpad-injection-order).

### Install

[Back up the active EFI](opencore.md#english) before making changes.

1. Copy the entire [GoodixForceTouch.kext](../Kexts/GoodixForceTouch.kext) bundle to `EFI/OC/Kexts/`.
2. Use the table below to update `Kernel → Add`. The tested configuration places this plug-in after `VoodooI2C.kext` and before `VoodooI2CHID.kext`.

| Field | Value |
| --- | --- |
| `BundlePath` | `GoodixForceTouch.kext` |
| `ExecutablePath` | `Contents/MacOS/GoodixForceTouch` |
| `PlistPath` | `Contents/Info.plist` |
| `Enabled` | `true` |
| `Arch` | `x86_64` |
| `MinKernel` | `24.0.0` |
| `MaxKernel` | `24.99.99` |

### Verify

Save the configuration, [validate it, and reboot](opencore.md#validation-and-rollback). Then run the following in Terminal.

```sh
ioreg -r -c GoodixForceTouchEngine -l | grep PressureMapping
```

Confirm that `GoodixForceTouchEngine` matched. Enable Force Click in System Settings → Trackpad, then try a normal click and a force click in an app that supports the gesture. If the engine does not match, check the trackpad’s vendor and product IDs.
