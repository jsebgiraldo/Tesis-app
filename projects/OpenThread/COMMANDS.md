# Guía de Comandos OpenThread CLI

## 📚 Referencia Completa de Comandos

### Comandos de Información Básica

| Comando | Descripción | Ejemplo |
|---------|-------------|---------|
| `help` | Muestra todos los comandos disponibles | `> help` |
| `version` | Versión de OpenThread | `> version` |
| `eui64` | IEEE EUI-64 del dispositivo | `> eui64` |
| `extaddr` | Extended Address (64-bit) | `> extaddr` |
| `rloc16` | RLOC16 address | `> rloc16` |

### Gestión de Dataset

```bash
# Crear nuevo dataset
> dataset init new

# Ver dataset activo
> dataset active

# Ver dataset en hexadecimal
> dataset active -x

# Configurar parámetros
> dataset networkname MiRedThread
> dataset channel 15
> dataset panid 0x1234
> dataset extpanid 1111111122222222
> dataset networkkey 00112233445566778899aabbccddeeff

# Aplicar cambios
> dataset commit active

# Cargar dataset desde hex
> dataset set active <hex-string>
```

### Control de Interfaz

```bash
# Activar interfaz Thread
> ifconfig up

# Desactivar interfaz
> ifconfig down

# Ver estado de interfaz
> ifconfig
```

### Control de Thread

```bash
# Iniciar Thread
> thread start

# Detener Thread
> thread stop

# Ver estado (disabled/detached/child/router/leader)
> state
```

### Direcciones IP

```bash
# Ver todas las direcciones IPv6
> ipaddr

# Ver dirección link-local
> ipaddr linklocal

# Ver dirección mesh-local
> ipaddr mleid

# Ver direcciones RLOC
> ipaddr rloc
```

### Topología de Red

```bash
# Ver vecinos directos
> neighbor table

# Ver nodos hijo
> child table

# Ver routers
> router table

# Información del líder
> leaderdata

# Información de partición
> partitionid
```

### Escaneo

```bash
# Escanear redes Thread
> scan

# Escaneo de energía en canales
> scan energy
```

### Ping

```bash
# Ping a dirección IPv6
> ping fd00:db8::1

# Ping con tamaño específico
> ping fd00:db8::1 100

# Ping con conteo
> ping fd00:db8::1 -c 5
```

### Commissioner

```bash
# Iniciar commissioner
> commissioner start

# Agregar joiner (con EUI64 específico)
> commissioner joiner add 0123456789abcdef J01NME

# Agregar cualquier joiner (*)
> commissioner joiner add * J01NME 300

# Listar joiners
> commissioner joiner table

# Detener commissioner
> commissioner stop

# Estado del commissioner
> commissioner state
```

### Joiner

```bash
# Iniciar joiner
> joiner start J01NME

# Detener joiner
> joiner stop

# Estado del joiner
> joiner state
```

### CoAP

```bash
# Iniciar servidor CoAP
> coap start

# Detener servidor CoAP
> coap stop

# Enviar GET request
> coap get <address> <uri-path>

# Enviar POST request
> coap post <address> <uri-path> <payload>

# Ejemplo
> coap get fd00::1 test
```

### UDP

```bash
# Abrir socket UDP
> udp open

# Conectar a dirección
> udp connect fd00::1 1234

# Enviar datos
> udp send hello

# Cerrar socket
> udp close
```

### Multicast

```bash
# Suscribirse a grupo multicast
> ipmaddr add ff04::1

# Ver grupos multicast
> ipmaddr

# Desuscribirse
> ipmaddr del ff04::1
```

### Diagnóstico

```bash
# Estadísticas de radio
> radio stats

# Resetear estadísticas
> radio stats reset

# Información de buffers
> bufferinfo

# Estadísticas MAC
> mac stats

# Información de counters
> counters

# Resetear counters
> counters reset
```

### Logging

```bash
# Cambiar nivel de log (0-5)
# 0: None, 1: Crit, 2: Warn, 3: Note, 4: Info, 5: Debg
> log level 5

# Ver nivel actual
> log level
```

### Configuración de Radio

```bash
# Ver canal actual
> channel

# Cambiar canal (11-26)
> channel 15

# Ver potencia TX
> txpower

# Establecer potencia TX (en dBm)
> txpower 10
```

### Factory Reset

```bash
# Reset completo (borra toda configuración)
> factoryreset
```

### Comandos de Red Avanzados

```bash
# Ver prefijo de red
> prefix

# Agregar prefijo
> prefix add fd00:db8::/64 paros

# Ver rutas
> route

# Información del border router
> netdata show
```

## 🎯 Escenarios Comunes

### 1. Crear una Red desde Cero (Leader)

```bash
> dataset init new
> dataset networkname MiRedThread
> dataset channel 15
> dataset panid 0xABCD
> dataset commit active
> ifconfig up
> thread start
> state
# Esperar a ver "leader"
> dataset active -x
# Copiar el hex string para otros nodos
```

### 2. Unirse a una Red Existente

```bash
> dataset set active <hex-string-copiado>
> ifconfig up
> thread start
> state
# Esperar a ver "child" o "router"
```

### 3. Test de Conectividad

```bash
# En nodo A
> ipaddr
# Copiar dirección fd...

# En nodo B
> ping <direccion-ipv6-de-A>
```

### 4. Agregar Dispositivo con Commissioner

```bash
# En el commissioner
> commissioner start
> commissioner joiner add * J01NME 300

# En el nuevo dispositivo
> ifconfig up
> joiner start J01NME
# Esperar mensaje de éxito
> thread start
```

### 5. Monitorear la Red

```bash
> state
> neighbor table
> child table
> router table
> ipaddr
> leaderdata
```

## 💡 Tips y Trucos

1. **Guardar configuración**: Los cambios se guardan automáticamente en NVS
2. **Ver logs detallados**: `log level 5` antes de ejecutar comandos
3. **Reset rápido**: `factoryreset` para empezar de cero
4. **Autocompletado**: Presiona TAB para autocompletar comandos
5. **Historial**: Usa flechas arriba/abajo para navegar historial

## ⚠️ Notas Importantes

- Los comandos son case-sensitive
- Algunos comandos requieren que la interfaz esté activa (`ifconfig up`)
- El estado debe ser diferente de `disabled` para muchos comandos
- Los cambios en dataset requieren `dataset commit active`
- El factoryreset requiere reinicio del dispositivo

---

Para más información: [OpenThread CLI Reference](https://github.com/openthread/openthread/blob/main/src/cli/README.md)
