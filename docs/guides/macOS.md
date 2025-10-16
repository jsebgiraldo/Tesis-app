# Setup macOS

## Dependencias
```bash
# Homebrew
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Herramientas
brew update
brew install git python@3.11 cmake ninja dfu-util wireshark qemu
brew install --cask wireshark

# ESP-IDF prerequisites
brew install cmake ninja dfu-util gawk grep coreutils wget
```

## ESP-IDF
```bash
cd $HOME
git clone -b v5.2 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32,esp32c3
```
Añade al shell:
```bash
# zsh
source "$HOME/esp-idf/export.sh"
```

## QEMU (para RISC-V y Xtensa)
- Usar QEMU de Homebrew para RISC-V (ESP32-C3)
- Para Xtensa (ESP32), considerar qemu-xtensa de Espressif si se requiere

## Captura de tráfico
```bash
brew install tshark
sudo chmod +x /Applications/Wireshark.app/Contents/MacOS/dumpcap
```

## Verificación rápida
```bash
python3 --version
idf.py --version
qemu-system-riscv32 --version
```
