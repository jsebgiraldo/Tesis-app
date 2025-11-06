#!/bin/bash
##############################################################################
# Gestión del servicio ThingsBoard Gateway - DLMS Bridge
##############################################################################

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVICE_FILE="tb-gateway-dlms.service"
SERVICE_NAME="tb-gateway-dlms"
SYSTEMD_DIR="/etc/systemd/system"

# Colores
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Funciones auxiliares
info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

success() {
    echo -e "${GREEN}[OK]${NC} $1"
}

warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Verificar que se ejecuta como root para operaciones systemd
check_root() {
    if [[ $EUID -ne 0 ]]; then
        error "Este comando requiere privilegios sudo"
        exit 1
    fi
}

# Instalar servicio
install_service() {
    check_root
    
    info "Instalando servicio $SERVICE_NAME..."
    
    # Copiar archivo de servicio
    cp "$SCRIPT_DIR/$SERVICE_FILE" "$SYSTEMD_DIR/"
    success "Archivo de servicio copiado a $SYSTEMD_DIR/"
    
    # Recargar systemd
    systemctl daemon-reload
    success "systemd recargado"
    
    # Habilitar servicio
    systemctl enable "$SERVICE_NAME"
    success "Servicio habilitado para auto-inicio"
    
    success "✅ Servicio instalado correctamente"
    info "Usa: sudo systemctl start $SERVICE_NAME"
}

# Desinstalar servicio
uninstall_service() {
    check_root
    
    info "Desinstalando servicio $SERVICE_NAME..."
    
    # Detener servicio
    if systemctl is-active --quiet "$SERVICE_NAME"; then
        systemctl stop "$SERVICE_NAME"
        success "Servicio detenido"
    fi
    
    # Deshabilitar servicio
    if systemctl is-enabled --quiet "$SERVICE_NAME"; then
        systemctl disable "$SERVICE_NAME"
        success "Servicio deshabilitado"
    fi
    
    # Eliminar archivo
    if [ -f "$SYSTEMD_DIR/$SERVICE_FILE" ]; then
        rm "$SYSTEMD_DIR/$SERVICE_FILE"
        success "Archivo de servicio eliminado"
    fi
    
    # Recargar systemd
    systemctl daemon-reload
    success "systemd recargado"
    
    success "✅ Servicio desinstalado correctamente"
}

# Iniciar servicio
start_service() {
    check_root
    
    info "Iniciando servicio $SERVICE_NAME..."
    systemctl start "$SERVICE_NAME"
    sleep 2
    
    if systemctl is-active --quiet "$SERVICE_NAME"; then
        success "✅ Servicio iniciado correctamente"
        systemctl status "$SERVICE_NAME" --no-pager -l
    else
        error "❌ El servicio no pudo iniciarse"
        systemctl status "$SERVICE_NAME" --no-pager -l
        exit 1
    fi
}

# Detener servicio
stop_service() {
    check_root
    
    info "Deteniendo servicio $SERVICE_NAME..."
    systemctl stop "$SERVICE_NAME"
    
    if ! systemctl is-active --quiet "$SERVICE_NAME"; then
        success "✅ Servicio detenido correctamente"
    else
        error "❌ El servicio no pudo detenerse"
        exit 1
    fi
}

# Reiniciar servicio
restart_service() {
    check_root
    
    info "Reiniciando servicio $SERVICE_NAME..."
    systemctl restart "$SERVICE_NAME"
    sleep 2
    
    if systemctl is-active --quiet "$SERVICE_NAME"; then
        success "✅ Servicio reiniciado correctamente"
        systemctl status "$SERVICE_NAME" --no-pager -l
    else
        error "❌ El servicio no pudo reiniciarse"
        systemctl status "$SERVICE_NAME" --no-pager -l
        exit 1
    fi
}

# Ver estado
status_service() {
    info "Estado del servicio $SERVICE_NAME:"
    systemctl status "$SERVICE_NAME" --no-pager -l || true
}

# Ver logs
logs_service() {
    info "Logs del servicio $SERVICE_NAME (últimas 50 líneas):"
    journalctl -u "$SERVICE_NAME" -n 50 --no-pager
}

# Seguir logs en tiempo real
follow_logs() {
    info "Siguiendo logs del servicio $SERVICE_NAME (Ctrl+C para detener):"
    journalctl -u "$SERVICE_NAME" -f
}

# Detener servicios antiguos (DLMS anteriores)
stop_old_services() {
    check_root
    
    info "Deteniendo servicios DLMS antiguos..."
    
    OLD_SERVICES=("dlms-mqtt-bridge" "dlms-admin-api")
    
    for service in "${OLD_SERVICES[@]}"; do
        if systemctl is-active --quiet "$service"; then
            systemctl stop "$service"
            success "✓ $service detenido"
        fi
        
        if systemctl is-enabled --quiet "$service" 2>/dev/null; then
            systemctl disable "$service"
            success "✓ $service deshabilitado"
        fi
    done
    
    success "✅ Servicios antiguos detenidos y deshabilitados"
}

# Mostrar ayuda
show_help() {
    cat << EOF
${BLUE}═══════════════════════════════════════════════════════════════════════${NC}
${GREEN}Gestión del servicio ThingsBoard Gateway - DLMS Bridge${NC}
${BLUE}═══════════════════════════════════════════════════════════════════════${NC}

${YELLOW}Uso:${NC}
  $0 <comando>

${YELLOW}Comandos:${NC}
  ${GREEN}install${NC}        Instalar servicio en systemd (requiere sudo)
  ${GREEN}uninstall${NC}      Desinstalar servicio de systemd (requiere sudo)
  ${GREEN}start${NC}          Iniciar servicio (requiere sudo)
  ${GREEN}stop${NC}           Detener servicio (requiere sudo)
  ${GREEN}restart${NC}        Reiniciar servicio (requiere sudo)
  ${GREEN}status${NC}         Ver estado del servicio
  ${GREEN}logs${NC}           Ver últimos logs del servicio
  ${GREEN}follow${NC}         Seguir logs en tiempo real
  ${GREEN}stop-old${NC}       Detener y deshabilitar servicios DLMS antiguos (requiere sudo)
  ${GREEN}help${NC}           Mostrar esta ayuda

${YELLOW}Ejemplos:${NC}
  # Instalar y arrancar servicio por primera vez
  ${GREEN}sudo $0 stop-old${NC}      # Detener servicios anteriores
  ${GREEN}sudo $0 install${NC}       # Instalar nuevo servicio
  ${GREEN}sudo $0 start${NC}         # Iniciar servicio
  ${GREEN}$0 logs${NC}               # Ver logs

  # Operaciones comunes
  ${GREEN}sudo $0 restart${NC}       # Reiniciar servicio
  ${GREEN}$0 status${NC}             # Ver estado
  ${GREEN}$0 follow${NC}             # Seguir logs en vivo

${BLUE}═══════════════════════════════════════════════════════════════════════${NC}
EOF
}

# Menú principal
case "${1:-help}" in
    install)
        install_service
        ;;
    uninstall)
        uninstall_service
        ;;
    start)
        start_service
        ;;
    stop)
        stop_service
        ;;
    restart)
        restart_service
        ;;
    status)
        status_service
        ;;
    logs)
        logs_service
        ;;
    follow)
        follow_logs
        ;;
    stop-old)
        stop_old_services
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        error "Comando desconocido: $1"
        show_help
        exit 1
        ;;
esac
