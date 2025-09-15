#!/usr/bin/env bash
set -euo pipefail

# Auto-elevate if not running as root (nmcli connection changes require privileges)
if [ "${EUID:-$(id -u)}" -ne 0 ]; then
  exec sudo -E "$0" "$@"
fi

# wifi_ap_nmcli.sh — Gestiona un punto de acceso Wi‑Fi con NetworkManager (nmcli)
#
# Requisitos:
#  - Raspberry Pi 5 (u otro Linux) con NetworkManager activo y radio Wi‑Fi soportada por AP mode
#  - nmcli instalado
#
# Uso rápido:
#   sudo ./wifi_ap_nmcli.sh up   SSID PASS [BAND] [CHANNEL]
#   sudo ./wifi_ap_nmcli.sh down [SSID]
#   sudo ./wifi_ap_nmcli.sh status [SSID]
#
# Ejemplos:
#   sudo ./wifi_ap_nmcli.sh up   TesisAP MyPass123 5 36
#   sudo ./wifi_ap_nmcli.sh down TesisAP
#   sudo ./wifi_ap_nmcli.sh status TesisAP

# Parsing:
# Soporta ambos formatos:
#  1) ./wifi_ap_nmcli.sh up SSID PASS [BAND] [CHANNEL]
#  2) ./wifi_ap_nmcli.sh SSID PASS [BAND] [CHANNEL]  (shorthand -> up)

CMD=${1:-}
SSID=${2:-}
PASS=${3:-}
BAND=${4:-}
CHAN=${5:-}

# Si el primer argumento no es un comando conocido pero parece un SSID,
# reinterpreta como shorthand: up SSID PASS [BAND] [CHANNEL]
case "${CMD:-}" in
  up|down|status|help) ;;
  "") ;;
  *)
    # Shift virtual: trata $1 como SSID
    CHAN="$BAND"; BAND="$PASS"; PASS="$SSID"; SSID="$CMD"; CMD="up"
    ;;
esac

die() { echo "[ap] $*" >&2; exit 1; }

require_nm() {
  command -v nmcli >/dev/null 2>&1 || die "nmcli no encontrado. Instala NetworkManager."
  # Verifica que NetworkManager esté corriendo
  systemctl is-active --quiet NetworkManager || die "NetworkManager no está activo. Inícialo con: sudo systemctl enable --now NetworkManager"
}

wifi_iface() {
  # Preferir wlan0 si existe y es gestionable; SoftAp0 puede fallar con algunos backends
  local line
  while IFS=: read -r dev type state rest; do
    if [ "$dev" = "wlan0" ] && [ "$type" = "wifi" ] && [ "$state" != "unmanaged" ]; then
      echo "$dev"; return 0
    fi
  done < <(nmcli -t -f DEVICE,TYPE,STATE dev status)

  # Si no hay wlan0, probar SoftAp0
  while IFS=: read -r dev type state rest; do
    if [ "$dev" = "SoftAp0" ] && [ "$type" = "wifi" ] && [ "$state" != "unmanaged" ]; then
      echo "$dev"; return 0
    fi
  done < <(nmcli -t -f DEVICE,TYPE,STATE dev status)

  # Si no hay ninguna de las anteriores, tomar la primera interfaz Wi‑Fi utilizable
  nmcli -t -f DEVICE,TYPE,STATE dev status | awk -F: '$2=="wifi" && $3!="unavailable" {print $1; exit}'
}

ap_con_name() {
  local ssid="$1"; echo "ap-${ssid}"
}

up_ap() {
  local ssid="$1" pass="$2" band="${3:-}" chan="${4:-}"
  [ -n "$ssid" ] || die "Falta SSID"
  [ -n "$pass" ] || die "Falta contraseña (8-63 chars)"
  [ ${#pass} -ge 8 ] || die "La contraseña debe tener al menos 8 caracteres"

  local iface; iface=$(wifi_iface) || true
  [ -n "$iface" ] || die "No se encontró interfaz Wi‑Fi disponible"

  # Asegura que la radio Wi‑Fi esté encendida
  nmcli radio wifi on || true

  # Asegura que la interfaz esté bajo control de NM
  nmcli dev set "$iface" managed yes || true

  local con_name; con_name=$(ap_con_name "$ssid")

  # Crea o actualiza conexión en modo AP
  if nmcli -t -f NAME,TYPE con show | awk -F: '$1==c && $2=="802-11-wireless"{found=1} END{exit !found}' c="$con_name"; then
    echo "[ap] Actualizando conexión existente: $con_name"
    nmcli con modify "$con_name" 802-11-wireless.mode ap 802-11-wireless.ssid "$ssid"
  else
    echo "[ap] Creando conexión AP: $con_name en $iface"
    nmcli con add type wifi ifname "$iface" con-name "$con_name" autoconnect yes ssid "$ssid"
    nmcli con modify "$con_name" 802-11-wireless.mode ap
  fi

  # Asegura que esta conexión use la interfaz seleccionada (SoftAp0 preferida)
  nmcli con modify "$con_name" connection.interface-name "$iface"

  # Seguridad WPA2-Personal explícita (RSN + CCMP) para compatibilidad con IWD y wpa_supplicant
  # Nota: IWD puede fallar si TKIP está habilitado implícitamente. Forzamos RSN/CCMP.
  nmcli con modify "$con_name" \
    802-11-wireless-security.key-mgmt wpa-psk \
    802-11-wireless-security.auth-alg open \
    802-11-wireless-security.proto rsn \
    802-11-wireless-security.group ccmp \
    802-11-wireless-security.pairwise ccmp \
    802-11-wireless-security.psk "$pass"

  # PMF (802.11w): deshabilitar por compatibilidad si el backend es IWD antiguo
  # (algunas combinaciones IWD/NM fallan con "InvalidArguments" en AP si PMF requerido)
  nmcli con modify "$con_name" 802-11-wireless-security.pmf disable || true

  # Banda/canal: si el usuario no especifica, usar 2.4GHz por defecto por compatibilidad
  if [ -z "$band" ]; then
    band=2
  fi
  if [ -n "$band" ]; then
    case "$band" in
      2|2.4) nmcli con modify "$con_name" 802-11-wireless.band bg ;;
      5)     nmcli con modify "$con_name" 802-11-wireless.band a  ;;
      *)     echo "[ap] Banda desconocida/no soportada: $band (usa 2 o 5)" ;;
    esac
  fi

  if [ -n "$chan" ]; then
    nmcli con modify "$con_name" 802-11-wireless.channel "$chan"
  fi

  # IP local (shared) para dar NAT/DHCP mediante NetworkManager
  nmcli con modify "$con_name" ipv4.method shared ipv6.method ignore

  echo "[ap] Reiniciando conexión para aplicar cambios ..."
  nmcli con down "$con_name" || true
  # Asegura que el dispositivo no tenga otra conexión activa que bloquee AP
  nmcli dev disconnect "$iface" || true
  echo "[ap] Activando AP $ssid en $iface ..."
  if ! nmcli con up "$con_name"; then
    echo "[ap] Falló la activación con configuración RSN/CCMP. Intentando ajuste de compatibilidad ..." >&2
    # Intento de compatibilidad: retirar restricciones explícitas (listas) y reintentar
    nmcli con modify "$con_name" -802-11-wireless-security.proto || true
    nmcli con modify "$con_name" -802-11-wireless-security.group || true
    nmcli con modify "$con_name" -802-11-wireless-security.pairwise || true
    nmcli con modify "$con_name" ipv4.method shared ipv6.method ignore || true
    nmcli con down "$con_name" || true
    nmcli dev disconnect "$iface" || true
    if ! nmcli con up "$con_name"; then
      echo "[ap] Reintento fallido. Probando con interfaz alternativa si está disponible ..." >&2
      # Cambiar de interfaz (wlan0 <-> SoftAp0) y reintentar
      local alt_iface=""
      if [ "$iface" = "wlan0" ]; then alt_iface="SoftAp0"; else alt_iface="wlan0"; fi
      if nmcli -t -f DEVICE,TYPE,STATE dev status | awk -F: -v d="$alt_iface" '$1==d && $2=="wifi" && $3!="unavailable" {ok=1} END{exit !ok}'; then
        nmcli con modify "$con_name" connection.interface-name "$alt_iface" || true
        nmcli dev disconnect "$alt_iface" || true
        nmcli con up "$con_name" || true
      fi
    fi
  fi
  # Muestra IP asignada por modo compartido
  local ip
  ip=$(nmcli -g IP4.ADDRESS dev show "$iface" 2>/dev/null | head -n1 || true)
  echo "[ap] AP activo. SSID=$ssid iface=$iface ip=${ip:-desconocida}"
}

down_ap() {
  local ssid="$1"; [ -n "$ssid" ] || die "Falta SSID"
  local con_name; con_name=$(ap_con_name "$ssid")
  echo "[ap] Desactivando AP $ssid ..."
  nmcli -t -f NAME con show | grep -Fxq "$con_name" && nmcli con down "$con_name" || echo "[ap] Conexión no encontrada, nada que bajar"
}

status_ap() {
  local ssid="$1"
  if [ -n "$ssid" ]; then
    local con_name; con_name=$(ap_con_name "$ssid")
    # Show connection profile details (use fully-qualified field names for compatibility)
    nmcli -f connection.id,connection.uuid,connection.type,connection.interface-name con show "$con_name" || true
    nmcli -f GENERAL,IP4 dev show "$(wifi_iface)" || true
  else
    nmcli dev status
    nmcli con show
  fi
}

main() {
  if [ -z "$CMD" ] || [ "$CMD" = "help" ]; then
    cat <<EOF
Uso:
  $0 up SSID PASS [BAND] [CHANNEL]
  $0 down SSID
  $0 status [SSID]
  $0 SSID PASS [BAND] [CHANNEL]   # shorthand de 'up'

Notas:
  - BAND: 2 o 5 (por defecto 2.4GHz)
  - CHANNEL: entero válido (por defecto 6 en 2.4GHz)
EOF
    [ "$CMD" = "help" ] && exit 0
  fi
  require_nm
  case "$CMD" in
    up) up_ap "$SSID" "$PASS" "$BAND" "$CHAN" ;;
    down) down_ap "$SSID" ;;
    status) status_ap "$SSID" ;;
    *) die "Comando no soportado: $CMD" ;;
  esac
}

main "$@"
