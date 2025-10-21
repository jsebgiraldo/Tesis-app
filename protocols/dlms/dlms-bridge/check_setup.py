#!/usr/bin/env python3
"""
Script de verificación para el DLMS-MQTT Bridge.
Verifica que todas las dependencias estén instaladas y la configuración sea válida.
"""

import sys
from pathlib import Path

def check_python_version():
    """Verificar versión de Python."""
    print("🐍 Verificando versión de Python...")
    version = sys.version_info
    if version.major < 3 or (version.major == 3 and version.minor < 10):
        print(f"   ❌ Python {version.major}.{version.minor} detectado")
        print(f"   ⚠️  Se requiere Python 3.10 o superior")
        return False
    print(f"   ✅ Python {version.major}.{version.minor}.{version.micro}")
    return True

def check_dependencies():
    """Verificar dependencias instaladas."""
    print("\n📦 Verificando dependencias...")
    
    dependencies = {
        'aiomqtt': 'Cliente MQTT',
        'pydantic': 'Validación de configuración',
        'pydantic_settings': 'Settings con Pydantic',
    }
    
    all_ok = True
    for module, description in dependencies.items():
        try:
            __import__(module)
            print(f"   ✅ {description} ({module})")
        except ImportError:
            print(f"   ❌ {description} ({module}) - NO INSTALADO")
            all_ok = False
    
    return all_ok

def check_config_file():
    """Verificar archivo de configuración."""
    print("\n⚙️  Verificando configuración...")
    
    env_file = Path(".env")
    env_example = Path(".env.example")
    
    if not env_example.exists():
        print("   ❌ .env.example no encontrado")
        return False
    print("   ✅ .env.example encontrado")
    
    if not env_file.exists():
        print("   ⚠️  .env no encontrado")
        print("   💡 Ejecuta: cp .env.example .env")
        return False
    print("   ✅ .env encontrado")
    
    return True

def check_dlms_reader():
    """Verificar que el dlms_reader.py esté accesible."""
    print("\n📡 Verificando módulo DLMS...")
    
    parent_dir = Path(__file__).resolve().parent.parent
    dlms_reader = parent_dir / "dlms_reader.py"
    
    if not dlms_reader.exists():
        print(f"   ❌ dlms_reader.py no encontrado en {parent_dir}")
        return False
    print(f"   ✅ dlms_reader.py encontrado")
    
    return True

def check_app_structure():
    """Verificar estructura de la aplicación."""
    print("\n📁 Verificando estructura de la aplicación...")
    
    required_files = [
        "app/__init__.py",
        "app/config.py",
        "app/dlms_reader.py",
        "app/mqtt_transport.py",
        "app/controller.py",
        "app/main.py",
    ]
    
    all_ok = True
    for file_path in required_files:
        path = Path(file_path)
        if path.exists():
            print(f"   ✅ {file_path}")
        else:
            print(f"   ❌ {file_path} - NO ENCONTRADO")
            all_ok = False
    
    return all_ok

def try_import_config():
    """Intentar importar y validar la configuración."""
    print("\n🔧 Validando configuración...")
    
    try:
        from app.config import settings
        print(f"   ✅ Configuración cargada")
        print(f"   📍 DLMS: {settings.DLMS_HOST}:{settings.DLMS_PORT}")
        print(f"   📍 MQTT: {settings.MQTT_HOST}:{settings.MQTT_PORT}")
        print(f"   📍 Device ID: {settings.DEVICE_ID}")
        print(f"   📍 Mediciones: {', '.join(settings.DLMS_MEASUREMENTS)}")
        return True
    except Exception as e:
        print(f"   ❌ Error al cargar configuración: {e}")
        return False

def main():
    """Función principal."""
    print("=" * 60)
    print("DLMS-MQTT Bridge - Verificación de Sistema")
    print("=" * 60)
    print()
    
    checks = [
        ("Python", check_python_version),
        ("Dependencias", check_dependencies),
        ("Configuración", check_config_file),
        ("DLMS Reader", check_dlms_reader),
        ("Estructura", check_app_structure),
        ("Config Import", try_import_config),
    ]
    
    results = {}
    for name, check_func in checks:
        try:
            results[name] = check_func()
        except Exception as e:
            print(f"\n   ❌ Error inesperado en {name}: {e}")
            results[name] = False
    
    print("\n" + "=" * 60)
    print("Resumen")
    print("=" * 60)
    
    for name, result in results.items():
        status = "✅ PASS" if result else "❌ FAIL"
        print(f"{status:12} {name}")
    
    all_pass = all(results.values())
    
    print("\n" + "=" * 60)
    if all_pass:
        print("🎉 ¡Todo listo! El sistema está correctamente configurado.")
        print("\nPróximo paso:")
        print("  ./run.sh")
        print("\nO manualmente:")
        print("  python -m app.main")
        return 0
    else:
        print("⚠️  Se encontraron problemas. Por favor corrige los errores.")
        print("\nPara instalar dependencias:")
        print("  pip install -r requirements.txt")
        print("\nPara configurar:")
        print("  cp .env.example .env")
        return 1

if __name__ == "__main__":
    sys.exit(main())
