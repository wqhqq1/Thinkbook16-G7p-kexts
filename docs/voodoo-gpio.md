# VoodooGPIO.kext 1.1.1

[简体中文](#简体中文) · [English](#english)

## 简体中文

为 ACPI 设备 `INTC105E` 提供 GPIO 中断支持。先按 [中文版 README 的构建说明](../README.md#单独构建-voodoogpio)生成 `build/VoodooGPIO.kext`。实机使用 VoodooI2C 2.9.1 和原版 VoodooI2CServices。

完整顺序见 [I2C 触控板注入顺序](opencore.md#i2c-触控板注入顺序)。

### 安装

先按 [OpenCore 通用步骤](opencore.md)备份 EFI。

1. 用构建出的 bundle 替换 `EFI/OC/Kexts/VoodooI2C.kext/Contents/PlugIns/VoodooGPIO.kext`。沿用能枚举 `INTC105E` 的 GPIO ACPI 配置。
2. 在 `Kernel → Add` 中使用下表的嵌套 `BundlePath`。按实机顺序放在 `VoodooI2CServices` 后、`VoodooI2C.kext` 主条目前。禁用其他指向同 bundle ID 的条目。

| 字段 | 值 |
| --- | --- |
| `BundlePath` | `VoodooI2C.kext/Contents/PlugIns/VoodooGPIO.kext` |
| `ExecutablePath` | `Contents/MacOS/VoodooGPIO` |
| `PlistPath` | `Contents/Info.plist` |
| `Enabled` | `true` |
| `Arch` | `x86_64` |
| `MinKernel` | `24.0.0` |
| `MaxKernel` | `24.99.99` |

### 验证

保存配置并[校验后重启](opencore.md#校验与回退)，在终端运行：

```sh
ioreg -r -c VoodooGPIOMeteorLakeP -d 1
```

确认出现 `VoodooGPIOMeteorLakeP` 服务，再测试触控板。若只有 kext 加载记录，先核对 ACPI 是否向 macOS 暴露了 `INTC105E`。

## English

Provides GPIO interrupt support for ACPI device `INTC105E`. First generate `build/VoodooGPIO.kext` using the [build instructions](../README.en.md#build-voodoogpio-separately). The tested setup uses VoodooI2C 2.9.1 and the unmodified VoodooI2CServices.

See the [I2C trackpad injection order](opencore.md#i2c-trackpad-injection-order).

### Install

[Back up the active EFI](opencore.md#english) before making changes.

1. Replace `EFI/OC/Kexts/VoodooI2C.kext/Contents/PlugIns/VoodooGPIO.kext` with the built bundle. Keep the GPIO ACPI configuration that exposes `INTC105E`.
2. Use the nested `BundlePath` below in `Kernel → Add`. In the tested order it follows `VoodooI2CServices` and precedes the `VoodooI2C.kext` entry. Disable any other entry with the same bundle ID.

| Field | Value |
| --- | --- |
| `BundlePath` | `VoodooI2C.kext/Contents/PlugIns/VoodooGPIO.kext` |
| `ExecutablePath` | `Contents/MacOS/VoodooGPIO` |
| `PlistPath` | `Contents/Info.plist` |
| `Enabled` | `true` |
| `Arch` | `x86_64` |
| `MinKernel` | `24.0.0` |
| `MaxKernel` | `24.99.99` |

### Verify

Save the configuration, [validate it, and reboot](opencore.md#validation-and-rollback). Then run the following in Terminal.

```sh
ioreg -r -c VoodooGPIOMeteorLakeP -d 1
```

Confirm that `VoodooGPIOMeteorLakeP` appears and then test the trackpad. If the kext loads without a matching service, check whether ACPI exposes `INTC105E` to macOS.
