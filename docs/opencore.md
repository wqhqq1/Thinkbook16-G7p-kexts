# OpenCore 注入通用步骤 / OpenCore injection

[简体中文](#简体中文) · [English](#english)

## 简体中文

以下配置以 ThinkBook 16 G7+ IAH（21TL）和 macOS Sequoia 15.7.8 为基线。先挂载用于启动 macOS 的 EFI 分区，将整份 `EFI` 目录备份到另一位置。编辑正在使用的 `EFI/OC/config.plist`。

1. 按组件指南复制完整的 `.kext` 目录。使用嵌套插件时，先替换 `Contents/PlugIns/` 下的旧 bundle，再核对其在 `Kernel → Add` 中的条目。
2. 在 `Kernel → Add` 新增或更新对应字典。`BundlePath` 相对于 `EFI/OC/Kexts/`，`ExecutablePath` 和 `PlistPath` 相对于该 bundle。`Enabled` 用布尔值，`Arch`、`MinKernel` 和 `MaxKernel` 用字符串。`Comment` 可用组件名称。指南里的 `24.0.0` 至 `24.99.99` 将注入限定在 Darwin 24（Sequoia）。
3. 如组件需要 SSDT，将 `.aml` 放进 `EFI/OC/ACPI/`，在 `ACPI → Add` 中填文件名 `Path`、布尔值 `Enabled = true` 和简短的 `Comment`。已有条目时更新原条目。 [OpenCore 配置参考](https://github.com/acidanthera/OpenCorePkg/blob/1.0.8/Docs/Configuration.pdf)

### I2C 触控板注入顺序

下表是实机的 `Kernel → Add` 相对顺序。`VoodooI2C.kext`（2.9.1）和 VoodooI2CServices 由上游提供。`SSDT-TB-I2C.aml` 和 GPIO ACPI 配置分别见 [ThinkBookI2C](thinkbook-i2c.md) 和 [VoodooGPIO](voodoo-gpio.md)。 [VoodooI2C 2.9.1](https://github.com/VoodooI2C/VoodooI2C/releases/tag/v2.9.1)

| 顺序 | BundlePath |
| --- | --- |
| 1 | `VoodooI2C.kext/Contents/PlugIns/VoodooI2CServices.kext` |
| 2 | `VoodooI2C.kext/Contents/PlugIns/VoodooGPIO.kext` |
| 3 | `VoodooI2C.kext` |
| 4 | `GoodixForceTouch.kext`（仅适用 `27C6:01E9`） |
| 5 | `ThinkBookI2C.kext` |
| 6 | `VoodooI2C.kext/Contents/PlugIns/VoodooInput.kext` |
| 7 | `VoodooI2CHID.kext` |

### 校验与回退

使用与引导程序同版本的 `ocvalidate`，把示例中的两个路径替换成实际位置。校验通过后重启，再按组件指南检查是否匹配到设备。

```sh
"/path/to/ocvalidate" "/Volumes/EFI/EFI/OC/config.plist"
```

如果组件失效，可恢复备份中的对应 bundle 和配置条目。若无法启动，使用可启动的备用 EFI 进入系统，再恢复备份。

## English

These settings use the ThinkBook 16 G7+ IAH (21TL) and macOS Sequoia 15.7.8 as their baseline. Mount the EFI partition that boots macOS and back up the entire `EFI` directory to another location. Edit the active `EFI/OC/config.plist`.

1. Copy the entire `.kext` directory as described in the component guide. For a nested plug-in, replace the old bundle under `Contents/PlugIns/`, then check its `Kernel → Add` entry.
2. Add or update the matching dictionary under `Kernel → Add`. `BundlePath` is relative to `EFI/OC/Kexts/`. `ExecutablePath` and `PlistPath` are relative to the bundle. `Enabled` is a boolean. `Arch`, `MinKernel`, and `MaxKernel` are strings. Use the component name for `Comment`. The range `24.0.0` through `24.99.99` limits injection to Darwin 24 (Sequoia).
3. If the component needs an SSDT, copy the `.aml` to `EFI/OC/ACPI/`. Under `ACPI → Add`, set `Path` to the filename, `Enabled` to boolean `true`, and add a short `Comment`. Update the existing entry if present. [OpenCore configuration reference](https://github.com/acidanthera/OpenCorePkg/blob/1.0.8/Docs/Configuration.pdf)

### I2C trackpad injection order

This is the relative `Kernel → Add` order from the tested machine. Get `VoodooI2C.kext` (2.9.1) and VoodooI2CServices from upstream. For `SSDT-TB-I2C.aml` and the GPIO ACPI setup, see [ThinkBookI2C](thinkbook-i2c.md#english) and [VoodooGPIO](voodoo-gpio.md#english), respectively. [VoodooI2C 2.9.1](https://github.com/VoodooI2C/VoodooI2C/releases/tag/v2.9.1)

| Order | BundlePath |
| --- | --- |
| 1 | `VoodooI2C.kext/Contents/PlugIns/VoodooI2CServices.kext` |
| 2 | `VoodooI2C.kext/Contents/PlugIns/VoodooGPIO.kext` |
| 3 | `VoodooI2C.kext` |
| 4 | `GoodixForceTouch.kext` (only for `27C6:01E9`) |
| 5 | `ThinkBookI2C.kext` |
| 6 | `VoodooI2C.kext/Contents/PlugIns/VoodooInput.kext` |
| 7 | `VoodooI2CHID.kext` |

### Validation and rollback

Run `ocvalidate` from the same release as your OpenCore bootloader. Substitute both paths below. Once validation passes, reboot and follow the component guide to check that its driver matches the device.

```sh
"/path/to/ocvalidate" "/Volumes/EFI/EFI/OC/config.plist"
```

If a component stops working, restore its bundle and entries from the backup. If the system cannot boot, use a known working EFI to boot and then restore the backup.
