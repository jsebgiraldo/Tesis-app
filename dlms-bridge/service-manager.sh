#!/bin/bash

# Script para gestionar el servicio DLMS Multi-Meter Bridge (ESCALABLE)
# Gestiona múltiples medidores con un solo servicio
# Permite instalar, iniciar, detener, reiniciar y monitorear el polling

set -e

# Colores
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m' # No Color

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}⚠${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

print_info() {
    echo -e "${BLUE}ℹ${NC} $1"
}

print_highlight() {
    echo -e "${CYAN}★${NC} $1"
}

# Directorio del script
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
SERVICE_FILE="/tmp/dlms-multi-meter.service"
SERVICE_NAME="dlms-multi-meter.service"
SYSTEMD_PATH="/etc/systemd/system/$SERVICE_NAME"

# Servicios relacionados
API_SERVICE="dlms-admin-api.service"
DASHBOARD_SERVICE="dlms-dashboard.service"

echo "╔══════════════════════════════════════════════════════════════════════╗"
echo "║     DLMS Multi-Meter Bridge - Service Manager (ESCALABLE) ⭐        ║"
echo "╔══════════════════════════════════════════════════════════════════════╗"
echo ""

# Función para instalar el servicio
install_service() {
    print_info "Instalando servicio systemd..."
    echo ""
    
    # Verificar que existe el archivo de servicio
    if [ ! -f "$SERVICE_FILE" ]; then
        print_error "No se encontró el archivo de servicio: $SERVICE_FILE"
        exit 1
    fi
    
    # Copiar archivo de servicio
    print_info "Copiando archivo de servicio a /etc/systemd/system/..."
    sudo cp "$SERVICE_FILE" "$SYSTEMD_PATH"
    
    # Recargar systemd
    print_info "Recargando systemd daemon..."
    sudo systemctl daemon-reload
    
    # Habilitar servicio para inicio automático
    print_info "Habilitando servicio para inicio automático..."
    sudo systemctl enable "$SERVICE_NAME"
    
    echo ""
    print_success "Servicio instalado exitosamente!"
    echo ""
    print_info "Para iniciar el servicio, ejecuta:"
    echo "    sudo systemctl start $SERVICE_NAME"
    echo ""
}

# Función para desinstalar el servicio
uninstall_service() {
    print_info "Desinstalando servicio..."
    echo ""
    
    # Detener servicio si está corriendo
    if systemctl is-active --quiet "$SERVICE_NAME"; then
        print_info "Deteniendo servicio..."
        sudo systemctl stop "$SERVICE_NAME"
    fi
    
    # Deshabilitar servicio
    if systemctl is-enabled --quiet "$SERVICE_NAME" 2>/dev/null; then
        print_info "Deshabilitando servicio..."
        sudo systemctl disable "$SERVICE_NAME"
    fi
    
    # Eliminar archivo de servicio
    if [ -f "$SYSTEMD_PATH" ]; then
        print_info "Eliminando archivo de servicio..."
        sudo rm "$SYSTEMD_PATH"
    fi
    
    # Recargar systemd
    print_info "Recargando systemd daemon..."
    sudo systemctl daemon-reload
    
    echo ""
    print_success "Servicio desinstalado exitosamente!"
    echo ""
}

# Función para iniciar el servicio
start_service() {
    print_info "Iniciando servicio..."
    sudo systemctl start "$SERVICE_NAME"
    sleep 2
    
    if systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "Servicio iniciado exitosamente!"
        echo ""
        show_status
    else
        print_error "Error al iniciar el servicio"
        echo ""
        print_info "Ver logs con: sudo journalctl -u $SERVICE_NAME -f"
    fi
}

# Función para detener el servicio
stop_service() {
    print_info "Deteniendo servicio..."
    sudo systemctl stop "$SERVICE_NAME"
    sleep 1
    
    if ! systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "Servicio detenido exitosamente!"
    else
        print_error "Error al detener el servicio"
    fi
}

# Función para reiniciar el servicio
restart_service() {
    print_info "Reiniciando servicio..."
    sudo systemctl restart "$SERVICE_NAME"
    sleep 2
    
    if systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "Servicio reiniciado exitosamente!"
        echo ""
        show_status
    else
        print_error "Error al reiniciar el servicio"
    fi
}

# Función para mostrar el estado
show_status() {
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "ESTADO DEL SERVICIO"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    sudo systemctl status "$SERVICE_NAME" --no-pager || true
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
}

# Función para ver los logs
show_logs() {
    local lines=${1:-50}
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "LOGS DEL SERVICIO (últimas $lines líneas)"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    sudo journalctl -u "$SERVICE_NAME" -n "$lines" --no-pager
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    print_info "Para ver logs en tiempo real: sudo journalctl -u $SERVICE_NAME -f"
}

# Función para seguir los logs en tiempo real
follow_logs() {
    print_info "Mostrando logs en tiempo real (Ctrl+C para salir)..."
    echo ""
    sudo journalctl -u "$SERVICE_NAME" -f
}

# Función para ver el polling en tiempo real (solo datos de medidores)
watch_polling() {
    print_highlight "Monitoreando POLLING de medidores en tiempo real..."
    print_info "Mostrando lecturas de voltaje, corriente, frecuencia, potencia y energía"
    print_info "Presiona Ctrl+C para salir"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    
    # Filtrar solo las líneas con datos de mediciones (las que tienen V: o | V:)
    sudo journalctl -u "$SERVICE_NAME" -f --no-pager | grep --line-buffered "| V:"
}

# Función para ver estadísticas del sistema
show_stats() {
    print_highlight "Estadísticas del sistema multi-meter..."
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    
    # Buscar el último reporte de sistema
    sudo journalctl -u "$SERVICE_NAME" --no-pager | grep -A 10 "SYSTEM STATUS REPORT" | tail -20
    
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    print_info "Los reportes completos se generan cada 60 segundos"
    print_info "Para ver reportes en tiempo real: $0 follow"
}

# Función para ver logs de un medidor específico
show_meter_logs() {
    local meter_id=$1
    local lines=${2:-50}
    
    if [ -z "$meter_id" ]; then
        print_error "Debes especificar el ID del medidor"
        echo ""
        print_info "Uso: $0 meter <meter_id> [lines]"
        print_info "Ejemplo: $0 meter 1 100"
        return 1
    fi
    
    print_highlight "Logs del Medidor $meter_id (últimas $lines líneas)..."
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    
    sudo journalctl -u "$SERVICE_NAME" --no-pager | grep "Meter\[$meter_id:" | tail -n "$lines"
    
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    print_info "Para ver en tiempo real: sudo journalctl -u $SERVICE_NAME -f | grep 'Meter\[$meter_id:'"
}

# Función para mostrar todos los servicios del sistema
show_all_services() {
    print_highlight "Estado de TODOS los servicios del sistema DLMS..."
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "1. SERVICIO MULTI-METER (Lectura de medidores)"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    
    if systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "dlms-multi-meter.service: ACTIVO"
        # Mostrar info básica
        sudo systemctl status "$SERVICE_NAME" --no-pager | head -12
    else
        print_warning "dlms-multi-meter.service: INACTIVO"
    fi
    
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "2. SERVICIO API (Backend REST)"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    
    if systemctl is-active --quiet "$API_SERVICE"; then
        print_success "$API_SERVICE: ACTIVO"
        sudo systemctl status "$API_SERVICE" --no-pager | head -8
    else
        print_warning "$API_SERVICE: INACTIVO"
    fi
    
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "3. SERVICIO DASHBOARD (Web UI)"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    
    if systemctl is-active --quiet "$DASHBOARD_SERVICE"; then
        print_success "$DASHBOARD_SERVICE: ACTIVO"
        sudo systemctl status "$DASHBOARD_SERVICE" --no-pager | head -8
    else
        print_warning "$DASHBOARD_SERVICE: INACTIVO"
    fi
    
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    print_info "Dashboard disponible en: http://localhost:8501"
    print_info "API disponible en: http://localhost:8000"
}

# Función para iniciar todos los servicios
start_all() {
    print_highlight "Iniciando TODOS los servicios del sistema..."
    echo ""
    
    # Iniciar servicio principal
    print_info "Iniciando servicio multi-meter..."
    sudo systemctl start "$SERVICE_NAME"
    
    # Esperar un poco
    sleep 2
    
    # Iniciar API
    print_info "Iniciando API..."
    sudo systemctl start "$API_SERVICE" 2>/dev/null || print_warning "API no disponible"
    
    # Iniciar Dashboard
    print_info "Iniciando Dashboard..."
    sudo systemctl start "$DASHBOARD_SERVICE" 2>/dev/null || print_warning "Dashboard no disponible"
    
    sleep 2
    echo ""
    print_success "Servicios iniciados!"
    echo ""
    show_all_services
}

# Función para detener todos los servicios
stop_all() {
    print_highlight "Deteniendo TODOS los servicios del sistema..."
    echo ""
    
    print_info "Deteniendo Dashboard..."
    sudo systemctl stop "$DASHBOARD_SERVICE" 2>/dev/null || true
    
    print_info "Deteniendo API..."
    sudo systemctl stop "$API_SERVICE" 2>/dev/null || true
    
    print_info "Deteniendo servicio multi-meter..."
    sudo systemctl stop "$SERVICE_NAME"
    
    echo ""
    print_success "Todos los servicios detenidos!"
}

# Verificar si el servicio está instalado
is_installed() {
    [ -f "$SYSTEMD_PATH" ]
}

# Mostrar menú de ayuda
show_help() {
    echo "Uso: $0 [comando] [opciones]"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "COMANDOS DE INSTALACIÓN"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  install       - Instalar el servicio systemd"
    echo "  uninstall     - Desinstalar el servicio systemd"
    echo "  enable        - Habilitar inicio automático"
    echo "  disable       - Deshabilitar inicio automático"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "COMANDOS DE CONTROL"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  start         - Iniciar el servicio multi-meter"
    echo "  stop          - Detener el servicio multi-meter"
    echo "  restart       - Reiniciar el servicio multi-meter"
    echo "  start-all     - Iniciar TODOS los servicios (multi-meter, API, dashboard)"
    echo "  stop-all      - Detener TODOS los servicios"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "COMANDOS DE MONITOREO (⭐ NUEVOS)"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  status        - Estado del servicio multi-meter"
    echo "  all           - Estado de TODOS los servicios"
    echo "  logs [n]      - Últimos logs (default: 50 líneas)"
    echo "  follow        - Seguir todos los logs en tiempo real"
    echo "  watch         - ⭐ Ver POLLING de medidores en tiempo real"
    echo "  stats         - ⭐ Ver estadísticas del sistema (reportes cada 60s)"
    echo "  meter <id>    - ⭐ Ver logs de un medidor específico"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "EJEMPLOS"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  $0 install            # Instalar servicio"
    echo "  $0 start-all          # Iniciar todo el sistema"
    echo "  $0 watch              # Ver polling en tiempo real (RECOMENDADO ⭐)"
    echo "  $0 stats              # Ver estadísticas de rendimiento"
    echo "  $0 meter 1            # Ver logs solo del medidor 1"
    echo "  $0 logs 100           # Ver últimas 100 líneas"
    echo "  $0 all                # Ver estado de todos los servicios"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "URLS DEL SISTEMA"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  Dashboard:  http://localhost:8501"
    echo "  API:        http://localhost:8000"
    echo "  API Docs:   http://localhost:8000/docs"
    echo ""
}

# Procesar comando
COMMAND=${1:-}

case "$COMMAND" in
    install)
        install_service
        ;;
    uninstall)
        uninstall_service
        ;;
    start)
        if ! is_installed; then
            print_error "Servicio no instalado. Ejecuta: $0 install"
            exit 1
        fi
        start_service
        ;;
    stop)
        if ! is_installed; then
            print_error "Servicio no instalado."
            exit 1
        fi
        stop_service
        ;;
    restart)
        if ! is_installed; then
            print_error "Servicio no instalado. Ejecuta: $0 install"
            exit 1
        fi
        restart_service
        ;;
    status)
        if ! is_installed; then
            print_error "Servicio no instalado."
            exit 1
        fi
        show_status
        ;;
    logs)
        if ! is_installed; then
            print_error "Servicio no instalado."
            exit 1
        fi
        LINES=${2:-50}
        show_logs "$LINES"
        ;;
    follow)
        if ! is_installed; then
            print_error "Servicio no instalado."
            exit 1
        fi
        follow_logs
        ;;
    watch)
        if ! is_installed; then
            print_error "Servicio no instalado."
            exit 1
        fi
        watch_polling
        ;;
    stats)
        if ! is_installed; then
            print_error "Servicio no instalado."
            exit 1
        fi
        show_stats
        ;;
    meter)
        if ! is_installed; then
            print_error "Servicio no instalado."
            exit 1
        fi
        METER_ID=$2
        LINES=${3:-50}
        show_meter_logs "$METER_ID" "$LINES"
        ;;
    all)
        show_all_services
        ;;
    start-all)
        start_all
        ;;
    stop-all)
        stop_all
        ;;
    enable)
        if ! is_installed; then
            print_error "Servicio no instalado. Ejecuta: $0 install"
            exit 1
        fi
        print_info "Habilitando inicio automático..."
        sudo systemctl enable "$SERVICE_NAME"
        print_success "Servicio habilitado para inicio automático"
        ;;
    disable)
        if ! is_installed; then
            print_error "Servicio no instalado."
            exit 1
        fi
        print_info "Deshabilitando inicio automático..."
        sudo systemctl disable "$SERVICE_NAME"
        print_success "Inicio automático deshabilitado"
        ;;
    help|--help|-h)
        show_help
        ;;
    "")
        # Si no se especifica comando, mostrar estado completo si está instalado
        if is_installed; then
            show_all_services
        else
            print_warning "Servicio no instalado."
            echo ""
            show_help
        fi
        ;;
    *)
        print_error "Comando desconocido: $COMMAND"
        echo ""
        show_help
        exit 1
        ;;
esac

echo ""
echo "╚══════════════════════════════════════════════════════════════════════╝"
