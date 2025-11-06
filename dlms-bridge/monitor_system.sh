#!/bin/bash
# Script de Monitoreo del Sistema DLMS Multi-Meter
# Muestra el estado de cada componente del sistema

COLOR_RESET="\033[0m"
COLOR_GREEN="\033[0;32m"
COLOR_RED="\033[0;31m"
COLOR_YELLOW="\033[1;33m"
COLOR_BLUE="\033[0;34m"
COLOR_CYAN="\033[0;36m"
COLOR_MAGENTA="\033[0;35m"

# Función para imprimir encabezado
print_header() {
    echo -e "\n${COLOR_CYAN}═══════════════════════════════════════════════════════════════════${COLOR_RESET}"
    echo -e "${COLOR_CYAN}  $1${COLOR_RESET}"
    echo -e "${COLOR_CYAN}═══════════════════════════════════════════════════════════════════${COLOR_RESET}\n"
}

# Función para imprimir subsección
print_subsection() {
    echo -e "\n${COLOR_MAGENTA}▶ $1${COLOR_RESET}"
    echo -e "${COLOR_MAGENTA}───────────────────────────────────────────────────────────────────${COLOR_RESET}"
}

clear
echo -e "${COLOR_BLUE}"
echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║          🔍 MONITOR DEL SISTEMA DLMS MULTI-METER 🔍            ║"
echo "║                  Estado en Tiempo Real                          ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo -e "${COLOR_RESET}"
echo -e "Fecha: $(date '+%Y-%m-%d %H:%M:%S')\n"

# ============================================================================
# 1. ESTADO DEL SERVICIO PRINCIPAL (MultiMeterBridge)
# ============================================================================
print_header "1️⃣  SERVICIO PRINCIPAL: dlms-multi-meter.service"

SERVICE_STATUS=$(systemctl is-active dlms-multi-meter.service 2>/dev/null)
SERVICE_ENABLED=$(systemctl is-enabled dlms-multi-meter.service 2>/dev/null)

if [ "$SERVICE_STATUS" = "active" ]; then
    echo -e "   Estado: ${COLOR_GREEN}✅ ACTIVO${COLOR_RESET}"
else
    echo -e "   Estado: ${COLOR_RED}❌ INACTIVO ($SERVICE_STATUS)${COLOR_RESET}"
fi

if [ "$SERVICE_ENABLED" = "enabled" ]; then
    echo -e "   Auto-start: ${COLOR_GREEN}✅ HABILITADO${COLOR_RESET}"
else
    echo -e "   Auto-start: ${COLOR_YELLOW}⚠️  DESHABILITADO${COLOR_RESET}"
fi

# Información del proceso
if [ "$SERVICE_STATUS" = "active" ]; then
    PID=$(systemctl show -p MainPID --value dlms-multi-meter.service)
    if [ "$PID" != "0" ]; then
        UPTIME=$(ps -p $PID -o etime= 2>/dev/null | xargs)
        MEM=$(ps -p $PID -o rss= 2>/dev/null | awk '{printf "%.1f MB", $1/1024}')
        CPU=$(ps -p $PID -o %cpu= 2>/dev/null | xargs)
        
        echo -e "   PID: ${COLOR_GREEN}$PID${COLOR_RESET}"
        echo -e "   Uptime: ${COLOR_GREEN}$UPTIME${COLOR_RESET}"
        echo -e "   Memoria: ${COLOR_GREEN}$MEM${COLOR_RESET}"
        echo -e "   CPU: ${COLOR_GREEN}${CPU}%${COLOR_RESET}"
    fi
fi

# Restart counter
RESTART_COUNT=$(systemctl show dlms-multi-meter.service -p NRestarts --value 2>/dev/null)
if [ ! -z "$RESTART_COUNT" ] && [ "$RESTART_COUNT" -gt 0 ]; then
    if [ "$RESTART_COUNT" -gt 10 ]; then
        echo -e "   Reinicios: ${COLOR_RED}⚠️  $RESTART_COUNT (ALTO)${COLOR_RESET}"
    else
        echo -e "   Reinicios: ${COLOR_YELLOW}$RESTART_COUNT${COLOR_RESET}"
    fi
fi

# ============================================================================
# 2. MULTIMETERBRIDGE - Gestor Principal
# ============================================================================
print_header "2️⃣  MULTIMETERBRIDGE - Gestor Principal MQTT"

if [ "$SERVICE_STATUS" = "active" ]; then
    # Buscar información de MQTT en logs recientes
    print_subsection "Conexión MQTT"
    
    MQTT_CONNECTED=$(sudo journalctl -u dlms-multi-meter.service --since "5 minutes ago" | grep -i "MQTT Connected" | tail -1)
    if [ ! -z "$MQTT_CONNECTED" ]; then
        CLIENT_ID=$(echo "$MQTT_CONNECTED" | grep -oP 'client_id: \K[^)]+')
        TIMESTAMP=$(echo "$MQTT_CONNECTED" | awk '{print $1, $2, $3}')
        echo -e "   ✅ Conectado a MQTT"
        echo -e "   Client ID: ${COLOR_GREEN}$CLIENT_ID${COLOR_RESET}"
        echo -e "   Última conexión: $TIMESTAMP"
    else
        MQTT_ERROR=$(sudo journalctl -u dlms-multi-meter.service --since "5 minutes ago" | grep -i "mqtt.*error\|mqtt.*fail" | tail -1)
        if [ ! -z "$MQTT_ERROR" ]; then
            echo -e "   ${COLOR_RED}❌ Error de conexión MQTT${COLOR_RESET}"
            echo -e "   $MQTT_ERROR"
        else
            echo -e "   ${COLOR_YELLOW}⚠️  Sin información de MQTT reciente${COLOR_RESET}"
        fi
    fi
    
    # Estadísticas de publicación MQTT
    print_subsection "Estadísticas MQTT (últimos 5 min)"
    
    PUBLISH_SUCCESS=$(sudo journalctl -u dlms-multi-meter.service --since "5 minutes ago" | grep -c "Published.*ThingsBoard" 2>/dev/null || echo "0")
    PUBLISH_FAILED=$(sudo journalctl -u dlms-multi-meter.service --since "5 minutes ago" | grep -c "Failed.*publish\|MQTT.*error" 2>/dev/null || echo "0")
    
    echo -e "   Publicaciones exitosas: ${COLOR_GREEN}$PUBLISH_SUCCESS${COLOR_RESET}"
    echo -e "   Publicaciones fallidas: ${COLOR_RED}$PUBLISH_FAILED${COLOR_RESET}"
    
    if [ "$PUBLISH_SUCCESS" -gt 0 ]; then
        SUCCESS_RATE=$((PUBLISH_SUCCESS * 100 / (PUBLISH_SUCCESS + PUBLISH_FAILED)))
        if [ "$SUCCESS_RATE" -ge 95 ]; then
            echo -e "   Tasa de éxito: ${COLOR_GREEN}${SUCCESS_RATE}%${COLOR_RESET}"
        elif [ "$SUCCESS_RATE" -ge 80 ]; then
            echo -e "   Tasa de éxito: ${COLOR_YELLOW}${SUCCESS_RATE}%${COLOR_RESET}"
        else
            echo -e "   Tasa de éxito: ${COLOR_RED}${SUCCESS_RATE}% (BAJO)${COLOR_RESET}"
        fi
    fi
    
    # Código 7 (NOT_AUTHORIZED)
    CODE7_COUNT=$(sudo journalctl -u dlms-multi-meter.service --since "1 hour ago" | grep -c "rc=7\|NOT_AUTHORIZED" 2>/dev/null || echo "0")
    if [ "$CODE7_COUNT" -gt 0 ]; then
        echo -e "   ${COLOR_RED}⚠️  Errores código 7 (última hora): $CODE7_COUNT${COLOR_RESET}"
    else
        echo -e "   Errores código 7: ${COLOR_GREEN}0 ✅${COLOR_RESET}"
    fi
    
else
    echo -e "   ${COLOR_RED}❌ Servicio no activo - No hay datos disponibles${COLOR_RESET}"
fi

# ============================================================================
# 3. METERWORKER - Workers de Lectura de Medidores
# ============================================================================
print_header "3️⃣  METERWORKER(S) - Lectura de Medidores"

if [ "$SERVICE_STATUS" = "active" ]; then
    print_subsection "Estado de Conexión DLMS"
    
    # Verificar conexión al medidor
    DLMS_CONNECTED=$(sudo journalctl -u dlms-multi-meter.service --since "5 minutes ago" | grep -i "Connected to DLMS\|Conexión DLMS establecida" | tail -1)
    if [ ! -z "$DLMS_CONNECTED" ]; then
        echo -e "   ✅ Conectado al medidor DLMS"
        TIMESTAMP=$(echo "$DLMS_CONNECTED" | awk '{print $1, $2, $3}')
        echo -e "   Última conexión: $TIMESTAMP"
    else
        echo -e "   ${COLOR_YELLOW}⚠️  Sin confirmación de conexión reciente${COLOR_RESET}"
    fi
    
    # Verificar modo de operación
    OPTIMIZED=$(sudo journalctl -u dlms-multi-meter.service --since "10 minutes ago" | grep -i "Modo OPTIMIZADO\|OPTIMIZED" | tail -1)
    if [ ! -z "$OPTIMIZED" ]; then
        echo -e "   Modo: ${COLOR_GREEN}⚡ OPTIMIZADO (Caché activo)${COLOR_RESET}"
    fi
    
    print_subsection "Lecturas Recientes (últimos 30 segundos)"
    
    # Mostrar últimas 5 lecturas con telemetría
    RECENT_READINGS=$(sudo journalctl -u dlms-multi-meter.service --since "30 seconds ago" | grep "| V:" | tail -5)
    if [ ! -z "$RECENT_READINGS" ]; then
        echo "$RECENT_READINGS" | while read line; do
            echo -e "   ${COLOR_GREEN}$line${COLOR_RESET}"
        done
        
        # Calcular latencia promedio
        AVG_LATENCY=$(echo "$RECENT_READINGS" | grep -oP '\(\K[0-9.]+(?=s\))' | awk '{sum+=$1; n++} END {if(n>0) printf "%.2f", sum/n}')
        if [ ! -z "$AVG_LATENCY" ]; then
            echo -e "\n   Latencia promedio: ${COLOR_GREEN}${AVG_LATENCY}s${COLOR_RESET}"
        fi
    else
        echo -e "   ${COLOR_YELLOW}⚠️  No hay lecturas recientes (últimos 30s)${COLOR_RESET}"
    fi
    
    print_subsection "Errores y Recuperación"
    
    # Errores HDLC
    HDLC_ERRORS=$(sudo journalctl -u dlms-multi-meter.service --since "1 hour ago" | grep -c "Invalid HDLC\|HDLC.*error" 2>/dev/null || echo "0")
    if [ "$HDLC_ERRORS" -gt 0 ]; then
        echo -e "   ${COLOR_RED}⚠️  Errores HDLC (última hora): $HDLC_ERRORS${COLOR_RESET}"
    else
        echo -e "   Errores HDLC: ${COLOR_GREEN}0 ✅${COLOR_RESET}"
    fi
    
    # Recuperaciones exitosas
    RECOVERIES=$(sudo journalctl -u dlms-multi-meter.service --since "1 hour ago" | grep -c "Recuperación exitosa\|Recovery successful" 2>/dev/null || echo "0")
    if [ "$RECOVERIES" -gt 0 ]; then
        echo -e "   Auto-recuperaciones: ${COLOR_GREEN}$RECOVERIES${COLOR_RESET}"
    fi
    
    # Reconexiones
    RECONNECTS=$(sudo journalctl -u dlms-multi-meter.service --since "1 hour ago" | grep -c "Reconectando\|Reconnecting" 2>/dev/null || echo "0")
    if [ "$RECONNECTS" -gt 5 ]; then
        echo -e "   ${COLOR_RED}⚠️  Reconexiones (última hora): $RECONNECTS (ALTO)${COLOR_RESET}"
    elif [ "$RECONNECTS" -gt 0 ]; then
        echo -e "   Reconexiones: ${COLOR_YELLOW}$RECONNECTS${COLOR_RESET}"
    else
        echo -e "   Reconexiones: ${COLOR_GREEN}0 ✅${COLOR_RESET}"
    fi
    
else
    echo -e "   ${COLOR_RED}❌ Servicio no activo - No hay datos disponibles${COLOR_RESET}"
fi

# ============================================================================
# 4. MONITOREO - Sistema de Alertas y Métricas
# ============================================================================
print_header "4️⃣  SISTEMA DE MONITOREO - Alertas y Métricas"

if [ "$SERVICE_STATUS" = "active" ]; then
    print_subsection "Alertas Activas (últimos 10 min)"
    
    ALERTS=$(sudo journalctl -u dlms-multi-meter.service --since "10 minutes ago" | grep -i "ALERTA\|ALERT\|WARNING.*rate" | tail -5)
    if [ ! -z "$ALERTS" ]; then
        echo "$ALERTS" | while read line; do
            echo -e "   ${COLOR_YELLOW}⚠️  $line${COLOR_RESET}"
        done
    else
        echo -e "   ${COLOR_GREEN}✅ Sin alertas recientes${COLOR_RESET}"
    fi
    
    print_subsection "Métricas de Rendimiento (última hora)"
    
    # Calcular ciclos de lectura
    CYCLES=$(sudo journalctl -u dlms-multi-meter.service --since "1 hour ago" | grep -c "| V:" 2>/dev/null || echo "0")
    echo -e "   Ciclos de lectura: ${COLOR_GREEN}$CYCLES${COLOR_RESET}"
    
    # Estimar tasa de éxito
    ERRORS=$(sudo journalctl -u dlms-multi-meter.service --since "1 hour ago" | grep -c "error\|Error\|ERROR\|Failed" 2>/dev/null || echo "0")
    if [ "$CYCLES" -gt 0 ]; then
        SUCCESS_RATE=$((($CYCLES - $ERRORS) * 100 / $CYCLES))
        if [ "$SUCCESS_RATE" -ge 95 ]; then
            echo -e "   Tasa de éxito estimada: ${COLOR_GREEN}${SUCCESS_RATE}%${COLOR_RESET}"
        elif [ "$SUCCESS_RATE" -ge 80 ]; then
            echo -e "   Tasa de éxito estimada: ${COLOR_YELLOW}${SUCCESS_RATE}%${COLOR_RESET}"
        else
            echo -e "   Tasa de éxito estimada: ${COLOR_RED}${SUCCESS_RATE}% (BAJO)${COLOR_RESET}"
        fi
    fi
    
    print_subsection "Limpieza de Buffer (BufferCleaner)"
    
    BUFFER_CLEANINGS=$(sudo journalctl -u dlms-multi-meter.service --since "1 hour ago" | grep -c "🧹\|Buffer.*clean\|Drained" 2>/dev/null || echo "0")
    if [ "$BUFFER_CLEANINGS" -gt 0 ]; then
        echo -e "   Limpiezas de buffer: ${COLOR_GREEN}$BUFFER_CLEANINGS${COLOR_RESET}"
    else
        echo -e "   Limpiezas de buffer: ${COLOR_GREEN}0 (no necesarias)${COLOR_RESET}"
    fi
    
else
    echo -e "   ${COLOR_RED}❌ Servicio no activo - No hay datos disponibles${COLOR_RESET}"
fi

# ============================================================================
# 5. CONECTIVIDAD Y RED
# ============================================================================
print_header "5️⃣  CONECTIVIDAD Y RED"

print_subsection "Medidor DLMS"

# Verificar conectividad del medidor
METER_IP="192.168.1.127"
METER_PORT="3333"

if timeout 2 bash -c "echo > /dev/tcp/$METER_IP/$METER_PORT" 2>/dev/null; then
    echo -e "   Estado: ${COLOR_GREEN}✅ ALCANZABLE${COLOR_RESET}"
    echo -e "   IP: $METER_IP:$METER_PORT"
else
    echo -e "   Estado: ${COLOR_RED}❌ NO ALCANZABLE${COLOR_RESET}"
    echo -e "   IP: $METER_IP:$METER_PORT"
fi

print_subsection "ThingsBoard MQTT Broker"

MQTT_HOST="localhost"
MQTT_PORT="1883"

if timeout 2 bash -c "echo > /dev/tcp/$MQTT_HOST/$MQTT_PORT" 2>/dev/null; then
    echo -e "   Estado: ${COLOR_GREEN}✅ ALCANZABLE${COLOR_RESET}"
    echo -e "   Host: $MQTT_HOST:$MQTT_PORT"
    
    # Contar conexiones MQTT activas
    MQTT_CONNECTIONS=$(ss -tn | grep ":1883" | grep ESTAB | wc -l)
    echo -e "   Conexiones activas: ${COLOR_GREEN}$MQTT_CONNECTIONS${COLOR_RESET}"
    
    if [ "$MQTT_CONNECTIONS" -gt 1 ]; then
        echo -e "   ${COLOR_YELLOW}⚠️  Múltiples conexiones MQTT detectadas (posible conflicto)${COLOR_RESET}"
    fi
else
    echo -e "   Estado: ${COLOR_RED}❌ NO ALCANZABLE${COLOR_RESET}"
    echo -e "   Host: $MQTT_HOST:$MQTT_PORT"
fi

# ============================================================================
# 6. OTROS SERVICIOS
# ============================================================================
print_header "6️⃣  OTROS SERVICIOS DEL SISTEMA"

print_subsection "Dashboard Streamlit"
DASHBOARD_STATUS=$(systemctl is-active dlms-dashboard.service 2>/dev/null || echo "inactive")
if [ "$DASHBOARD_STATUS" = "active" ]; then
    echo -e "   dlms-dashboard.service: ${COLOR_GREEN}✅ ACTIVO${COLOR_RESET}"
    echo -e "   URL: http://localhost:8501"
else
    echo -e "   dlms-dashboard.service: ${COLOR_YELLOW}❌ INACTIVO${COLOR_RESET}"
fi

print_subsection "Admin API"
ADMIN_STATUS=$(systemctl is-active dlms-admin-api.service 2>/dev/null || echo "inactive")
if [ "$ADMIN_STATUS" = "active" ]; then
    echo -e "   dlms-admin-api.service: ${COLOR_GREEN}✅ ACTIVO${COLOR_RESET}"
else
    echo -e "   dlms-admin-api.service: ${COLOR_YELLOW}❌ INACTIVO (recomendado)${COLOR_RESET}"
fi

# ============================================================================
# 7. RESUMEN Y RECOMENDACIONES
# ============================================================================
print_header "7️⃣  RESUMEN Y RECOMENDACIONES"

ISSUES=0

if [ "$SERVICE_STATUS" != "active" ]; then
    echo -e "${COLOR_RED}❌ CRÍTICO: Servicio principal no está activo${COLOR_RESET}"
    ISSUES=$((ISSUES + 1))
fi

if [ ! -z "$CODE7_COUNT" ] && [ "$CODE7_COUNT" -gt 5 ]; then
    echo -e "${COLOR_RED}⚠️  PROBLEMA: Múltiples errores código 7 (conflicto MQTT)${COLOR_RESET}"
    ISSUES=$((ISSUES + 1))
fi

if [ ! -z "$HDLC_ERRORS" ] && [ "$HDLC_ERRORS" -gt 10 ]; then
    echo -e "${COLOR_YELLOW}⚠️  ADVERTENCIA: Múltiples errores HDLC${COLOR_RESET}"
    ISSUES=$((ISSUES + 1))
fi

if [ "$ISSUES" -eq 0 ]; then
    echo -e "${COLOR_GREEN}✅ Sistema operando normalmente${COLOR_RESET}"
else
    echo -e "\n${COLOR_YELLOW}Se detectaron $ISSUES problema(s)${COLOR_RESET}"
    echo -e "\nPara más detalles, ejecuta:"
    echo -e "   ${COLOR_CYAN}sudo journalctl -u dlms-multi-meter.service -f${COLOR_RESET}"
fi

# ============================================================================
# FOOTER
# ============================================================================
echo -e "\n${COLOR_BLUE}═══════════════════════════════════════════════════════════════════${COLOR_RESET}"
echo -e "${COLOR_BLUE}Para monitoreo continuo (actualización cada 5 seg):${COLOR_RESET}"
echo -e "${COLOR_CYAN}   watch -n 5 -c $0${COLOR_RESET}"
echo -e "\n${COLOR_BLUE}Para ver logs en tiempo real:${COLOR_RESET}"
echo -e "${COLOR_CYAN}   sudo journalctl -u dlms-multi-meter.service -f${COLOR_RESET}"
echo -e "${COLOR_BLUE}═══════════════════════════════════════════════════════════════════${COLOR_RESET}\n"
