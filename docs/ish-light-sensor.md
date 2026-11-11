# ISHLightSensor.kext 1.0.0

[简体中文](#简体中文) · [English](#english)

## 简体中文

将 Intel ISH 的真实照度接入 VirtualSMC。准备 Lilu、VirtualSMC 和 [ThinkBookISHProbe](thinkbook-ish-probe.md)。如需系统自动调光，还要按 [NativeALSDisplay 指南](native-als-display.md)核对 `SSDT-IALS.aml` 和内屏。

### 安装

先按 [OpenCore 通用步骤](opencore.md)备份 EFI。

1. 从 [Kexts](../Kexts/ISHLightSensor.kext) 复制整个 `ISHLightSensor.kext` 到 `EFI/OC/Kexts/`。
2. 按下表更新 `Kernel → Add`。将 `ISHLightSensor` 排在 `Lilu` 和 `VirtualSMC` 后面。禁用旧的 `SMCLightSensor.kext` 注入条目，避免两个插件注册同一组环境光键。

| 字段 | 值 |
| --- | --- |
| `BundlePath` | `ISHLightSensor.kext` |
| `ExecutablePath` | `Contents/MacOS/ISHLightSensor` |
| `PlistPath` | `Contents/Info.plist` |
| `Enabled` | `true` |
| `Arch` | `x86_64` |
| `MinKernel` | `24.0.0` |
| `MaxKernel` | `24.99.99` |

### 验证

保存配置并[校验后重启](opencore.md#校验与回退)，在终端运行：

```sh
ioreg -r -c ISHLightSensor -l | grep -E 'NativeSMCPluginSubmitted|ALSReadingValid|ALSLuxMilli'
```

确认 `NativeSMCPluginSubmitted` 和 `ALSReadingValid` 为 `Yes`，并改变环境光检查 `ALSLuxMilli`。如果插件已提交但读数无效，先检查 ISH 探针是否有有效样本。

## English

Feeds Intel ISH light readings into VirtualSMC. You need Lilu, VirtualSMC, and [ThinkBookISHProbe](thinkbook-ish-probe.md#english). For system automatic brightness, see the [NativeALSDisplay guide](native-als-display.md#english) for the panel and `SSDT-IALS.aml` setup.

### Install

[Back up the active EFI](opencore.md#english) before making changes.

1. Copy the entire [ISHLightSensor.kext](../Kexts/ISHLightSensor.kext) bundle to `EFI/OC/Kexts/`.
2. Use the table below to update `Kernel → Add`. Place `ISHLightSensor` after both `Lilu` and `VirtualSMC`. Disable the old `SMCLightSensor.kext` entry to avoid registering the same ambient light keys twice.

| Field | Value |
| --- | --- |
| `BundlePath` | `ISHLightSensor.kext` |
| `ExecutablePath` | `Contents/MacOS/ISHLightSensor` |
| `PlistPath` | `Contents/Info.plist` |
| `Enabled` | `true` |
| `Arch` | `x86_64` |
| `MinKernel` | `24.0.0` |
| `MaxKernel` | `24.99.99` |

### Verify

Save the configuration, [validate it, and reboot](opencore.md#validation-and-rollback). Then run the following in Terminal.

```sh
ioreg -r -c ISHLightSensor -l | grep -E 'NativeSMCPluginSubmitted|ALSReadingValid|ALSLuxMilli'
```

Confirm that `NativeSMCPluginSubmitted` and `ALSReadingValid` are both `Yes`. Change the ambient light and check `ALSLuxMilli` again. If the plug-in was submitted but the reading is invalid, check whether the ISH probe has valid samples.
