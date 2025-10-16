#!/bin/bash
# Script para generar visualización de la estructura del SDK

echo "========================================================================="
echo "                    ESTRUCTURA SDK TESIS IoT"
echo "          Arquitectura ISO/IEC 30141:2024 - Wi-Fi HaLow Mesh"
echo "========================================================================="
echo ""

# Función para contar archivos
count_files() {
    find "$1" -type f 2>/dev/null | wc -l
}

# Función para mostrar directorio con contador
show_dir() {
    local dir=$1
    local desc=$2
    local count=$(count_files "$dir")
    printf "%-40s %s (%d archivos)\n" "📁 $dir" "$desc" "$count"
}

echo "🏗️  INFRAESTRUCTURA"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
show_dir "infrastructure/halow_mesh" "Mesh HaLow 802.11s (OpenWRT)"
show_dir "infrastructure/thingsboard" "ThingsBoard Server + Edge"
show_dir "infrastructure/gateways" "Gateways Modbus/DLMS"
echo ""

echo "💻 DISPOSITIVOS FINALES"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
show_dir "devices/end_nodes/esp32c6_lwm2m_smart_meter" "Smart Meter LwM2M"
show_dir "devices/end_nodes/esp32c6_lwm2m_temp" "Temp/Humidity LwM2M"
show_dir "devices/end_nodes/esp32c6_mqtt" "Cliente MQTT (comparativo)"
show_dir "devices/end_nodes/common" "Componentes compartidos"
echo ""

echo "🔌 PROTOCOLOS"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
show_dir "protocols/lwm2m" "Leshan Server + Tests"
show_dir "protocols/mqtt" "Mosquitto Broker + QoS"
show_dir "protocols/modbus" "Simulador + Gateway"
show_dir "protocols/dlms" "DLMS Reader + Adapter"
echo ""

echo "🧪 TESTING Y VALIDACIÓN"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
show_dir "testing/compliance" "ISO 30141, LwM2M conformance"
show_dir "testing/performance" "Latencia, throughput, energía"
show_dir "testing/interoperability" "Tests multi-vendor"
show_dir "testing/integration" "End-to-end scenarios"
echo ""

echo "📊 DATOS Y EVIDENCIAS"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
show_dir "data/captures" "PCAPs, logs, métricas"
show_dir "data/experiments" "Experimentos documentados"
show_dir "data/benchmarks" "Resultados consolidados"
echo ""

echo "🛠️  HERRAMIENTAS"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
show_dir "tools/network" "Análisis mesh, captures"
show_dir "tools/deployment" "Flash, provisioning, FOTA"
show_dir "tools/monitoring" "Health checks, alertas"
show_dir "tools/validation" "Conformidad estándares"
echo ""

echo "📚 DOCUMENTACIÓN"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
show_dir "docs/thesis" "LaTeX + Presentación"
show_dir "docs/guides" "HOWTOs técnicos"
show_dir "docs/standards" "PDFs ISO, IEEE, OMA"
echo ""

echo "📄 ARCHIVOS MAESTROS"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
[ -f "README.md" ] && printf "%-40s %s\n" "✅ README.md" "$(wc -l < README.md) líneas"
[ -f "WORKSPACE_GUIDE.md" ] && printf "%-40s %s\n" "✅ WORKSPACE_GUIDE.md" "$(wc -l < WORKSPACE_GUIDE.md) líneas"
[ -f "ARCHITECTURE_OVERVIEW.md" ] && printf "%-40s %s\n" "✅ ARCHITECTURE_OVERVIEW.md" "$(wc -l < ARCHITECTURE_OVERVIEW.md) líneas"
[ -f "CHANGELOG.md" ] && printf "%-40s %s\n" "✅ CHANGELOG.md" "$(wc -l < CHANGELOG.md) líneas"
[ -f "requirements.txt" ] && printf "%-40s %s\n" "✅ requirements.txt" "$(wc -l < requirements.txt) paquetes"
echo ""

echo "📈 ESTADÍSTICAS"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
total_dirs=$(find . -type d | wc -l)
total_files=$(find . -type f | wc -l)
total_code_c=$(find . -name "*.c" -o -name "*.h" | wc -l)
total_code_py=$(find . -name "*.py" | wc -l)
total_docs=$(find . -name "*.md" | wc -l)

echo "Directorios totales:       $total_dirs"
echo "Archivos totales:          $total_files"
echo "Archivos C/H (firmware):   $total_code_c"
echo "Scripts Python:            $total_code_py"
echo "Documentos Markdown:       $total_docs"
echo ""

echo "🎯 PRÓXIMOS PASOS"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "1. Leer:    cat WORKSPACE_GUIDE.md"
echo "2. Setup:   ./scripts/setup_environment.sh"
echo "3. Infra:   cd infrastructure/thingsboard/server && docker-compose up -d"
echo "4. Build:   cd devices/end_nodes/esp32c6_lwm2m_temp && idf.py build"
echo "5. Guías:   ls -la docs/guides/"
echo ""
echo "========================================================================="
