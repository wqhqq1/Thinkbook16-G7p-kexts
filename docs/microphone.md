# 内置数字麦克风 · OhMyDSP ACE 0.3.4

适用于 ThinkBook 16 G7+ IAH（21TL）的 Intel HDA/DSP `8086:7728`、子系统 `17aa:3847`。基线为 macOS Sequoia 15.7.8（24G824，x86_64）。用户已确认 0.3.4 修复此前复现的录音和扬声器问题。内核与 Core Audio 插件均为 0.3.4，系统录音设备显示为 **Internal Microphone (ACE DMIC)**。

录音使用 48 kHz、双声道。录音期间，内置扬声器同样应设为 48 kHz。驱动不会抢占正在播放的 AppleHDA 流。若打开麦克风返回设备忙，先暂停内置扬声器，再开启录音，之后可恢复播放。

## 构建

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

## 安装与回退

1. 备份现有 EFI，将输出的 `build/OhMyDSPACE.kext` 复制到 `EFI/OC/Kexts/OhMyDSPACE-034.kext`。将 `OpenCore-Kernel-Add.plist` 中的条目合入当前配置，禁用旧的 OhMyDSP 或 ThinkBookDMIC 实验版。使用同版本的 `ocvalidate` 校验。
2. 首次安装时，在输出目录运行 `sudo ./install-userspace.sh`，重启后加载后台服务和 Core Audio 插件。也可用仓库的 `Audio/OhMyDSP-ACE/install-userspace.sh` 安装已签名的用户态组件。已安装相同版本的组件无需重装。
3. 重启后检查 `kmutil showloaded` 是否列出 `local.OhMyDSPACE (0.3.4)`。在「系统设置 → 声音 → 输入」选择内置麦克风，试听录音，并验证录音期间内置扬声器可反复启停。
4. 回退时，在当前配置里禁用本驱动。按需卸载 `/Library/LaunchDaemons/com.ohmydsp.ace.daemon.plist`、`/Library/Audio/Plug-Ins/HAL/OhMyDSP-ACE.driver` 和 `/Library/Application Support/OhMyDSP-ACE`，然后重启。

驱动复用 [OhMyDSP `0b928ef`](https://github.com/itsyvintsyspider/OhMyDSP/tree/0b928ef08a9a47d12d768128981ba88d0f1e8b8d) 的用户态服务和共享缓冲区协议，内核端为本机的 ACE/IPC4 实现。仓库 `LICENSES/` 保留 OhMyDSP BSD 许可及 Intel SOF 许可。
