#!/usr/bin/env python3
"""
Analyze DLMS diagnostics and network metrics to find temporal patterns.
Produces a simple CSV summary: errors_per_hour.csv and prints top time windows.
"""
import sqlite3
from collections import Counter
from datetime import datetime
import csv

DB='data/admin.db'

def parse_ts(ts):
    # ts may be stored as ISO string
    try:
        return datetime.fromisoformat(ts)
    except Exception:
        return None

con=sqlite3.connect(DB)
cur=con.cursor()

# Errors per hour
cur.execute("SELECT timestamp, category, message FROM dlms_diagnostics ORDER BY timestamp DESC")
rows=cur.fetchall()
hours=Counter()
for ts, cat, msg in rows:
    dt=parse_ts(ts)
    if dt:
        key=dt.replace(minute=0, second=0, microsecond=0)
        hours[key]+=1

# Write CSV
with open('dlms_errors_per_hour.csv','w',newline='') as f:
    w=csv.writer(f)
    w.writerow(['hour','errors'])
    for hour, cnt in sorted(hours.items()):
        w.writerow([hour.isoformat(), cnt])

print('Total diagnostics:', len(rows))
print('Top 10 hours by errors:')
for hour, cnt in hours.most_common(10):
    print(hour.isoformat(), cnt)

# Correlate with network_metrics: mqtt_messages_sent
cur.execute("SELECT timestamp, mqtt_messages_sent FROM network_metrics ORDER BY timestamp DESC LIMIT 500")
net=cur.fetchall()
net_by_hour={}
for ts, mqtt_msgs in net:
    dt=parse_ts(ts)
    if dt:
        key=dt.replace(minute=0, second=0, microsecond=0)
        net_by_hour.setdefault(key, []).append(mqtt_msgs)

# compute avg mqtt per hour
import statistics
with open('mqtt_per_hour.csv','w',newline='') as f:
    w=csv.writer(f)
    w.writerow(['hour','avg_mqtt_msgs'])
    for hour, vals in sorted(net_by_hour.items()):
        w.writerow([hour.isoformat(), statistics.mean(vals)])

print('\nWrote dlms_errors_per_hour.csv and mqtt_per_hour.csv')
con.close()
