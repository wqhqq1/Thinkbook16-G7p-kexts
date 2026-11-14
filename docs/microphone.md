# 内置数字麦克风 · OhMyDSP ACE 0.3.7

[简体中文](#简体中文) · [English](#english)

## 简体中文

适用于 ThinkBook 16 G7+ IAH（21TL）的 Intel HDA/DSP `8086:7728`、子系统 `17aa:3847`。内核 0.3.7 已在 macOS Tahoe 26.7.3（25G342，x86_64）实机验证录音。Sequoia 15.7.8（24G824）的实机验证版本为 0.3.4。Core Audio 插件和后台服务仍为 0.3.4，两个系统通用。系统录音设备显示为 **Internal Microphone (ACE DMIC)**。

录音时，驱动向 `AppleHDAController` 借用一条输入流描述符。它通过 Mach-O UUID `127B008B-0862-3232-9A2B-05A8B1C75BEE` 和两个流分配方法的代码特征识别控制器二进制。Tahoe 已移除 AppleHDA，需要由 SimpleHDA 1.0.1（laobamac） 提供同一版本的 AppleHDAController。SimpleHDA 用自带的 kxld 链接该镜像，运行时 load command 会被重排，所以驱动按 load command 查找 `LC_UUID`，不依赖固定偏移。每一步的结果会写入 ioreg 的 `ACELease*` 属性。

录音使用 48 kHz、双声道。录音期间，内置扬声器同样应设为 48 kHz。驱动不会抢占正在播放的 AppleHDA 流。若打开麦克风返回设备忙，先暂停内置扬声器，再开启录音，之后可恢复播放。

### 构建

需要 Xcode Command Line Tools、仓库首页所列 MacKernelSDK，以及下列三个输入。构建脚本会核对 SHA-256。

| 输入 | SHA-256 |
| --- | --- |
| SOF 2.15.0.1 Intel 签名的 `sof-mtl.ri` | `4f3fb3fc57d2a2c90216d2a6195e24be54921c099395c9d2d019a14bbdc09250` |
| SOF `sof-hda-generic-2ch.tplg` | `6d1313cf7311dcc47ccb78a05841f25da9640c4e87cb04a346ddf56f43f13f7b` |
| 目标机器 ACPI 的原始 `NHLT` 表 | `5357d472aa0b4a8e3474ffc39d6c99cb8dff0f455681c48c5f7e7668033f3ed8` |

固件和拓扑来自 [SOF 二进制发行仓库](https://github.com/thesofproject/sof-bin)。NHLT 由目标机器提供，例如 Windows ACPI 采集导出的 `SysReport-NHLT-1.aml`。公开仓库提供内核源码和已签名的用户态组件；含原机校准参数的已测内核包及原始输入保存在本机 `.local/OhMyDSP-ACE/`。

```sh
./scripts/build-microphone.sh /path/to/MacKernelSDK \
  /path/to/sof-mtl.ri /path/to/sof-hda-generic-2ch.tplg /path/to/NHLT.bin
```

输出默认为 `build/OhMyDSP-ACE/`，含已签名的内核、用户态插件和 OpenCore 样例条目。第五个参数可指定一个尚不存在的输出目录。

### 安装与回退

1. [备份现有 EFI](opencore.md)，将输出包中的 `build/OhMyDSPACE.kext` 复制到 `EFI/OC/Kexts/OhMyDSPACE-037.kext`。将 [OpenCore-Kernel-Add.plist](../Audio/OhMyDSP-ACE/OpenCore-Kernel-Add.plist) 中的字典合入 `Kernel → Add`，也可按下表填写。禁用旧的 OhMyDSP 或 ThinkBookDMIC 实验版，沿用能提供 `AppleHDAController` 的音频配置。
2. 首次安装时，在输出目录运行 `sudo ./install-userspace.sh`，重启后加载后台服务和 Core Audio 插件。也可使用[仓库中的安装脚本](../Audio/OhMyDSP-ACE/install-userspace.sh)安装已签名的用户态组件。已安装相同版本的组件无需重装。
3. [校验配置并重启](opencore.md#校验与回退)后，运行 `kmutil showloaded --list-only | grep -F local.OhMyDSPACE`，确认输出包含 `local.OhMyDSPACE (0.3.7)`。在「系统设置 → 声音 → 输入」选择内置麦克风，试听录音，并验证录音期间内置扬声器可反复启停。录音失败时运行 `ioreg -r -c OhMyDSPACE -l | grep -E "ACEState|ACELease"`，`ACELeaseStage` 显示借用输入流停在哪一步。
4. 回退时，在当前配置里禁用本驱动。按需卸载 `/Library/LaunchDaemons/com.ohmydsp.ace.daemon.plist`、`/Library/Audio/Plug-Ins/HAL/OhMyDSP-ACE.driver` 和 `/Library/Application Support/OhMyDSP-ACE`，然后重启。

| 字段 | 值 |
| --- | --- |
| `BundlePath` | `OhMyDSPACE-037.kext` |
| `ExecutablePath` | `Contents/MacOS/OhMyDSPACE` |
| `PlistPath` | `Contents/Info.plist` |
| `Enabled` | `true` |
| `Arch` | `x86_64` |
| `MinKernel` | `25.0.0` |
| `MaxKernel` | `25.99.99` |

本机 EFI 中 Sequoia 仍注入已验证的 0.3.4：另设 `OhMyDSPACE-034.kext` 条目，`MinKernel` 为 `24.0.0`，`MaxKernel` 为 `24.99.99`。0.3.4 源码见提交 `b94cbbb`。

驱动复用 [OhMyDSP `0b928ef`](https://github.com/itsyvintsyspider/OhMyDSP/tree/0b928ef08a9a47d12d768128981ba88d0f1e8b8d) 的用户态服务和共享缓冲区协议，内核端为本机的 ACE/IPC4 实现。仓库 `LICENSES/` 保留 OhMyDSP BSD 许可及 Intel SOF 许可。

## English

### OhMyDSP ACE 0.3.7 for the internal digital microphone

For the ThinkBook 16 G7+ IAH (21TL) with Intel HDA/DSP `8086:7728`, subsystem `17aa:3847`. Kernel 0.3.7 was verified recording on macOS Tahoe 26.7.3 (25G342, x86_64). On Sequoia 15.7.8 (24G824), the verified kernel build is 0.3.4. The Core Audio plug-in and daemon stay at 0.3.4 on both systems. The input appears as **Internal Microphone (ACE DMIC)**.

While recording, the driver borrows one input stream descriptor from `AppleHDAController`. It identifies the controller binary by its Mach-O UUID `127B008B-0862-3232-9A2B-05A8B1C75BEE` and the code of its two stream allocation methods. Tahoe removed AppleHDA, so SimpleHDA 1.0.1 (laobamac) must provide the same AppleHDAController build. SimpleHDA links that image with its own kxld, which reorders the load commands at runtime, so the driver walks the load commands to find `LC_UUID` instead of using a fixed offset. Each step is reported in the `ACELease*` ioreg properties.

Capture uses 48 kHz stereo. Set the internal speakers to 48 kHz as well when using them during recording. If opening the input returns a device-busy error, pause speaker playback, start recording, then resume playback.

### Build

Install Xcode Command Line Tools, use the MacKernelSDK revision listed in the repository README, and obtain the three inputs below. The build checks their SHA-256 hashes.

| Input | SHA-256 |
| --- | --- |
| Intel-signed SOF 2.15.0.1 `sof-mtl.ri` | `4f3fb3fc57d2a2c90216d2a6195e24be54921c099395c9d2d019a14bbdc09250` |
| SOF `sof-hda-generic-2ch.tplg` | `6d1313cf7311dcc47ccb78a05841f25da9640c4e87cb04a346ddf56f43f13f7b` |
| Raw `NHLT` ACPI table from the target machine | `5357d472aa0b4a8e3474ffc39d6c99cb8dff0f455681c48c5f7e7668033f3ed8` |

The firmware and topology come from [SOF binary releases](https://github.com/thesofproject/sof-bin). Dump the NHLT table from the target machine, for example as `SysReport-NHLT-1.aml` from a Windows ACPI dump. The public repository includes the kernel source and signed userspace components.

```sh
./scripts/build-microphone.sh /path/to/MacKernelSDK \
  /path/to/sof-mtl.ri /path/to/sof-hda-generic-2ch.tplg /path/to/NHLT.bin
```

The default output is `build/OhMyDSP-ACE/`, which contains the kext, userspace plug-in, and OpenCore entry. An optional fifth argument selects a new output directory.

### Inject with OpenCore and verify

1. [Back up the active EFI](opencore.md#english). Copy `build/OhMyDSPACE.kext` from the output package to `EFI/OC/Kexts/OhMyDSPACE-037.kext`. Add the entry from [OpenCore-Kernel-Add.plist](../Audio/OhMyDSP-ACE/OpenCore-Kernel-Add.plist) to `Kernel → Add`, or enter the fields below. Disable any older OhMyDSP ACE or ThinkBookDMIC trial entries. Keep the existing audio configuration that provides `AppleHDAController`.
2. For a first installation, run `sudo ./install-userspace.sh` from the built package, or use the [repository installer](../Audio/OhMyDSP-ACE/install-userspace.sh) with its signed userspace components. An installation of the same version can be reused.
3. [Validate the configuration and reboot](opencore.md#validation-and-rollback). Run `kmutil showloaded --list-only | grep -F local.OhMyDSPACE` and confirm version `0.3.7`. Select the microphone in System Settings → Sound → Input, record and play back a short clip, then try starting and stopping speaker playback during recording. If recording fails, run `ioreg -r -c OhMyDSPACE -l | grep -E "ACEState|ACELease"`; `ACELeaseStage` shows where borrowing the input stream stopped.
4. To roll back, disable the kext entry. If removing the userspace installation too, remove `/Library/LaunchDaemons/com.ohmydsp.ace.daemon.plist`, `/Library/Audio/Plug-Ins/HAL/OhMyDSP-ACE.driver`, and `/Library/Application Support/OhMyDSP-ACE`, then reboot.

| Field | Value |
| --- | --- |
| `BundlePath` | `OhMyDSPACE-037.kext` |
| `ExecutablePath` | `Contents/MacOS/OhMyDSPACE` |
| `PlistPath` | `Contents/Info.plist` |
| `Enabled` | `true` |
| `Arch` | `x86_64` |
| `MinKernel` | `25.0.0` |
| `MaxKernel` | `25.99.99` |

The EFI on this machine keeps injecting the verified 0.3.4 on Sequoia through a separate `OhMyDSPACE-034.kext` entry with `MinKernel` `24.0.0` and `MaxKernel` `24.99.99`. The 0.3.4 source is in commit `b94cbbb`.

The driver uses the [OhMyDSP daemon and shared buffer protocol](https://github.com/itsyvintsyspider/OhMyDSP/tree/0b928ef08a9a47d12d768128981ba88d0f1e8b8d) with a model-specific ACE/IPC4 kernel implementation. See `LICENSES/` for the OhMyDSP BSD license and the Intel SOF license.
