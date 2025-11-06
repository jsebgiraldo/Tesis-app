#!/usr/bin/env python3
"""
Migration script: Add ThingsBoard configuration fields to meters table
"""

import sqlite3
import sys
from pathlib import Path

def migrate_database(db_path: str = "data/admin.db"):
    """Add ThingsBoard fields to meters table"""
    
    print(f"🔧 Migrating database: {db_path}")
    
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    
    # Check if columns already exist
    cursor.execute("PRAGMA table_info(meters)")
    columns = [row[1] for row in cursor.fetchall()]
    
    migrations = []
    
    if 'tb_enabled' not in columns:
        migrations.append("ALTER TABLE meters ADD COLUMN tb_enabled BOOLEAN DEFAULT 1")
    
    if 'tb_host' not in columns:
        migrations.append("ALTER TABLE meters ADD COLUMN tb_host VARCHAR(255) DEFAULT 'thingsboard.cloud'")
    
    if 'tb_port' not in columns:
        migrations.append("ALTER TABLE meters ADD COLUMN tb_port INTEGER DEFAULT 1883")
    
    if 'tb_token' not in columns:
        migrations.append("ALTER TABLE meters ADD COLUMN tb_token VARCHAR(100)")
    
    if 'tb_device_name' not in columns:
        migrations.append("ALTER TABLE meters ADD COLUMN tb_device_name VARCHAR(100)")
    
    if not migrations:
        print("✅ Database already up to date")
        conn.close()
        return
    
    print(f"📝 Applying {len(migrations)} migrations:")
    
    for i, migration in enumerate(migrations, 1):
        print(f"   {i}. {migration}")
        try:
            cursor.execute(migration)
        except Exception as e:
            print(f"   ❌ Error: {e}")
            conn.rollback()
            conn.close()
            sys.exit(1)
    
    conn.commit()
    conn.close()
    
    print("✅ Migration completed successfully")
    
    # Verify
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    cursor.execute("PRAGMA table_info(meters)")
    columns = [row[1] for row in cursor.fetchall()]
    conn.close()
    
    print("\n📊 Current meters table columns:")
    for col in columns:
        print(f"   • {col}")


if __name__ == "__main__":
    db_path = sys.argv[1] if len(sys.argv) > 1 else "data/admin.db"
    migrate_database(db_path)
