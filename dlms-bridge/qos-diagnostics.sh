#!/bin/bash
# Script para consultar diagnósticos del QoS Supervisor

SUPERVISOR_SERVICE="qos-supervisor.service"

# Colores
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

show_help() {
    echo "═══════════════════════════════════════════════════════════════"
    echo "  📊 QoS Supervisor - Herramienta de Diagnóstico"
    echo "═══════════════════════════════════════════════════════════════"
    echo ""
    echo "Uso: $0 [comando] [opciones]"
    echo ""
    echo "Comandos:"
    echo "  status         - Estado actual del supervisor"
    echo "  logs [min]     - Ver logs (últimos N minutos, default: 60)"
    echo "  errors [min]   - Ver solo errores (últimos N minutos, default: 60)"
    echo "  actions [min]  - Ver acciones correctivas (últimos N minutos, default: 60)"
    echo "  cycles [n]     - Ver resumen de últimos N ciclos (default: 5)"
    echo "  live           - Seguir logs en tiempo real"
    echo "  stats [hours]  - Estadísticas (últimas N horas, default: 24)"
    echo "  restart        - Reiniciar supervisor"
    echo "  start          - Iniciar supervisor"
    echo "  stop           - Detener supervisor"
    echo ""
    echo "Ejemplos:"
    echo "  $0 status                  # Ver estado actual"
    echo "  $0 logs 30                 # Ver últimos 30 minutos de logs"
    echo "  $0 errors 120              # Ver errores de las últimas 2 horas"
    echo "  $0 live                    # Seguir logs en vivo"
    echo "  $0 stats 48                # Estadísticas de últimas 48 horas"
    echo ""
}

show_status() {
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${CYAN}  📊 Estado del QoS Supervisor${NC}"
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
    echo ""
    
    # Estado del servicio
    if systemctl is-active --quiet $SUPERVISOR_SERVICE; then
        echo -e "Estado:     ${GREEN}✅ ACTIVO${NC}"
    else
        echo -e "Estado:     ${RED}❌ INACTIVO${NC}"
    fi
    
    # Uptime
    uptime=$(systemctl show $SUPERVISOR_SERVICE --property=ActiveEnterTimestamp --value)
    if [ -n "$uptime" ] && [ "$uptime" != "n/a" ]; then
        echo -e "Inicio:     ${uptime}"
    fi
    
    # Último check
    last_check=$(journalctl -u $SUPERVISOR_SERVICE --since "5 minutes ago" -n 1 --no-pager 2>/dev/null | grep "Check #" | tail -1)
    if [ -n "$last_check" ]; then
        echo -e "\nÚltimo check:"
        echo "$last_check" | sed 's/^/  /'
    fi
    
    # Contadores
    echo ""
    echo -e "${CYAN}Últimos 60 minutos:${NC}"
    
    total_checks=$(journalctl -u $SUPERVISOR_SERVICE --since "60 minutes ago" --no-pager 2>/dev/null | grep -c "Check #")
    echo -e "  Checks realizados:    ${total_checks}"
    
    errors=$(journalctl -u $SUPERVISOR_SERVICE --since "60 minutes ago" --no-pager 2>/dev/null | grep -c "❌")
    if [ $errors -gt 0 ]; then
        echo -e "  Errores detectados:   ${RED}${errors}${NC}"
    else
        echo -e "  Errores detectados:   ${GREEN}0${NC}"
    fi
    
    actions=$(journalctl -u $SUPERVISOR_SERVICE --since "60 minutes ago" --no-pager 2>/dev/null | grep -c "⚡")
    if [ $actions -gt 0 ]; then
        echo -e "  Acciones tomadas:     ${YELLOW}${actions}${NC}"
    else
        echo -e "  Acciones tomadas:     ${GREEN}0${NC}"
    fi
    
    echo ""
}

show_logs() {
    minutes=${1:-60}
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${CYAN}  📋 Logs (últimos ${minutes} minutos)${NC}"
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
    echo ""
    
    journalctl -u $SUPERVISOR_SERVICE --since "${minutes} minutes ago" --no-pager | \
        sed "s/❌/${RED}❌${NC}/g" | \
        sed "s/✅/${GREEN}✅${NC}/g" | \
        sed "s/⚠️/${YELLOW}⚠️${NC}/g" | \
        sed "s/⚡/${YELLOW}⚡${NC}/g" | \
        sed "s/ℹ️/${BLUE}ℹ️${NC}/g"
}

show_errors() {
    minutes=${1:-60}
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${CYAN}  ❌ Errores (últimos ${minutes} minutos)${NC}"
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
    echo ""
    
    errors=$(journalctl -u $SUPERVISOR_SERVICE --since "${minutes} minutes ago" --no-pager | grep "❌")
    
    if [ -z "$errors" ]; then
        echo -e "${GREEN}✅ No se detectaron errores${NC}"
    else
        echo "$errors" | sed "s/❌/${RED}❌${NC}/g"
    fi
    echo ""
}

show_actions() {
    minutes=${1:-60}
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${CYAN}  ⚡ Acciones Correctivas (últimos ${minutes} minutos)${NC}"
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
    echo ""
    
    actions=$(journalctl -u $SUPERVISOR_SERVICE --since "${minutes} minutes ago" --no-pager | grep "⚡")
    
    if [ -z "$actions" ]; then
        echo -e "${GREEN}✅ No se requirieron acciones correctivas${NC}"
    else
        echo "$actions" | sed "s/⚡/${YELLOW}⚡${NC}/g"
    fi
    echo ""
}

show_cycles() {
    count=${1:-5}
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${CYAN}  🔄 Últimos ${count} Ciclos Completados${NC}"
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
    echo ""
    
    journalctl -u $SUPERVISOR_SERVICE --no-pager | grep "CICLO.*COMPLETADO" | tail -n $count | \
        sed "s/CICLO/${BLUE}CICLO${NC}/g"
    echo ""
}

show_live() {
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${CYAN}  📡 Logs en Tiempo Real (Ctrl+C para salir)${NC}"
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
    echo ""
    
    journalctl -u $SUPERVISOR_SERVICE -f --no-pager | \
        sed "s/❌/${RED}❌${NC}/g" | \
        sed "s/✅/${GREEN}✅${NC}/g" | \
        sed "s/⚠️/${YELLOW}⚠️${NC}/g" | \
        sed "s/⚡/${YELLOW}⚡${NC}/g" | \
        sed "s/ℹ️/${BLUE}ℹ️${NC}/g"
}

show_stats() {
    hours=${1:-24}
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${CYAN}  📊 Estadísticas (últimas ${hours} horas)${NC}"
    echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
    echo ""
    
    # Total de checks
    total_checks=$(journalctl -u $SUPERVISOR_SERVICE --since "${hours} hours ago" --no-pager | grep -c "Check #")
    echo -e "Checks realizados:        ${BLUE}${total_checks}${NC}"
    
    # Ciclos completados
    cycles=$(journalctl -u $SUPERVISOR_SERVICE --since "${hours} hours ago" --no-pager | grep -c "CICLO.*COMPLETADO")
    echo -e "Ciclos completados:       ${BLUE}${cycles}${NC}"
    
    # Problemas detectados
    errors=$(journalctl -u $SUPERVISOR_SERVICE --since "${hours} hours ago" --no-pager | grep -c "❌")
    if [ $errors -gt 0 ]; then
        echo -e "Problemas detectados:     ${RED}${errors}${NC}"
    else
        echo -e "Problemas detectados:     ${GREEN}0${NC}"
    fi
    
    # Acciones correctivas
    actions=$(journalctl -u $SUPERVISOR_SERVICE --since "${hours} hours ago" --no-pager | grep -c "⚡")
    if [ $actions -gt 0 ]; then
        echo -e "Acciones correctivas:     ${YELLOW}${actions}${NC}"
        
        # Desglose de acciones
        echo ""
        echo -e "${CYAN}Desglose de acciones:${NC}"
        bridge_restarts=$(journalctl -u $SUPERVISOR_SERVICE --since "${hours} hours ago" --no-pager | grep "⚡" | grep -c "dlms-mosquitto-bridge")
        gateway_restarts=$(journalctl -u $SUPERVISOR_SERVICE --since "${hours} hours ago" --no-pager | grep "⚡" | grep -c "thingsboard-gateway")
        mosquitto_restarts=$(journalctl -u $SUPERVISOR_SERVICE --since "${hours} hours ago" --no-pager | grep "⚡" | grep -c "mosquitto\.service")
        
        echo -e "  Bridge reiniciado:      ${bridge_restarts}"
        echo -e "  Gateway reiniciado:     ${gateway_restarts}"
        echo -e "  Mosquitto reiniciado:   ${mosquitto_restarts}"
    else
        echo -e "Acciones correctivas:     ${GREEN}0${NC}"
    fi
    
    # Disponibilidad
    echo ""
    if [ $total_checks -gt 0 ] && [ $errors -gt 0 ]; then
        success=$((total_checks - errors))
        availability=$(awk "BEGIN {printf \"%.2f\", ($success / $total_checks) * 100}")
        
        if (( $(echo "$availability >= 99" | bc -l) )); then
            echo -e "Disponibilidad estimada:  ${GREEN}${availability}%${NC}"
        elif (( $(echo "$availability >= 95" | bc -l) )); then
            echo -e "Disponibilidad estimada:  ${YELLOW}${availability}%${NC}"
        else
            echo -e "Disponibilidad estimada:  ${RED}${availability}%${NC}"
        fi
    elif [ $total_checks -gt 0 ]; then
        echo -e "Disponibilidad estimada:  ${GREEN}100.00%${NC}"
    fi
    
    echo ""
}

restart_supervisor() {
    echo -e "${YELLOW}🔄 Reiniciando QoS Supervisor...${NC}"
    sudo systemctl restart $SUPERVISOR_SERVICE
    sleep 2
    show_status
}

start_supervisor() {
    echo -e "${GREEN}▶️  Iniciando QoS Supervisor...${NC}"
    sudo systemctl start $SUPERVISOR_SERVICE
    sleep 2
    show_status
}

stop_supervisor() {
    echo -e "${RED}⏹️  Deteniendo QoS Supervisor...${NC}"
    sudo systemctl stop $SUPERVISOR_SERVICE
    sleep 1
    show_status
}

# Main
case "${1:-help}" in
    status)
        show_status
        ;;
    logs)
        show_logs "$2"
        ;;
    errors)
        show_errors "$2"
        ;;
    actions)
        show_actions "$2"
        ;;
    cycles)
        show_cycles "$2"
        ;;
    live)
        show_live
        ;;
    stats)
        show_stats "$2"
        ;;
    restart)
        restart_supervisor
        ;;
    start)
        start_supervisor
        ;;
    stop)
        stop_supervisor
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        echo -e "${RED}❌ Comando desconocido: $1${NC}"
        echo ""
        show_help
        exit 1
        ;;
esac
