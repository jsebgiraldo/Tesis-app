#!/bin/bash
# ==============================================================================
# ThingsBoard Gateway - DLMS Connector Setup Script
# ==============================================================================
# Este script instala y configura el ThingsBoard Gateway con el conector DLMS
# personalizado.
#
# Uso:
#   ./setup_gateway.sh [install|configure|start|stop|status]
#
# ==============================================================================

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
GATEWAY_DIR="/etc/thingsboard-gateway"
LOG_DIR="/var/log/thingsboard-gateway"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_DIR="${PROJECT_DIR}/config"
CONNECTOR_DIR="${PROJECT_DIR}/connectors"
VENV_DIR="${PROJECT_DIR}/../venv"

# Functions
print_header() {
    echo -e "${BLUE}===================================================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}===================================================================${NC}"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

print_info() {
    echo -e "${BLUE}ℹ $1${NC}"
}

check_root() {
    if [ "$EUID" -ne 0 ]; then 
        print_error "This command requires root privileges. Please run with sudo."
        exit 1
    fi
}

install_system_dependencies() {
    print_header "Installing System Dependencies"
    
    apt-get update
    apt-get install -y python3-dev python3-pip libglib2.0-dev
    
    print_success "System dependencies installed"
}

install_thingsboard_gateway() {
    print_header "Installing ThingsBoard Gateway"
    
    # Install ThingsBoard Gateway
    pip3 install thingsboard-gateway
    
    print_success "ThingsBoard Gateway installed"
}

create_directories() {
    print_header "Creating Directories"
    
    # Create gateway directories
    mkdir -p "$GATEWAY_DIR"
    mkdir -p "$LOG_DIR"
    mkdir -p "${GATEWAY_DIR}/config"
    mkdir -p "${GATEWAY_DIR}/connectors"
    mkdir -p "${GATEWAY_DIR}/extensions"
    
    print_success "Directories created"
}

copy_configurations() {
    print_header "Copying Configuration Files"
    
    # Copy gateway configuration
    if [ -f "${CONFIG_DIR}/tb_gateway.yaml" ]; then
        cp "${CONFIG_DIR}/tb_gateway.yaml" "${GATEWAY_DIR}/config/"
        print_success "Copied tb_gateway.yaml"
    else
        print_warning "tb_gateway.yaml not found in ${CONFIG_DIR}"
    fi
    
    # Copy connector configuration
    if [ -f "${CONFIG_DIR}/dlms_connector.json" ]; then
        cp "${CONFIG_DIR}/dlms_connector.json" "${GATEWAY_DIR}/config/"
        print_success "Copied dlms_connector.json"
    else
        print_warning "dlms_connector.json not found in ${CONFIG_DIR}"
    fi
    
    # Copy DLMS connector module
    if [ -f "${CONNECTOR_DIR}/dlms_connector.py" ]; then
        cp "${CONNECTOR_DIR}/dlms_connector.py" "${GATEWAY_DIR}/connectors/"
        print_success "Copied dlms_connector.py"
    else
        print_warning "dlms_connector.py not found in ${CONNECTOR_DIR}"
    fi
    
    # Copy dependencies from parent directory
    if [ -f "${PROJECT_DIR}/../dlms_poller_production.py" ]; then
        cp "${PROJECT_DIR}/../dlms_poller_production.py" "${GATEWAY_DIR}/connectors/"
        print_success "Copied dlms_poller_production.py"
    fi
}

set_permissions() {
    print_header "Setting Permissions"
    
    # Get current user
    CURRENT_USER=${SUDO_USER:-$USER}
    
    # Set ownership
    chown -R $CURRENT_USER:$CURRENT_USER "$GATEWAY_DIR"
    chown -R $CURRENT_USER:$CURRENT_USER "$LOG_DIR"
    
    # Set permissions
    chmod -R 755 "$GATEWAY_DIR"
    chmod -R 755 "$LOG_DIR"
    
    print_success "Permissions set for user: $CURRENT_USER"
}

configure_gateway() {
    print_header "Gateway Configuration"
    
    print_info "Configuration file: ${GATEWAY_DIR}/config/tb_gateway.yaml"
    print_info ""
    print_info "Please update the following settings:"
    print_info "1. ThingsBoard host and port"
    print_info "2. Gateway access token (get from ThingsBoard UI)"
    print_info "3. DLMS device configurations in dlms_connector.json"
    print_info ""
    
    read -p "Do you want to edit the configuration now? (y/n): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        ${EDITOR:-nano} "${GATEWAY_DIR}/config/tb_gateway.yaml"
    fi
}

create_systemd_service() {
    print_header "Creating Systemd Service"
    
    cat > /etc/systemd/system/tb-gateway.service << EOF
[Unit]
Description=ThingsBoard IoT Gateway with DLMS Connector
After=network.target
Wants=network-online.target

[Service]
Type=simple
User=${SUDO_USER:-$USER}
Group=${SUDO_USER:-$USER}
WorkingDirectory=${GATEWAY_DIR}
Environment="PYTHONPATH=${GATEWAY_DIR}/connectors:${GATEWAY_DIR}"
ExecStart=/usr/local/bin/thingsboard-gateway --config ${GATEWAY_DIR}/config/tb_gateway.yaml
Restart=on-failure
RestartSec=10s
StandardOutput=journal
StandardError=journal

# Resource limits
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
EOF
    
    systemctl daemon-reload
    print_success "Systemd service created: tb-gateway.service"
}

start_gateway() {
    print_header "Starting Gateway"
    
    systemctl start tb-gateway.service
    sleep 2
    
    if systemctl is-active --quiet tb-gateway.service; then
        print_success "Gateway started successfully"
        systemctl status tb-gateway.service --no-pager
    else
        print_error "Failed to start gateway"
        systemctl status tb-gateway.service --no-pager
        exit 1
    fi
}

stop_gateway() {
    print_header "Stopping Gateway"
    
    systemctl stop tb-gateway.service
    print_success "Gateway stopped"
}

gateway_status() {
    print_header "Gateway Status"
    
    systemctl status tb-gateway.service --no-pager
}

enable_service() {
    print_header "Enabling Service at Boot"
    
    systemctl enable tb-gateway.service
    print_success "Service enabled"
}

show_logs() {
    print_header "Gateway Logs"
    
    journalctl -u tb-gateway.service -f
}

install_full() {
    check_root
    
    print_header "Full Installation"
    
    install_system_dependencies
    install_thingsboard_gateway
    create_directories
    copy_configurations
    set_permissions
    create_systemd_service
    
    print_success "Installation complete!"
    print_info ""
    print_info "Next steps:"
    print_info "1. Configure ThingsBoard: sudo $0 configure"
    print_info "2. Start gateway: sudo $0 start"
    print_info "3. Enable at boot: sudo $0 enable"
    print_info "4. View logs: sudo $0 logs"
}

# Main menu
case "${1:-}" in
    install)
        install_full
        ;;
    configure)
        configure_gateway
        ;;
    start)
        check_root
        start_gateway
        ;;
    stop)
        check_root
        stop_gateway
        ;;
    restart)
        check_root
        stop_gateway
        sleep 1
        start_gateway
        ;;
    status)
        gateway_status
        ;;
    enable)
        check_root
        enable_service
        ;;
    logs)
        show_logs
        ;;
    update-config)
        check_root
        copy_configurations
        print_success "Configurations updated. Restart gateway to apply changes."
        ;;
    *)
        print_header "ThingsBoard Gateway - DLMS Connector Setup"
        echo ""
        echo "Usage: $0 [command]"
        echo ""
        echo "Commands:"
        echo "  install        - Full installation (requires root)"
        echo "  configure      - Configure gateway settings"
        echo "  start          - Start gateway service (requires root)"
        echo "  stop           - Stop gateway service (requires root)"
        echo "  restart        - Restart gateway service (requires root)"
        echo "  status         - Show gateway status"
        echo "  enable         - Enable service at boot (requires root)"
        echo "  logs           - View gateway logs"
        echo "  update-config  - Update configuration files (requires root)"
        echo ""
        echo "Examples:"
        echo "  sudo $0 install"
        echo "  sudo $0 start"
        echo "  $0 status"
        echo "  sudo $0 logs"
        exit 1
        ;;
esac
