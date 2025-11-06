"""
Dashboard Alerts Component
Muestra alertas críticas de errores HDLC, watchdog y degradación de QoS
"""
import streamlit as st
import pandas as pd
from datetime import datetime, timedelta
from admin.database import db, DLMSDiagnostic, Alarm, Meter
from sqlalchemy import func, desc


def show_critical_alerts_banner():
    """Muestra un banner con alertas críticas en la parte superior del dashboard"""
    
    try:
        with db.get_session() as session:
            # Contar alarmas críticas no reconocidas en las últimas 24 horas
            cutoff = datetime.utcnow() - timedelta(hours=24)
            critical_alarms = session.query(Alarm).filter(
                Alarm.acknowledged == False,
                Alarm.severity.in_(['critical', 'error']),
                Alarm.timestamp >= cutoff
            ).count()
            
            # Contar errores HDLC en la última hora
            hour_ago = datetime.utcnow() - timedelta(hours=1)
            hdlc_errors_hour = session.query(DLMSDiagnostic).filter(
                DLMSDiagnostic.category == 'hdlc',
                DLMSDiagnostic.timestamp >= hour_ago
            ).count()
            
            # Contar alarmas de watchdog en las últimas 24h
            watchdog_alarms = session.query(Alarm).filter(
                Alarm.category == 'watchdog',
                Alarm.timestamp >= cutoff
            ).count()
            
            # Mostrar banner solo si hay problemas
            if critical_alarms > 0 or hdlc_errors_hour > 5 or watchdog_alarms > 0:
                with st.container():
                    cols = st.columns([1, 3, 1])
                    with cols[1]:
                        if critical_alarms > 0:
                            st.error(f"🚨 **{critical_alarms} alarmas críticas** sin reconocer")
                        
                        if hdlc_errors_hour > 5:
                            st.warning(f"⚠️ **{hdlc_errors_hour} errores HDLC** en la última hora")
                        
                        if watchdog_alarms > 0:
                            st.warning(f"🐕 **{watchdog_alarms} intervenciones del watchdog** en 24h")
    
    except Exception as e:
        st.error(f"Error cargando alertas: {e}")


def show_hdlc_errors_panel():
    """Panel detallado de errores HDLC para análisis"""
    
    st.subheader("🔴 Errores HDLC Recientes")
    
    try:
        with db.get_session() as session:
            # Últimos 50 errores HDLC
            diagnostics = session.query(DLMSDiagnostic).filter(
                DLMSDiagnostic.category == 'hdlc'
            ).order_by(desc(DLMSDiagnostic.timestamp)).limit(50).all()
            
            if diagnostics:
                # Convertir a DataFrame
                data = []
                for d in diagnostics:
                    meter = session.query(Meter).filter(Meter.id == d.meter_id).first()
                    data.append({
                        'Timestamp': d.timestamp,
                        'Medidor': meter.name if meter else f"ID {d.meter_id}",
                        'Severidad': d.severity,
                        'Mensaje': d.message[:100] + '...' if len(d.message) > 100 else d.message
                    })
                
                df = pd.DataFrame(data)
                st.dataframe(df, use_container_width=True, height=300)
                
                # Estadísticas por hora
                st.markdown("### Distribución por Hora")
                df['Hora'] = pd.to_datetime(df['Timestamp']).dt.floor('H')
                hourly = df.groupby('Hora').size().reset_index(name='Errores')
                st.bar_chart(hourly.set_index('Hora')['Errores'])
                
            else:
                st.success("✅ No hay errores HDLC recientes")
    
    except Exception as e:
        st.error(f"Error cargando diagnósticos HDLC: {e}")


def show_watchdog_status():
    """Muestra el estado del watchdog y sus intervenciones"""
    
    st.subheader("🐕 Estado del Watchdog")
    
    try:
        with db.get_session() as session:
            # Alarmas de watchdog en las últimas 24h
            cutoff = datetime.utcnow() - timedelta(hours=24)
            watchdog_alarms = session.query(Alarm).filter(
                Alarm.category == 'watchdog',
                Alarm.timestamp >= cutoff
            ).order_by(desc(Alarm.timestamp)).all()
            
            col1, col2, col3 = st.columns(3)
            
            with col1:
                st.metric("Intervenciones (24h)", len(watchdog_alarms))
            
            with col2:
                critical_count = sum(1 for a in watchdog_alarms if a.severity == 'critical')
                st.metric("Críticas", critical_count)
            
            with col3:
                if watchdog_alarms:
                    last_intervention = watchdog_alarms[0].timestamp
                    minutes_ago = (datetime.utcnow() - last_intervention).total_seconds() / 60
                    st.metric("Última intervención", f"{minutes_ago:.0f} min ago")
                else:
                    st.metric("Última intervención", "N/A")
            
            # Tabla de intervenciones
            if watchdog_alarms:
                st.markdown("### Intervenciones Recientes")
                data = []
                for alarm in watchdog_alarms[:10]:
                    meter = session.query(Meter).filter(Meter.id == alarm.meter_id).first()
                    data.append({
                        'Timestamp': alarm.timestamp,
                        'Medidor': meter.name if meter else f"ID {alarm.meter_id}",
                        'Severidad': alarm.severity,
                        'Razón': alarm.message
                    })
                
                df = pd.DataFrame(data)
                st.dataframe(df, use_container_width=True)
    
    except Exception as e:
        st.error(f"Error cargando estado del watchdog: {e}")


def show_qos_degradation_alerts():
    """Alertas de degradación de calidad de servicio"""
    
    st.subheader("📊 Calidad de Servicio (QoS)")
    
    try:
        with db.get_session() as session:
            # Obtener métricas de red de la última hora
            hour_ago = datetime.utcnow() - timedelta(hours=1)
            
            from admin.database import NetworkMetric
            
            recent_metrics = session.query(NetworkMetric).filter(
                NetworkMetric.timestamp >= hour_ago
            ).order_by(desc(NetworkMetric.timestamp)).limit(60).all()
            
            if recent_metrics:
                # Calcular promedios
                avg_mqtt = sum(m.mqtt_messages_sent for m in recent_metrics) / len(recent_metrics)
                avg_dlms = sum(m.dlms_requests_sent for m in recent_metrics) / len(recent_metrics)
                
                # Tasa de publicación MQTT vs DLMS requests
                if avg_dlms > 0:
                    mqtt_ratio = (avg_mqtt / avg_dlms) * 100
                else:
                    mqtt_ratio = 0
                
                col1, col2, col3 = st.columns(3)
                
                with col1:
                    st.metric("MQTT msgs/hora (avg)", f"{avg_mqtt:.1f}")
                
                with col2:
                    st.metric("DLMS requests/hora (avg)", f"{avg_dlms:.1f}")
                
                with col3:
                    color = "normal" if mqtt_ratio > 80 else "inverse"
                    st.metric("Tasa MQTT/DLMS", f"{mqtt_ratio:.1f}%", 
                             delta=None if mqtt_ratio > 80 else "Bajo rendimiento",
                             delta_color=color)
                
                # Alertas de degradación
                if mqtt_ratio < 50:
                    st.error(f"🔴 **Degradación crítica**: Solo {mqtt_ratio:.1f}% de requests DLMS resultan en publicación MQTT")
                elif mqtt_ratio < 80:
                    st.warning(f"⚠️ **Rendimiento reducido**: {mqtt_ratio:.1f}% de tasa MQTT/DLMS")
                else:
                    st.success(f"✅ Sistema operando normalmente ({mqtt_ratio:.1f}% eficiencia)")
            
            else:
                st.info("No hay métricas disponibles para la última hora")
    
    except Exception as e:
        st.error(f"Error calculando QoS: {e}")


def show_connection_health():
    """Muestra la salud de las conexiones DLMS"""
    
    st.subheader("🔌 Salud de Conexiones DLMS")
    
    try:
        with db.get_session() as session:
            meters = session.query(Meter).all()
            
            for meter in meters:
                with st.expander(f"📟 {meter.name} ({meter.ip_address}:{meter.port})"):
                    col1, col2, col3 = st.columns(3)
                    
                    with col1:
                        st.write(f"**Estado**: {meter.status}")
                        if meter.last_seen:
                            time_diff = (datetime.utcnow() - meter.last_seen).total_seconds()
                            st.write(f"**Última actividad**: {time_diff:.0f}s ago")
                    
                    with col2:
                        st.write(f"**Errores acumulados**: {meter.error_count}")
                        if meter.last_error:
                            st.write(f"**Último error**: {meter.last_error[:50]}...")
                    
                    with col3:
                        # Contar errores HDLC en las últimas 24h para este medidor
                        cutoff = datetime.utcnow() - timedelta(hours=24)
                        hdlc_count = session.query(DLMSDiagnostic).filter(
                            DLMSDiagnostic.meter_id == meter.id,
                            DLMSDiagnostic.category == 'hdlc',
                            DLMSDiagnostic.timestamp >= cutoff
                        ).count()
                        st.write(f"**Errores HDLC (24h)**: {hdlc_count}")
                        
                        if hdlc_count > 20:
                            st.error("⚠️ Alto número de errores HDLC")
                        elif hdlc_count > 5:
                            st.warning("⚠️ Errores HDLC detectados")
    
    except Exception as e:
        st.error(f"Error cargando salud de conexiones: {e}")


# Función principal para integrar en dashboard.py
def render_alerts_page():
    """Renderiza la página completa de alertas y diagnósticos"""
    
    st.title("🚨 Sistema de Alertas y Diagnóstico")
    
    # Banner de alertas críticas
    show_critical_alerts_banner()
    
    st.markdown("---")
    
    # Tabs para organizar información
    tab1, tab2, tab3, tab4 = st.tabs(["🔴 Errores HDLC", "🐕 Watchdog", "📊 QoS", "🔌 Conexiones"])
    
    with tab1:
        show_hdlc_errors_panel()
    
    with tab2:
        show_watchdog_status()
    
    with tab3:
        show_qos_degradation_alerts()
    
    with tab4:
        show_connection_health()
    
    # Auto-refresh
    if st.button("🔄 Actualizar"):
        st.rerun()
