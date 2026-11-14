# ThinkBookISHProbe.kext 0.5.3

[简体中文](#简体中文) · [English](#english)

## 简体中文

适用于 Intel ISH `8086:7745`。本扩展读取真实照度。自动亮度还需要 [ISHLightSensor](ish-light-sensor.md) 和适配内屏时的 [NativeALSDisplay](native-als-display.md)。

### 安装

先按 [OpenCore 通用步骤](opencore.md)备份 EFI。

1. 从 [Kexts](../Kexts/ThinkBookISHProbe.kext) 复制整个 `ThinkBookISHProbe.kext` 到 `EFI/OC/Kexts/`。
2. 按下表更新 `Kernel → Add`。本扩展本身无需 ACPI 补丁。若曾安装旧版 ISH 探针，只启用当前版本。

| 字段 | 值 |
| --- | --- |
| `BundlePath` | `ThinkBookISHProbe.kext` |
| `ExecutablePath` | `Contents/MacOS/ThinkBookISHProbe` |
| `PlistPath` | `Contents/Info.plist` |
| `Enabled` | `true` |
| `Arch` | `x86_64` |
| `MinKernel` | `24.0.0` |
| `MaxKernel` | `25.99.99` |

### 验证

保存配置并[校验后重启](opencore.md#校验与回退)，在终端运行：

```sh
ioreg -r -c ThinkBookISHProbe -l | grep -E 'ALSSampleValid|ALSSampleSequence|ALSRawMilliLux'
```

看到 `ALSSampleValid = Yes` 后，改变传感器周围光线。再次运行命令，检查样本序号是否推进、照度是否变化。睡眠唤醒后可以用同样的方式检查是否恢复采样。

## English

For Intel ISH `8086:7745`. This extension reads ambient light. To enable automatic brightness, also install [ISHLightSensor](ish-light-sensor.md#english) and, for the matching panel, [NativeALSDisplay](native-als-display.md#english).

### Install

[Back up the active EFI](opencore.md#english) before making changes.

1. Copy the entire [ThinkBookISHProbe.kext](../Kexts/ThinkBookISHProbe.kext) bundle to `EFI/OC/Kexts/`.
2. Use the table below to update `Kernel → Add`. The probe itself needs no ACPI patch. If you previously installed an older ISH probe, enable only the current version.

| Field | Value |
| --- | --- |
| `BundlePath` | `ThinkBookISHProbe.kext` |
| `ExecutablePath` | `Contents/MacOS/ThinkBookISHProbe` |
| `PlistPath` | `Contents/Info.plist` |
| `Enabled` | `true` |
| `Arch` | `x86_64` |
| `MinKernel` | `24.0.0` |
| `MaxKernel` | `25.99.99` |

### Verify

Save the configuration, [validate it, and reboot](opencore.md#validation-and-rollback). Then run the following in Terminal.

```sh
ioreg -r -c ThinkBookISHProbe -l | grep -E 'ALSSampleValid|ALSSampleSequence|ALSRawMilliLux'
```

Once `ALSSampleValid` is `Yes`, vary the light around the sensor and run the command again. Check that the sample sequence advances and the reading changes. Repeat after waking from sleep to check that sampling resumes.
