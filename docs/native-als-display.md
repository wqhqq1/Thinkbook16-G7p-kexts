# NativeALSDisplay.kext 1.0.1

[简体中文](#简体中文) · [English](#english)

## 简体中文

为 `NVRMFramebuffer` 下的 `06AF:C4B5` 内屏提供可用亮度范围。自动调光需要 [ThinkBookISHProbe](thinkbook-ish-probe.md)、[ISHLightSensor](ish-light-sensor.md) 和 AppleSMCLMU 对应的 ACPI 设备。

### 安装

先按 [OpenCore 通用步骤](opencore.md)备份 EFI。

1. 从 [Kexts](../Kexts/NativeALSDisplay.kext) 复制整个 `NativeALSDisplay.kext` 到 `EFI/OC/Kexts/`，按下表更新 `Kernel → Add`。
2. 如果当前配置尚未提供 `\_SB.ALS0`，复制 [SSDT-IALS.aml](../ACPI/SSDT-IALS.aml) 到 `EFI/OC/ACPI/`。在 `ACPI → Add` 添加 `Path = SSDT-IALS.aml`、`Enabled = true`。已有同名设备时先核对现有 ACPI 表，避免重复声明。

| 字段 | 值 |
| --- | --- |
| `BundlePath` | `NativeALSDisplay.kext` |
| `ExecutablePath` | `Contents/MacOS/NativeALSDisplay` |
| `PlistPath` | `Contents/Info.plist` |
| `Enabled` | `true` |
| `Arch` | `x86_64` |
| `MinKernel` | `24.0.0` |
| `MaxKernel` | `24.99.99` |

### 验证

保存配置并[校验后重启](opencore.md#校验与回退)，在终端运行：

```sh
ioreg -r -c NativeALSDisplay -l | grep RangeStatus
ioreg -r -c AppleSMCLMU -d 1
```

`RangeStatus` 应显示已添加亮度范围，或已保留现有范围。到「系统设置 → 显示器」开启自动调节亮度，改变环境光观察屏幕响应。如果一直等待目标屏幕，先核对面板 ID 和显示驱动。

## English

Exposes the usable brightness range of the `06AF:C4B5` internal panel under `NVRMFramebuffer`. Automatic brightness also needs [ThinkBookISHProbe](thinkbook-ish-probe.md#english), [ISHLightSensor](ish-light-sensor.md#english), and an ACPI device that AppleSMCLMU can match.

### Install

[Back up the active EFI](opencore.md#english) before making changes.

1. Copy the entire [NativeALSDisplay.kext](../Kexts/NativeALSDisplay.kext) bundle to `EFI/OC/Kexts/` and use the table below to update `Kernel → Add`.
2. If the current configuration does not already provide `\_SB.ALS0`, copy [SSDT-IALS.aml](../ACPI/SSDT-IALS.aml) to `EFI/OC/ACPI/`. Add an `ACPI → Add` entry with `Path = SSDT-IALS.aml` and `Enabled = true`. If that device already exists, check the existing table to avoid declaring it twice.

| Field | Value |
| --- | --- |
| `BundlePath` | `NativeALSDisplay.kext` |
| `ExecutablePath` | `Contents/MacOS/NativeALSDisplay` |
| `PlistPath` | `Contents/Info.plist` |
| `Enabled` | `true` |
| `Arch` | `x86_64` |
| `MinKernel` | `24.0.0` |
| `MaxKernel` | `24.99.99` |

### Verify

Save the configuration, [validate it, and reboot](opencore.md#validation-and-rollback). Then run the following in Terminal.

```sh
ioreg -r -c NativeALSDisplay -l | grep RangeStatus
ioreg -r -c AppleSMCLMU -d 1
```

`RangeStatus` should report that a range was added or that an existing range was preserved. Enable automatic brightness in System Settings → Displays and vary the ambient light to check the response. If the extension is still waiting for the target display, check the panel ID and display driver.
