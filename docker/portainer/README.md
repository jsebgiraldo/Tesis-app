# Portainer CE

Administración visual de los stacks Docker locales.

## Arranque
```bash
./up.sh
```
Primera vez: abre http://localhost:9000 y define usuario/contraseña admin.

En Windows PowerShell:
```powershell
./up.ps1
```

## Comandos útiles
- Ver estado: `./status.sh`
- Logs: `./logs.sh`
- Detener: `./down.sh`

HTTPS (auto): https://localhost:9443 (certificado self-signed generado al primer arranque).

## Persistencia
Los datos (usuarios, endpoints) se guardan en el volumen `portainer-data`.

## Seguridad
- Cambia la contraseña inicial por una fuerte.
- Considera habilitar HTTPS reverse proxy con certificados válidos si expones puertos fuera de localhost.
- No expongas el puerto 9000/9443 directamente a Internet sin autenticación adicional.

## Eliminar completamente
```bash
docker compose down -v
```
