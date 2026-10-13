# XM5 PEQ

Adds a parametric equalizer (PEQ) to the ONIX XM5.

Users can save EQ presets to a TF card, then select and apply them on the player. Compatible Equalizer APO and AutoEQ text presets are supported. The player reads their filter frequencies, gains, Q values and preamp settings for local music playback.

This project is based on the official V2.43 firmware.

## Features

- Keeps the factory EQ and adds a separate PEQ page. Swipe left from the factory EQ page to open it.
- Independent switches for PEQ and the factory EQ. Can be used separately or together.
- Supports up to 30 active filters.
- PEQ supports local PCM playback up to 192 kHz. The factory EQ remains limited to 48 kHz. PEQ does not apply to DSD playback or USB DAC mode.
- The interface follows the factory XM5 style and supports Simplified Chinese and English.

## Usage

For firmware installation, refer to the [XM5 TF-card update instructions provided by ONIX](https://onixhiend.co.uk/overture-firmware-update/).

1. Create an `EQ` folder at the TF card root. Place your presets inside it as UTF-8 `.txt` files.
2. Swipe left from the factory EQ page to open the PEQ page.
3. Tap `PEQ OFF` to open the preset list, then tap `Rescan` to scan for files.
4. Select a preset and enable PEQ.

See the [configuration documentation](docs/CONFIG_FORMAT.md) for preset formats and limits. To convert other formats, give an AI your EQ settings together with the [AI conversion guide](docs/AI_EQ_CONVERSION.md) provided by this project.

## Warning

This is an Alpha test release. It has only been confirmed usable on one XM5 owned by the author. This does not guarantee successful installation or operation on other devices. Flashing may cause data loss or boot failure, and repairs may be necessary. Recovery after a failed flash has not been verified for this project.

Before flashing, back up your data and keep the official recovery package and instructions.

Excessive EQ gain can cause clipping or distortion, especially when both EQ layers are enabled. Keep the preset's preamp setting and lower it further if necessary.

Thanks to Shanling for their technical support.
