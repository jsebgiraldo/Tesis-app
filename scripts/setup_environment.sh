#!/bin/bash
set -euo pipefail

echo "========================================="
echo "  Setup Environment - Tesis IoT SDK"
echo "========================================="

# Verificar Docker
if ! command -v docker &> /dev/null; then
    echo "❌ Docker no encontrado. Instalando..."
    curl -fsSL https://get.docker.com -o get-docker.sh
    sudo sh get-docker.sh
    sudo usermod -aG docker $USER
fi

# Verificar Docker Compose
if ! command -v docker-compose &> /dev/null; then
    echo "❌ Docker Compose no encontrado. Instalando..."
    sudo curl -L "https://github.com/docker/compose/releases/latest/download/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose
    sudo chmod +x /usr/local/bin/docker-compose
fi

# Python dependencies
if [ -f "requirements.txt" ]; then
    echo "📦 Instalando dependencias Python..."
    pip3 install -r requirements.txt
fi

# ESP-IDF (opcional)
if [ ! -d "$HOME/esp/esp-idf" ]; then
    echo "📥 ¿Deseas instalar ESP-IDF? (y/n)"
    read -r response
    if [[ "$response" =~ ^[Yy]$ ]]; then
        mkdir -p ~/esp
        cd ~/esp
        git clone --recursive https://github.com/espressif/esp-idf.git
        cd esp-idf
        ./install.sh esp32c6
        echo "✓ ESP-IDF instalado. Ejecuta: . $HOME/esp/esp-idf/export.sh"
    fi
fi

echo ""
echo "✅ Entorno configurado correctamente"
echo ""
echo "Próximos pasos:"
echo "  1. cd infrastructure/thingsboard/server && docker-compose up -d"
echo "  2. cd protocols/lwm2m/server_leshan && docker-compose up -d"
echo "  3. Ver WORKSPACE_GUIDE.md para más información"
