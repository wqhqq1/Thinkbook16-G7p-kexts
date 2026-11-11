# ThinkBook 16 G7+ IAH Kexts

联想 ThinkBook 16 G7+ IAH（21TL）的 macOS 专用 kext。当前快照来自 Core Ultra 9 285H、macOS Sequoia 15.7.8（24G824，x86_64）的实机。收录触控板、环境光、显示接入和内置数字麦克风的本机版本，以及配套 ACPI 源码。

| 模块 | 版本 | 本机用途 |
| --- | --- | --- |
| `ThinkBookISHProbe.kext` | 0.5.3 | 从 Intel ISH `8086:7745` 持续读取照度，处理 HID 超时和睡眠唤醒 |
| `ISHLightSensor.kext` | 1.0.0 | 将真实照度转换为 VirtualSMC 环境光键，通知 macOS 更新 |
| `NativeALSDisplay.kext` | 1.0.1 | 为目标内屏 `06AF:C4B5` 提供原生自动调光所需的可用亮度范围 |
| `GoodixForceTouch.kext` | 1.0.0 | 为 Goodix `27C6:01E9` 识别普通按压和重按 |
| `VoodooGPIO.kext` | 1.1.1（本地构建） | 增加 `INTC105E` Meteor Lake-P 支持，用于本机触控板 GPIO 中断 |
| `VoodooI2CHID.kext` | 1.0.2 | 适配本机 HID 描述符，过滤同一采样的重复报告 |
| `VoodooInput.kext` | 1.1.8 | 保留触控板上报的压力值 |
| `ThinkBookI2C.kext` | 1.0.0 | 让 VoodooI2C 匹配本机 `8086:7778` I2C 控制器，只有 `Info.plist` |
| `OhMyDSPACE.kext` / OhMyDSP ACE | 0.3.4（内核本地构建） | 内置数字麦克风及录音期间的内置扬声器播放 |

`Kexts/` 收录公开快照中的本机版本，`src/` 保存对应源码，`versions.json` 记录 bundle ID、版本和二进制 SHA‑256。`ACPI/` 保存三张本机补充表的 DSL 和 AML。Wi‑Fi、蓝牙和现有 EFI 的整份 `config.plist` 由各自项目管理。

## 上游与许可证

- `GoodixForceTouch` 改自 VoodooI2C 的原生手势引擎。`VoodooI2CHID` 使用其上游 `2727793` 及 VoodooI2C 主工程的头文件。运行时需要 VoodooI2C **2.9.1** 和原版 VoodooI2CServices。保留上游版权声明及 GPLv3 许可文本。[VoodooI2C `6be653a`](https://github.com/VoodooI2C/VoodooI2C/tree/6be653a) · [VoodooI2CHID `2727793`](https://github.com/VoodooI2C/VoodooI2CHID/tree/2727793)
- `VoodooGPIO` 基于 `44fc744`，本仓库保存 `INTC105E` 的匹配及适配源码。编译时从指定的 Linux v6.12 文件提取 Meteor Lake-P 引脚表，在本地临时目录生成头文件。上游 VoodooGPIO 采用 GPLv3，Linux 引脚表带 `GPL-2.0` 标记，组合构建产物保存在本地。[VoodooGPIO `44fc744`](https://github.com/VoodooI2C/VoodooGPIO/tree/44fc744) · [Linux Intel Meteor Lake pinctrl](https://github.com/torvalds/linux/blob/v6.12/drivers/pinctrl/intel/pinctrl-meteorlake.c) · [GNU GPL 版本兼容说明](https://www.gnu.org/licenses/gpl-faq.html#v2v3Compatibility)
- `VoodooInput` 基于 `d897813`，按上游 GPLv2 发布的源码保留其版权声明和许可。[VoodooInput `d897813`](https://github.com/acidanthera/VoodooInput/tree/d897813)
- `ISHLightSensor` 改自 VirtualSMC 的 `SMCLightSensor`，编译使用 VirtualSMC `b5c62c2` 和 Lilu `0249c3a` 的接口。原许可文本分别存于 `LICENSES/`。[VirtualSMC `b5c62c2`](https://github.com/acidanthera/VirtualSMC/tree/b5c62c2) · [Lilu `0249c3a`](https://github.com/acidanthera/Lilu/tree/0249c3a)
- `ThinkBookISHProbe` 是本机 IOKit 实现，参考 Linux Intel ISH/ISHTP 的协议，源码使用 `GPL-2.0-only` 标记。`NativeALSDisplay` 是本机编写的显示接入扩展。[Linux Intel ISH](https://docs.kernel.org/hid/intel-ish-hid.html)
- `SSDT-IALS` 的设备结构参考 OpenCorePkg `SSDT-ALS0`，保留 OpenCorePkg 的 BSD-3-Clause 许可文本。它向 AppleSMCLMU 提供 ACPI 匹配入口，实时照度由 `ISHLightSensor` 提供。[OpenCorePkg SSDT-ALS0](https://github.com/acidanthera/OpenCorePkg/blob/1.0.8/Docs/AcpiSamples/Source/SSDT-ALS0.dsl)
- `OhMyDSP ACE` 基于 OhMyDSP 的后台服务和 Core Audio 插件，使用 Sound Open Firmware（SOF）及本机 NHLT 参数。上游许可证随仓库提供。[OhMyDSP `0b928ef`](https://github.com/itsyvintsyspider/OhMyDSP/tree/0b928ef08a9a47d12d768128981ba88d0f1e8b8d) · [SOF 固件与拓扑](https://github.com/thesofproject/sof-bin)
- 构建依赖 MacKernelSDK `3f750085caa17ec3a7880f11c11bf4f48cd6a164`。使用仓库外的 SDK 源码目录。[MacKernelSDK](https://github.com/acidanthera/MacKernelSDK/tree/3f750085caa17ec3a7880f11c11bf4f48cd6a164)

各组件沿用对应源码的许可与声明。`LICENSES/` 收录上游许可文本。

## OpenCore 集成

使用现有的可启动配置。将本仓库的同名 kext 替换到现有 `Kernel/Add` 对应路径，检查 BundlePath、ExecutablePath 和 PlistPath。`ThinkBookI2C` 的 ExecutablePath 为空。以下是实机配置里的依赖关系。

- 触控板使用 `SSDT-TB-I2C.aml`、VoodooI2C 2.9.1、VoodooI2CServices 和本仓库的 I2C 组件。使用已有的 `INTC105E` 兼容 GPIO 驱动，或用下节命令构建。本机将其与 `VoodooInput.kext` 放在 `VoodooI2C.kext/Contents/PlugIns/`，`GoodixForceTouch`、`ThinkBookI2C` 和 `VoodooI2CHID` 放在 `EFI/OC/Kexts/`。沿用已有的 GPIO ACPI 配置，每个 bundle ID 只启用一份。
- 自动调光使用 `SSDT-IALS.aml`、Lilu、VirtualSMC 及本仓库的三个照度和显示扩展。Lilu 和 VirtualSMC 在 `ISHLightSensor` 之前加载，同一套配置中禁用旧 `SMCLightSensor` 条目。
- `SSDT-TB-HDA.aml` 是本机板载声卡的可选 ACPI 配置，配合 AppleALC 使用。AppleALC、NMHDA 和 CtlnaSDXC 的原有二进制由各自项目或系统提供。[AppleALC](https://github.com/acidanthera/AppleALC) · [NMHDA](https://github.com/nullmoth/NMHDA)

- 内置数字麦克风需要注入 `OhMyDSPACE.kext`，并安装配套的 Core Audio 插件和后台服务。见[麦克风构建与安装](docs/microphone.md)。

该快照对应本机的固件设备路径和硬件 ID。更换机型或 BIOS 后，先核对这些匹配条件，再使用相应模块。

## 维护目录

后续在仓库中编辑 `src/`、运行 `scripts/`，构建结果放在 `build/`。实机固件输入、预编译的本机专用包和含设备身份的恢复配置保存在忽略目录 `.local/`。从 GitHub 克隆时不会下载这些本机文件。

## 从源码构建

在 macOS 上安装 Xcode Command Line Tools。准备上节指定版本的 MacKernelSDK、Lilu 和 VirtualSMC 源码，然后在仓库根目录运行：

```sh
python3 scripts/build.py \
  --sdk /path/to/MacKernelSDK \
  --lilu /path/to/Lilu \
  --virtualsmc /path/to/VirtualSMC
```

输出放在 `build/`，也可用 `--output` 指定一个新目录。命令末尾可加 kext 名称（省略 `.kext`）来选择目标，只有 `ISHLightSensor` 需要 Lilu 和 VirtualSMC 源码参数。脚本会构建并验证 ad hoc 签名。预编译包和重新构建的包可以有不同的 UUID 和哈希。


### 单独构建 VoodooGPIO

下载 Linux v6.12 的 `pinctrl-meteorlake.c` 到仓库外，再构建 GPIO 目标。构建脚本会核对输入 SHA-256，并在本地临时目录生成引脚表。默认构建会在提供 `--pinctrl` 时包含这个目标。

```sh
pinctrl_file=$(mktemp -t thinkbook-pinctrl)
curl -fL https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/pinctrl/intel/pinctrl-meteorlake.c -o "$pinctrl_file"
python3 scripts/build.py --sdk /path/to/MacKernelSDK --pinctrl "$pinctrl_file" VoodooGPIO
```

输出为 `build/VoodooGPIO.kext`。
