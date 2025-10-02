#!/usr/bin/env bash
set -euo pipefail

# Auto-elevate if not running as root (nmcli changes need privileges)
if [ "${EUID:-$(id -u)}" -ne 0 ]; then
  exec sudo -E "$0" "$@"
fi

# wifi_ap_nmcli.sh — Gestiona un punto de acceso Wi-Fi con NetworkManager (nmcli)
#
# Uso:
#   sudo ./wifi_ap_nmcli.sh up   SSID PASS [BAND] [CHANNEL]
#   sudo ./wifi_ap_nmcli.sh down SSID
#   sudo ./wifi_ap_nmcli.sh status [SSID]
#
# Nota (BeaglePlay / WL18xx): SOLO 2.4 GHz. 5 GHz no soportado.

# ---- Parsing args ----
CMD=${1:-}
SSID=${2:-}
PASS=${3:-}
BAND=${4:-}
CHAN=${5:-}

# shorthand: ./script SSID PASS [BAND] [CHAN]  => up
case "${CMD:-}" in
  up|down|status|help) ;;
  "") ;;
  *)
    CHAN="$BAND"; BAND="$PASS"; PASS="$SSID"; SSID="$CMD"; CMD="up"
    ;;
esac

die() { echo "[ap] $*" >&2; exit 1; }

require_nm() {
  command -v nmcli >/dev/null 2>&1 || die "nmcli no encontrado. Instala NetworkManager."
  systemctl is-active --quiet NetworkManager || die "NetworkManager no está activo. Inícialo con: sudo systemctl enable --now NetworkManager"

  # Verifica backend Wi-Fi (BeaglePlay va mejor con wpa_supplicant)
  if NetworkManager --print-config 2>/dev/null | grep -qi 'wifi\.backend *= *iwd'; then
    die "NM usa backend IWD (no recomendado para AP en WL18xx).
Configura wpa_supplicant y reinicia NM:
  sudo mkdir -p /etc/NetworkManager/conf.d
  echo -e '[device]\nwifi.backend=wpa_supplicant' | sudo tee /etc/NetworkManager/conf.d/10-wifi-backend.conf
  sudo systemctl restart NetworkManager"
  fi
}

# Intenta crear ap0 si conviene
ensure_ap0() {
  if ! ip link show ap0 >/dev/null 2>&1; then
    if command -v iw >/dev/null 2>&1 && ip link show wlan0 >/dev/null 2>&1; then
      ip link set wlan0 up || true
      iw dev wlan0 interface add ap0 type __ap || true
    fi
  fi
}

wifi_iface() {
  # Preferencia: SoftAp0 (Wilink AP nativa)
  while IFS=: read -r dev type state _; do
    if [ "$dev" = "SoftAp0" ] && [ "$type" = "wifi" ] && [ "$state" != "unmanaged" ]; then
      echo "$dev"; return 0
    fi
  done < <(nmcli -t -f DEVICE,TYPE,STATE dev status)

  # Si existe ap0 (creada con iw), úsala
  if ip link show ap0 >/dev/null 2>&1; then
    echo "ap0"; return 0
  fi

  # Por último wlan0
  while IFS=: read -r dev type state _; do
    if [ "$dev" = "wlan0" ] && [ "$type" = "wifi" ] && [ "$state" != "unmanaged" ]; then
      echo "$dev"; return 0
    fi
  done < <(nmcli -t -f DEVICE,TYPE,STATE dev status)

  # Si nada anterior, primera Wi-Fi utilizable
  nmcli -t -f DEVICE,TYPE,STATE dev status | awk -F: '$2=="wifi" && $3!="unavailable"{print $1; exit}'
}

ap_con_name() { echo "ap-$1"; }

up_ap() {
  local ssid="$1" pass="$2" band="${3:-}" chan="${4:-}"
  [ -n "$ssid" ] || die "Falta SSID"
  [ -n "$pass" ] || die "Falta contraseña (8-63 chars)"
  [ ${#pass} -ge 8 ] || die "La contraseña debe tener al menos 8 caracteres"

  require_nm
  ensure_ap0

  # Enciende radio y regdomain
  nmcli radio wifi on || true
  iw reg set CO || true

  # Elegir interfaz
  local iface; iface=$(wifi_iface) || true
  [ -n "${iface:-}" ] || die "No se encontró interfaz Wi-Fi disponible (SoftAp0/ap0/wlan0)."

  # Asegurar gestionada por NM
  nmcli dev set "$iface" managed yes || true

  # Validar banda (WL18xx no soporta 5 GHz)
  if [ -z "$band" ]; then band="2"; fi
  case "$band" in
    2|2.4) nmcli con 2>/dev/null >/dev/null; ;;  # ok
    5)     die "Este hardware (WL18xx) NO soporta 5 GHz. Usa banda 2.4." ;;
    *)     echo "[ap] Banda desconocida: $band (usando 2.4 GHz)"; band="2" ;;
  esac

  # Canal recomendado (1/6/11). Por defecto 6.
  if [ -z "$chan" ]; then chan="6"; fi
  case "$chan" in
    1|6|11) ;;
    *) echo "[ap] Canal $chan no recomendado en 2.4 GHz; usando 6."; chan="6" ;;
  esac

  local con_name; con_name=$(ap_con_name "$ssid")

  # Crear o actualizar perfil
  if nmcli -t -f NAME,TYPE con show | awk -F: -v c="$con_name" '$1==c && $2=="802-11-wireless"{f=1} END{exit !f}'; then
    echo "[ap] Actualizando conexión existente: $con_name"
    nmcli con modify "$con_name" 802-11-wireless.mode ap 802-11-wireless.ssid "$ssid"
  else
    echo "[ap] Creando conexión AP: $con_name en $iface"
    nmcli con add type wifi ifname "$iface" con-name "$con_name" autoconnect yes ssid "$ssid"
    nmcli con modify "$con_name" 802-11-wireless.mode ap
  fi

  # Atar al dispositivo elegido
  nmcli con modify "$con_name" connection.interface-name "$iface"

  # Banda/canal 2.4 GHz
  nmcli con modify "$con_name" 802-11-wireless.band bg
  nmcli con modify "$con_name" 802-11-wireless.channel "$chan"

  # Seguridad WPA2-Personal (RSN/CCMP) y PMF off por compatibilidad
  nmcli con modify "$con_name" \
    802-11-wireless-security.key-mgmt wpa-psk \
    802-11-wireless-security.auth-alg open \
    802-11-wireless-security.proto rsn \
    802-11-wireless-security.group ccmp \
    802-11-wireless-security.pairwise ccmp \
    802-11-wireless-security.pmf disable \
    802-11-wireless-security.psk "$pass"

  # IP compartida (NAT + DHCP por NM); sin IPv6
  nmcli con modify "$con_name" ipv4.method shared ipv6.method ignore

  echo "[ap] Reiniciando conexión para aplicar cambios ..."
  nmcli con down "$con_name" || true
  nmcli dev disconnect "$iface" || true

  echo "[ap] Activando AP $ssid en $iface ..."
  if ! nmcli con up "$con_name" ifname "$iface"; then
    echo "[ap] Falló la activación. Intentando ajustes de compatibilidad ..." >&2
    nmcli con modify "$con_name" -802-11-wireless-security.proto || true
    nmcli con modify "$con_name" -802-11-wireless-security.group || true
    nmcli con modify "$con_name" -802-11-wireless-security.pairwise || true
    nmcli con down "$con_name" || true
    nmcli dev disconnect "$iface" || true

    # Fallback: probar ap0, SoftAp0 y wlan0 en ese orden
    for alt_iface in ap0 SoftAp0 wlan0; do
      ip link show "$alt_iface" >/dev/null 2>&1 || continue
      if nmcli -t -f DEVICE,TYPE,STATE dev status | awk -F: -v d="$alt_iface" '$1==d && $2=="wifi"{ok=1} END{exit !ok}'; then
        nmcli con modify "$con_name" connection.interface-name "$alt_iface" || true
        nmcli dev disconnect "$alt_iface" || true
        if nmcli con up "$con_name" ifname "$alt_iface"; then
          iface="$alt_iface"; break
        fi
      fi
    done
  fi

  # Mostrar IP del modo compartido
  local ip
  ip=$(nmcli -g IP4.ADDRESS dev show "$iface" 2>/dev/null | head -n1 || true)
  echo "[ap] AP activo. SSID=$ssid iface=$iface ip=${ip:-desconocida}"
}

down_ap() {
  local ssid="$1"; [ -n "$ssid" ] || die "Falta SSID"
  local con_name; con_name=$(ap_con_name "$ssid")
  echo "[ap] Desactivando AP $ssid ..."
  if nmcli -t -f NAME con show | grep -Fxq "$con_name"; then
    nmcli con down "$con_name" || true
  else
    echo "[ap] Conexión no encontrada, nada que bajar"
  fi
}

status_ap() {
  local ssid="${1:-}"
  if [ -n "$ssid" ]; then
    local con_name; con_name=$(ap_con_name "$ssid")
    nmcli -f connection.id,connection.uuid,connection.type,connection.interface-name con show "$con_name" || true
    local dev; dev=$(wifi_iface || true)
    [ -n "${dev:-}" ] && nmcli -f GENERAL,IP4 dev show "$dev" || true
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
  - BAND: 2 (solo 2.4 GHz en BeaglePlay / WL18xx)
  - CHANNEL: 1, 6 o 11 (por defecto 6)
EOF
    [ "$CMD" = "help" ] && exit 0
  fi

  case "$CMD" in
    up)     up_ap "$SSID" "$PASS" "${BAND:-}" "${CHAN:-}" ;;
    down)   down_ap "$SSID" ;;
    status) status_ap "${SSID:-}" ;;
    *)      die "Comando no soportado: $CMD" ;;
  esac
}

main "$@"
