# ThinkBookI2C.kext 1.0.0

[简体中文](#简体中文) · [English](#english)

## 简体中文

这是无可执行文件的匹配插件，让 VoodooI2C 2.9.1 接管 PCI `8086:7778`。配套 `SSDT-TB-I2C.aml` 根据本机固件字段暴露控制器及 HID 触控板。

完整顺序见 [I2C 触控板注入顺序](opencore.md#i2c-触控板注入顺序)。

### 安装

先按 [OpenCore 通用步骤](opencore.md)备份 EFI。

1. 从 [Kexts](../Kexts/ThinkBookI2C.kext) 复制整个 `ThinkBookI2C.kext` 到 `EFI/OC/Kexts/`，并排在 `VoodooI2C.kext` 主条目后面。`ExecutablePath` 填空字符串。
2. 核对固件存在 `\_SB.PC00`、`\_SB.GPI0`，以及供该表读取的 `TPDF` 选择字段。确认没有其他表已提供同一个 `ZI20` 或为同一 PCI 地址重复声明控制器。将 [SSDT-TB-I2C.aml](../ACPI/SSDT-TB-I2C.aml) 放到 `EFI/OC/ACPI/`，在 `ACPI → Add` 添加 `Path = SSDT-TB-I2C.aml`、`Enabled = true`。

| 字段 | 值 |
| --- | --- |
| `BundlePath` | `ThinkBookI2C.kext` |
| `ExecutablePath` | `""` |
| `PlistPath` | `Contents/Info.plist` |
| `Enabled` | `true` |
| `Arch` | `x86_64` |
| `MinKernel` | `24.0.0` |
| `MaxKernel` | `24.99.99` |

### 验证

保存配置并[校验后重启](opencore.md#校验与回退)，在终端运行：

```sh
ioreg -r -c VoodooI2CPCIController -l | grep -E 'VoodooI2CPCIController|I2CClkRate'
ioreg -p IOACPIPlane -n ZI20 -r -d 2
```

确认控制器已匹配并出现 `ZI20` 和 `TPAD`。用 I/O Registry 检查这份无可执行文件的匹配插件是否生效。

## English

This codeless matching plug-in lets VoodooI2C 2.9.1 attach to PCI `8086:7778`. The companion `SSDT-TB-I2C.aml` exposes the controller and HID trackpad using this model’s firmware fields.

See the [I2C trackpad injection order](opencore.md#i2c-trackpad-injection-order).

### Install

[Back up the active EFI](opencore.md#english) before making changes.

1. Copy the entire [ThinkBookI2C.kext](../Kexts/ThinkBookI2C.kext) bundle to `EFI/OC/Kexts/` and place it after the main `VoodooI2C.kext` entry. Set `ExecutablePath` to an empty string.
2. Verify that the firmware exposes `\_SB.PC00`, `\_SB.GPI0`, and the `TPDF` selector used by the table. Check that no other table already declares `ZI20` or another controller at the same PCI address. Copy [SSDT-TB-I2C.aml](../ACPI/SSDT-TB-I2C.aml) to `EFI/OC/ACPI/`, then add an `ACPI → Add` entry with `Path = SSDT-TB-I2C.aml` and `Enabled = true`.

| Field | Value |
| --- | --- |
| `BundlePath` | `ThinkBookI2C.kext` |
| `ExecutablePath` | `""` |
| `PlistPath` | `Contents/Info.plist` |
| `Enabled` | `true` |
| `Arch` | `x86_64` |
| `MinKernel` | `24.0.0` |
| `MaxKernel` | `24.99.99` |

### Verify

Save the configuration, [validate it, and reboot](opencore.md#validation-and-rollback). Then run the following in Terminal.

```sh
ioreg -r -c VoodooI2CPCIController -l | grep -E 'VoodooI2CPCIController|I2CClkRate'
ioreg -p IOACPIPlane -n ZI20 -r -d 2
```

Confirm that the controller matched and that `ZI20` and `TPAD` appear. Use the I/O Registry to check whether the codeless matching plug-in took effect.
