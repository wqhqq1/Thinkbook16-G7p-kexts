**[简体中文](README.md)** | **English**

# ThinkBook 16 G7+ IAH Kexts

macOS kernel extensions for the Lenovo ThinkBook 16 G7+ IAH (21TL). This snapshot comes from a Core Ultra 9 285H system running macOS Sequoia 15.7.8 (24G824, x86_64). It includes model-specific extensions for the trackpad, ambient light sensor, display, and internal digital microphone, along with the corresponding ACPI tables.

| Component | Version | Purpose | Injection guide |
| --- | --- | --- | --- |
| `ThinkBookISHProbe.kext` | 0.5.3 | Reads ambient light continuously from Intel ISH `8086:7745` and handles HID timeouts and wake from sleep. | [Open guide](docs/thinkbook-ish-probe.md#english) |
| `ISHLightSensor.kext` | 1.0.0 | Converts measured light levels to VirtualSMC ambient light keys and notifies macOS of updates. | [Open guide](docs/ish-light-sensor.md#english) |
| `NativeALSDisplay.kext` | 1.0.1 | Exposes the usable brightness range of the `06AF:C4B5` internal panel for native automatic brightness. | [Open guide](docs/native-als-display.md#english) |
| `GoodixForceTouch.kext` | 1.0.0 | Distinguishes normal and firm presses on the Goodix `27C6:01E9` trackpad. | [Open guide](docs/goodix-force-touch.md#english) |
| `VoodooGPIO.kext` | 1.1.1 (local build) | Adds Meteor Lake-P `INTC105E` support for trackpad GPIO interrupts. | [Open guide](docs/voodoo-gpio.md#english) |
| `VoodooI2CHID.kext` | 1.0.2 | Adapts to this model’s HID descriptor and filters duplicate reports for the same sample. | [Open guide](docs/voodoo-i2chid.md#english) |
| `VoodooInput.kext` | 1.1.8 | Preserves the pressure values reported by the trackpad. | [Open guide](docs/voodoo-input.md#english) |
| `ThinkBookI2C.kext` | 1.0.0 | Matches this model’s `8086:7778` I2C controller to VoodooI2C. Contains only `Info.plist`. | [Open guide](docs/thinkbook-i2c.md#english) |
| `OhMyDSPACE.kext` / OhMyDSP ACE | 0.3.4 (local kernel build) | Enables the internal digital microphone and internal speaker playback while recording. | [Open guide](docs/microphone.md#english) |

`Kexts/` contains the publicly distributed bundles, `src/` contains the corresponding source, and `versions.json` records bundle IDs, versions, and binary SHA-256 hashes. `ACPI/` contains the DSL sources and compiled AML for three supplemental ACPI tables. Wi-Fi, Bluetooth, and the full EFI `config.plist` are maintained separately.

## Upstream projects and licenses

- `GoodixForceTouch` is adapted from VoodooI2C’s native gesture engine. `VoodooI2CHID` uses upstream revision `2727793` and headers from the main VoodooI2C project. Both run with VoodooI2C **2.9.1** and the unmodified VoodooI2CServices. The upstream copyright notices and GPLv3 text are included. [VoodooI2C `6be653a`](https://github.com/VoodooI2C/VoodooI2C/tree/6be653a) · [VoodooI2CHID `2727793`](https://github.com/VoodooI2C/VoodooI2CHID/tree/2727793)
- `VoodooGPIO` is based on `44fc744` and includes source for matching and supporting `INTC105E`. During the build, the script extracts the Meteor Lake-P pin table from the specified Linux v6.12 file and generates a header in a local temporary directory. Upstream VoodooGPIO uses GPLv3, and the Linux pin table is marked `GPL-2.0`. The combined build is kept locally. [VoodooGPIO `44fc744`](https://github.com/VoodooI2C/VoodooGPIO/tree/44fc744) · [Linux Intel Meteor Lake pinctrl](https://github.com/torvalds/linux/blob/v6.12/drivers/pinctrl/intel/pinctrl-meteorlake.c) · [GNU GPL version compatibility](https://www.gnu.org/licenses/gpl-faq.html#v2v3Compatibility)
- `VoodooInput` is based on `d897813` and retains the upstream copyright notices and GPLv2 license. [VoodooInput `d897813`](https://github.com/acidanthera/VoodooInput/tree/d897813)
- `ISHLightSensor` is adapted from VirtualSMC’s `SMCLightSensor` and builds against the interfaces in VirtualSMC `b5c62c2` and Lilu `0249c3a`. The original licenses are in `LICENSES/`. [VirtualSMC `b5c62c2`](https://github.com/acidanthera/VirtualSMC/tree/b5c62c2) · [Lilu `0249c3a`](https://github.com/acidanthera/Lilu/tree/0249c3a)
- `ThinkBookISHProbe` implements the Intel ISH/ISHTP protocols in IOKit for this model, using the Linux implementation as a reference. Its source is marked `GPL-2.0-only`. `NativeALSDisplay` is a display integration extension written for this model. [Linux Intel ISH](https://docs.kernel.org/hid/intel-ish-hid.html)
- `SSDT-IALS` follows the ACPI device structure of OpenCorePkg’s `SSDT-ALS0` and retains its BSD-3-Clause license. It provides an ACPI device that AppleSMCLMU can match, while `ISHLightSensor` supplies live readings. [OpenCorePkg SSDT-ALS0](https://github.com/acidanthera/OpenCorePkg/blob/1.0.8/Docs/AcpiSamples/Source/SSDT-ALS0.dsl)
- `OhMyDSP ACE` is based on the OhMyDSP daemon and Core Audio plug-in. It uses Sound Open Firmware (SOF) and the machine’s NHLT parameters. The upstream licenses are included. [OhMyDSP `0b928ef`](https://github.com/itsyvintsyspider/OhMyDSP/tree/0b928ef08a9a47d12d768128981ba88d0f1e8b8d) · [SOF firmware and topology files](https://github.com/thesofproject/sof-bin)
- Builds use MacKernelSDK `3f750085caa17ec3a7880f11c11bf4f48cd6a164`. Keep the SDK checkout outside the tracked source tree. [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK/tree/3f750085caa17ec3a7880f11c11bf4f48cd6a164)

Each component follows the license and notices in its source. Upstream license texts are in `LICENSES/`.

## OpenCore integration

See the [OpenCore injection steps](docs/opencore.md#english) for EFI backup, I2C load order, and configuration validation.

Start with a bootable OpenCore configuration. Replace each existing bundle with the corresponding bundle from this repository, and check `BundlePath`, `ExecutablePath`, and `PlistPath` under `Kernel/Add`. Leave `ExecutablePath` empty for `ThinkBookI2C`. The following dependencies reflect the tested configuration.

- The trackpad uses `SSDT-TB-I2C.aml`, VoodooI2C 2.9.1, VoodooI2CServices, and this repository’s I2C components. Use a GPIO driver that supports `INTC105E` or [build VoodooGPIO locally](#build-voodoogpio-separately). The tested setup places the GPIO driver and `VoodooInput.kext` in `VoodooI2C.kext/Contents/PlugIns/` and places `GoodixForceTouch`, `ThinkBookI2C`, and `VoodooI2CHID` in `EFI/OC/Kexts/`. Keep the existing GPIO ACPI configuration and enable only one copy of each bundle ID.
- Automatic brightness uses `SSDT-IALS.aml`, Lilu, VirtualSMC, and the three light sensor and display extensions in this repository. Load Lilu and VirtualSMC before `ISHLightSensor` and disable the old `SMCLightSensor` entry.
- `SSDT-TB-HDA.aml` is an optional ACPI table for using the onboard audio controller with AppleALC. The AppleALC, NMHDA, and CtlnaSDXC binaries come from their respective projects or macOS. [AppleALC](https://github.com/acidanthera/AppleALC) · [NMHDA](https://github.com/nullmoth/NMHDA)
- The internal digital microphone requires OpenCore to load `OhMyDSPACE.kext` and macOS to load the accompanying Core Audio plug-in and daemon. [Microphone build and installation guide](docs/microphone.md#english)

This snapshot uses the tested machine’s firmware device paths and hardware IDs. Check those values before using the extensions on a different model or BIOS version.

## Repository layout

Edit drivers in `src/`, run the tools in `scripts/`, and place build outputs in `build/`. The ignored `.local/` directory holds firmware inputs from the machine, model-specific binaries built locally, and recovery configurations that contain device identifiers. Keep these files with the machine that uses them.

## Build from source

Install Xcode Command Line Tools on macOS. Obtain the versions of MacKernelSDK, Lilu, and VirtualSMC listed above, then run the following command from the repository root.

```sh
python3 scripts/build.py \
  --sdk /path/to/MacKernelSDK \
  --lilu /path/to/Lilu \
  --virtualsmc /path/to/VirtualSMC
```

The default output is `build/`. Use `--output` to choose a new directory or append kext names without the `.kext` suffix to select specific targets. Only `ISHLightSensor` needs the Lilu and VirtualSMC source paths. The script applies and verifies ad hoc signatures. Rebuilt bundles can have different UUIDs and hashes from the prebuilt bundles.

### Build VoodooGPIO separately

Download `pinctrl-meteorlake.c` from Linux v6.12 to a temporary location outside the repository. The build script checks the SHA-256 hash and generates the pin table header in a local temporary directory. The default build includes VoodooGPIO when `--pinctrl` is provided.

```sh
pinctrl_file=$(mktemp -t thinkbook-pinctrl)
curl -fL https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/pinctrl/intel/pinctrl-meteorlake.c -o "$pinctrl_file"
python3 scripts/build.py --sdk /path/to/MacKernelSDK --pinctrl "$pinctrl_file" VoodooGPIO
```

The output is `build/VoodooGPIO.kext`.
