#!/bin/bash
# Script de inicio rápido para DLMS-MQTT Bridge

set -e

echo "============================================"
echo "DLMS to MQTT Bridge - Setup & Run"
echo "============================================"
echo ""

# Verificar que estamos en el directorio correcto
if [ ! -f "requirements.txt" ]; then
    echo "Error: Ejecuta este script desde el directorio dlms-bridge/"
    exit 1
fi

# Verificar Python 3
if ! command -v python3 &> /dev/null; then
    echo "Error: Python 3 no está instalado"
    exit 1
fi

# Crear entorno virtual si no existe
if [ ! -d ".venv" ]; then
    echo "📦 Creando entorno virtual..."
    python3 -m venv .venv
    echo "✅ Entorno virtual creado"
else
    echo "✅ Entorno virtual ya existe"
fi

# Activar entorno virtual
echo "🔌 Activando entorno virtual..."
source .venv/bin/activate

# Instalar dependencias
echo "📥 Instalando dependencias..."
pip install --upgrade pip > /dev/null 2>&1
pip install -r requirements.txt

echo "✅ Dependencias instaladas"
echo ""

# Verificar archivo .env
if [ ! -f ".env" ]; then
    echo "⚠️  Archivo .env no encontrado"
    echo "   Copiando .env.example a .env..."
    cp .env.example .env
    echo "   ⚠️  Por favor revisa y ajusta los parámetros en .env"
    echo ""
fi

echo "============================================"
echo "🚀 Iniciando DLMS-MQTT Bridge..."
echo "============================================"
echo ""
echo "Presiona Ctrl+C para detener"
echo ""

# Ejecutar la aplicación
python -m app.main
