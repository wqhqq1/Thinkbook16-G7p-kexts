# VoodooInput.kext 1.1.8

[简体中文](#简体中文) · [English](#english)

## 简体中文

保留触控板上报的压力值。实机将这版放在 VoodooI2C 2.9.1 的插件目录，配合 [VoodooI2CHID](voodoo-i2chid.md) 与 [GoodixForceTouch](goodix-force-touch.md)。

完整顺序见 [I2C 触控板注入顺序](opencore.md#i2c-触控板注入顺序)。

### 安装

先按 [OpenCore 通用步骤](opencore.md)备份 EFI。

1. 从 [Kexts](../Kexts/VoodooInput.kext) 复制整个 bundle，替换 `EFI/OC/Kexts/VoodooI2C.kext/Contents/PlugIns/VoodooInput.kext`。
2. 使用下表的嵌套 `BundlePath`，排在 `VoodooI2C.kext` 后面、`VoodooI2CHID.kext` 前面。检查独立目录及 VoodooPS2Controller 的嵌套插件，只启用一份 VoodooInput 条目。

| 字段 | 值 |
| --- | --- |
| `BundlePath` | `VoodooI2C.kext/Contents/PlugIns/VoodooInput.kext` |
| `ExecutablePath` | `Contents/MacOS/VoodooInput` |
| `PlistPath` | `Contents/Info.plist` |
| `Enabled` | `true` |
| `Arch` | `x86_64` |
| `MinKernel` | `24.0.0` |
| `MaxKernel` | `24.99.99` |

### 验证

保存配置并[校验后重启](opencore.md#校验与回退)，在终端运行：

```sh
kmutil showloaded --list-only | grep -F me.kishorprins.VoodooInput
ioreg -r -c VoodooInput -d 1
```

确认加载版本为 `1.1.8` 且出现 `VoodooInput` 服务。已安装 GoodixForceTouch 时，再对照普通按压和重按的行为。

## English

Preserves pressure values reported by the trackpad. The tested setup uses this version inside VoodooI2C 2.9.1 with [VoodooI2CHID](voodoo-i2chid.md#english) and [GoodixForceTouch](goodix-force-touch.md#english).

See the [I2C trackpad injection order](opencore.md#i2c-trackpad-injection-order).

### Install

[Back up the active EFI](opencore.md#english) before making changes.

1. Copy the entire [VoodooInput.kext](../Kexts/VoodooInput.kext) bundle over `EFI/OC/Kexts/VoodooI2C.kext/Contents/PlugIns/VoodooInput.kext`.
2. Use the nested `BundlePath` below and place it after `VoodooI2C.kext` and before `VoodooI2CHID.kext`. Check for standalone copies and copies nested under VoodooPS2Controller, and enable only one VoodooInput entry.

| Field | Value |
| --- | --- |
| `BundlePath` | `VoodooI2C.kext/Contents/PlugIns/VoodooInput.kext` |
| `ExecutablePath` | `Contents/MacOS/VoodooInput` |
| `PlistPath` | `Contents/Info.plist` |
| `Enabled` | `true` |
| `Arch` | `x86_64` |
| `MinKernel` | `24.0.0` |
| `MaxKernel` | `24.99.99` |

### Verify

Save the configuration, [validate it, and reboot](opencore.md#validation-and-rollback). Then run the following in Terminal.

```sh
kmutil showloaded --list-only | grep -F me.kishorprins.VoodooInput
ioreg -r -c VoodooInput -d 1
```

Confirm that version `1.1.8` loaded and that a `VoodooInput` service appears. With GoodixForceTouch installed, check that normal and firm presses behave as expected.
