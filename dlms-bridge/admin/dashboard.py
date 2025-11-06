"""
Streamlit Dashboard for DLMS Multi-Meter Admin System
Web UI for meter discovery, configuration, and monitoring
"""

import streamlit as st
import pandas as pd
import plotly.graph_objects as go
import plotly.express as px
from datetime import datetime, timedelta
import time
import requests
from typing import List, Dict
import logging

# Page configuration
st.set_page_config(
    page_title="DLMS Multi-Meter Admin",
    page_icon="⚡",
    layout="wide",
    initial_sidebar_state="expanded"
)

# API configuration
API_BASE_URL = "http://localhost:8000"

# Logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


# Initialize global session state for auto-refresh
if 'auto_refresh_enabled' not in st.session_state:
    st.session_state.auto_refresh_enabled = False
if 'refresh_interval' not in st.session_state:
    st.session_state.refresh_interval = 10
if 'last_update_time' not in st.session_state:
    st.session_state.last_update_time = datetime.now()


# Utility functions

def api_request(method: str, endpoint: str, **kwargs):
    """Make API request with error handling"""
    try:
        url = f"{API_BASE_URL}{endpoint}"
        # Add default timeout if not specified
        if 'timeout' not in kwargs:
            kwargs['timeout'] = 10
        response = requests.request(method, url, **kwargs)
        response.raise_for_status()
        return response.json()
    except requests.exceptions.Timeout:
        st.error(f"API Timeout: Request to {endpoint} took too long")
        return None
    except requests.exceptions.ConnectionError:
        st.error(f"API Connection Error: Cannot connect to {API_BASE_URL}")
        return None
    except requests.exceptions.RequestException as e:
        st.error(f"API Error: {str(e)}")
        return None
    except Exception as e:
        st.error(f"Unexpected error: {str(e)}")
        return None


def get_meters():
    """Get all meters"""
    return api_request("GET", "/meters")


def get_meter_status(meter_id: int):
    """Get meter status"""
    return api_request("GET", f"/meters/{meter_id}/status")


def get_alarms(acknowledged: bool = False):
    """Get alarms"""
    params = {"acknowledged": acknowledged}
    return api_request("GET", "/alarms", params=params)


def get_metrics_summary():
    """Get metrics summary"""
    return api_request("GET", "/metrics/summary")


# Sidebar navigation

st.sidebar.title("⚡ DLMS Admin")
st.sidebar.markdown("---")

page = st.sidebar.radio(
    "Navigation",
    ["🏠 Home", "🔍 Discovery", "⚙️ Configuration", "📊 Monitoring", "🚨 Alarms", "🔴 Diagnósticos"],
    index=0
)

st.sidebar.markdown("---")

# Auto-refresh settings (Global)
st.sidebar.markdown("### ⚙️ Settings")

auto_refresh = st.sidebar.checkbox(
    "Enable Auto-refresh", 
    value=st.session_state.auto_refresh_enabled,
    key="auto_refresh_checkbox",
    help="Automatically refresh all pages at regular intervals"
)

# Update session state when checkbox changes
if auto_refresh != st.session_state.auto_refresh_enabled:
    st.session_state.auto_refresh_enabled = auto_refresh
    if auto_refresh:
        st.session_state.last_update_time = datetime.now()

if st.session_state.auto_refresh_enabled:
    refresh_interval = st.sidebar.slider(
        "Refresh interval (seconds)", 
        5, 60, 
        st.session_state.refresh_interval,
        step=5,
        key="refresh_interval_slider",
        help="Time between automatic page refreshes"
    )
    
    # Update interval if changed
    if refresh_interval != st.session_state.refresh_interval:
        st.session_state.refresh_interval = refresh_interval
    
    # Show last update time
    time_since_update = (datetime.now() - st.session_state.last_update_time).total_seconds()
    st.sidebar.success(f"🔄 Auto-refresh active")
    st.sidebar.caption(f"Last updated: {int(time_since_update)}s ago")
    st.sidebar.caption(f"Next refresh: {max(0, st.session_state.refresh_interval - int(time_since_update))}s")
    
    # Auto-refresh using streamlit's built-in fragment feature
    # This creates a simple JavaScript auto-reloader
    st.sidebar.markdown(
        f"""
        <script>
            setTimeout(function(){{
                window.parent.location.reload();
            }}, {st.session_state.refresh_interval * 1000});
        </script>
        """,
        unsafe_allow_html=True
    )
    
    # Update last refresh time on each render
    st.session_state.last_update_time = datetime.now()

st.sidebar.markdown("---")
st.sidebar.markdown("### System Status")

# System health check
health = api_request("GET", "/health")
if health:
    if health.get("status") == "healthy":
        st.sidebar.success("✓ System Healthy")
    else:
        st.sidebar.error("✗ System Issues")
    
    orchestrator_status = health.get("orchestrator_running", False)
    if orchestrator_status:
        st.sidebar.info("Orchestrator: Running")
    else:
        st.sidebar.warning("Orchestrator: Stopped")
else:
    st.sidebar.error("✗ API Unreachable")


# ==========================================
# Page 0: Home Dashboard
# ==========================================

if page == "🏠 Home":
    # Header with refresh button
    header_col1, header_col2, header_col3 = st.columns([3, 1, 1])
    with header_col1:
        st.title("🏠 System Overview")
    with header_col2:
        if st.button("🔄 Refresh Now", key="home_refresh", use_container_width=True):
            st.rerun()
    with header_col3:
        # Show last update time
        st.caption(f"🕐 {datetime.now().strftime('%H:%M:%S')}")
    
    st.markdown("Real-time monitoring for DLMS Multi-Meter Admin System")
    st.markdown("---")
    
    # Get system data
    meters = get_meters()
    alarms = get_alarms(acknowledged=False)
    metrics = get_metrics_summary()
    
    # ==========================================
    # KPI Cards - Improved with better styling
    # ==========================================
    
    kpi_col1, kpi_col2, kpi_col3, kpi_col4 = st.columns(4)
    
    with kpi_col1:
        total_meters = len(meters) if meters else 0
        active_meters = len([m for m in (meters or []) if m.get('status') == 'active'])
        st.metric(
            label="Total Meters",
            value=total_meters,
            delta=f"{active_meters} active"
        )
    
    with kpi_col2:
        critical_alarms = len([a for a in (alarms or []) if a.get('severity') == 'critical']) if alarms else 0
        error_alarms = len([a for a in (alarms or []) if a.get('severity') == 'error']) if alarms else 0
        st.metric(
            label="Active Alarms",
            value=len(alarms) if alarms else 0,
            delta=f"{critical_alarms} critical" if critical_alarms > 0 else "All good",
            delta_color="inverse"
        )
    
    with kpi_col3:
        if metrics and 'total_readings' in metrics:
            total_readings = metrics['total_readings']
            success_rate = metrics.get('success_rate', 0) * 100
            st.metric(
                label="Total Readings",
                value=f"{total_readings:,}",
                delta=f"{success_rate:.1f}% success"
            )
        else:
            st.metric(label="Total Readings", value="N/A")
    
    with kpi_col4:
        if metrics and 'avg_read_time' in metrics:
            avg_time = metrics['avg_read_time']
            st.metric(
                label="Avg Read Time",
                value=f"{avg_time:.2f}s",
                delta="Optimized" if avg_time < 2.0 else "Normal"
            )
        else:
            st.metric(label="Avg Read Time", value="N/A")
    
    st.markdown("---")
    
    # ==========================================
    # System Health & Quick Stats Row
    # ==========================================
    
    health_col1, health_col2, health_col3 = st.columns([2, 2, 2])
    
    with health_col1:
        st.markdown("### 💚 System Health")
        if health and health.get('status') == 'healthy':
            st.success("✅ **All Systems Operational**")
            st.caption(f"Orchestrator: {'Running ✓' if health.get('orchestrator_running') else 'Stopped ✗'}")
        else:
            st.error("❌ **System Issues Detected**")
            st.caption("Check alarms for details")
    
    with health_col2:
        st.markdown("### 📊 Alarm Distribution")
        if alarms and len(alarms) > 0:
            severity_counts = {}
            for alarm in alarms:
                sev = alarm.get('severity', 'unknown')
                severity_counts[sev] = severity_counts.get(sev, 0) + 1
            
            # Create compact pie chart
            fig = go.Figure(data=[go.Pie(
                labels=list(severity_counts.keys()),
                values=list(severity_counts.values()),
                marker=dict(colors=['#FF4B4B', '#FFA500', '#FFD700', '#4B9EFF']),
                hole=0.5,
                textposition='inside',
                textinfo='label+value'
            )])
            
            fig.update_layout(
                height=180,
                margin=dict(l=0, r=0, t=0, b=0),
                showlegend=False
            )
            
            st.plotly_chart(fig, use_container_width=True, key="alarm_dist_pie")
        else:
            st.success("✅ No active alarms")
    
    with health_col3:
        st.markdown("### ⚡ Quick Actions")
        if st.button("🔍 Scan Network", key="quick_scan", use_container_width=True):
            st.info("→ Switch to Discovery page")
        if st.button("⚙️ Manage Meters", key="quick_config", use_container_width=True):
            st.info("→ Switch to Configuration page")
        if st.button("📊 View Metrics", key="quick_monitor", use_container_width=True):
            st.info("→ Switch to Monitoring page")
    
    st.markdown("---")
    
    # ==========================================
    # Main Content: Meters & Alarms
    # ==========================================
    
    # Meters Status Table - Full width for better visibility
    st.subheader("🔌 Active Meters")
    
    if meters and len(meters) > 0:
        # Create meters table with better formatting
        meters_data = []
        for meter in meters:
            status_emoji = {
                'active': '🟢',
                'inactive': '⚪',
                'error': '🔴',
                'starting': '🟡'
            }.get(meter.get('status', 'unknown'), '❓')
            
            # Get detailed status
            meter_status = get_meter_status(meter['id'])
            uptime = "Offline"
            running_badge = "⚪"
            
            if meter_status and meter_status.get('running'):
                uptime_sec = meter_status.get('uptime', 0)
                if uptime_sec < 60:
                    uptime = f"{uptime_sec:.0f}s"
                elif uptime_sec < 3600:
                    uptime = f"{uptime_sec/60:.1f}m"
                else:
                    uptime = f"{uptime_sec/3600:.1f}h"
                running_badge = "🟢 Running"
            elif meter.get('status') == 'active':
                running_badge = "🟡 Starting"
            else:
                running_badge = "⚪ Stopped"
            
            meters_data.append({
                '': status_emoji,
                'Meter Name': meter.get('name', 'Unknown'),
                'Address': f"{meter.get('ip_address')}:{meter.get('port')}",
                'Running': running_badge,
                'Uptime': uptime,
                'Errors': f"{'🔴' if meter.get('error_count', 0) > 10 else '🟡' if meter.get('error_count', 0) > 0 else '🟢'} {meter.get('error_count', 0)}",
                'Process ID': meter.get('process_id', 'N/A')
            })
        
        df_meters = pd.DataFrame(meters_data)
        st.dataframe(
            df_meters, 
            use_container_width=True, 
            hide_index=True,
            height=min(len(meters) * 35 + 38, 400)  # Dynamic height
        )
    else:
        st.info("📭 No meters configured. Go to **Discovery** page to scan and add meters.")
    
    st.markdown("---")
    
    # ==========================================
    # Recent Alarms - Two Column Layout
    # ==========================================
    
    alarm_header_col1, alarm_header_col2 = st.columns([4, 1])
    with alarm_header_col1:
        st.subheader("🚨 Recent Alarms & Issues")
    with alarm_header_col2:
        if alarms and len(alarms) > 0:
            st.caption(f"{len(alarms)} active alarms")
    
    if alarms and len(alarms) > 0:
        # Split alarms into two columns for better space usage
        alarm_col1, alarm_col2 = st.columns(2)
        
        # Sort alarms by timestamp
        recent_alarms = sorted(alarms, key=lambda x: x.get('timestamp', ''), reverse=True)[:12]
        mid_point = len(recent_alarms) // 2
        
        # Left column alarms
        with alarm_col1:
            alarms_data_left = []
            for alarm in recent_alarms[:mid_point]:
                severity_emoji = {
                    'critical': '🔴',
                    'error': '🟠',
                    'warning': '🟡',
                    'info': '🔵'
                }.get(alarm.get('severity', 'info'), '⚪')
                
                # Format timestamp
                timestamp_str = alarm.get('timestamp', '')
                try:
                    dt = datetime.fromisoformat(timestamp_str)
                    time_ago = datetime.now() - dt
                    if time_ago.total_seconds() < 60:
                        time_display = f"{int(time_ago.total_seconds())}s"
                    elif time_ago.total_seconds() < 3600:
                        time_display = f"{int(time_ago.total_seconds()/60)}m"
                    elif time_ago.total_seconds() < 86400:
                        time_display = f"{int(time_ago.total_seconds()/3600)}h"
                    else:
                        time_display = f"{int(time_ago.total_seconds()/86400)}d"
                except:
                    time_display = "?"
                
                message = alarm.get('message', 'No message')
                if len(message) > 50:
                    message = message[:47] + '...'
                
                alarms_data_left.append({
                    '': severity_emoji,
                    'Age': time_display,
                    'Category': alarm.get('category', 'N/A'),
                    'Message': message
                })
            
            if alarms_data_left:
                df_alarms_left = pd.DataFrame(alarms_data_left)
                st.dataframe(
                    df_alarms_left, 
                    use_container_width=True, 
                    hide_index=True,
                    height=min(len(alarms_data_left) * 35 + 38, 300)
                )
        
        # Right column alarms
        with alarm_col2:
            alarms_data_right = []
            for alarm in recent_alarms[mid_point:]:
                severity_emoji = {
                    'critical': '🔴',
                    'error': '🟠',
                    'warning': '🟡',
                    'info': '🔵'
                }.get(alarm.get('severity', 'info'), '⚪')
                
                # Format timestamp
                timestamp_str = alarm.get('timestamp', '')
                try:
                    dt = datetime.fromisoformat(timestamp_str)
                    time_ago = datetime.now() - dt
                    if time_ago.total_seconds() < 60:
                        time_display = f"{int(time_ago.total_seconds())}s"
                    elif time_ago.total_seconds() < 3600:
                        time_display = f"{int(time_ago.total_seconds()/60)}m"
                    elif time_ago.total_seconds() < 86400:
                        time_display = f"{int(time_ago.total_seconds()/3600)}h"
                    else:
                        time_display = f"{int(time_ago.total_seconds()/86400)}d"
                except:
                    time_display = "?"
                
                message = alarm.get('message', 'No message')
                if len(message) > 50:
                    message = message[:47] + '...'
                
                alarms_data_right.append({
                    '': severity_emoji,
                    'Age': time_display,
                    'Category': alarm.get('category', 'N/A'),
                    'Message': message
                })
            
            if alarms_data_right:
                df_alarms_right = pd.DataFrame(alarms_data_right)
                st.dataframe(
                    df_alarms_right, 
                    use_container_width=True, 
                    hide_index=True,
                    height=min(len(alarms_data_right) * 35 + 38, 300)
                )
        
        # Link to full alarms page
        st.caption("💡 Go to **Alarms** page for detailed alarm management")
        
    else:
        st.success("✅ No active alarms - System running smoothly!")
    
    
    # ==========================================
    # Error Summary Chart - Full Width
    # ==========================================
    
    st.markdown("---")
    st.subheader("⚠️ Error Summary by Meter")
    
    if meters and len(meters) > 0:
        # Prepare error data for all meters
        error_data = []
        for meter in meters:
            error_count = meter.get('error_count', 0)
            error_data.append({
                'Meter': meter.get('name', 'Unknown')[:25],
                'Errors': error_count,
                'Status': meter.get('status', 'unknown')
            })
        
        if any(m['Errors'] > 0 for m in error_data):
            df_errors = pd.DataFrame(error_data)
            
            # Create improved bar chart
            fig = px.bar(
                df_errors,
                x='Meter',
                y='Errors',
                color='Errors',
                color_continuous_scale='Reds',
                text='Errors',
                title=None
            )
            
            fig.update_traces(textposition='outside')
            fig.update_layout(
                height=300,
                margin=dict(l=20, r=20, t=20, b=80),
                showlegend=False,
                xaxis_title="Meter Name",
                yaxis_title="Error Count"
            )
            
            st.plotly_chart(fig, use_container_width=True, key="error_summary_chart")
        else:
            st.success("🎉 **Excellent!** No errors detected on any meter")
    else:
        st.info("📭 No meters available to analyze")
    
    # ==========================================
    # Footer
    # ==========================================
    
    st.markdown("---")
    footer_col1, footer_col2, footer_col3 = st.columns(3)
    
    with footer_col1:
        st.caption("� **Tip:** Enable auto-refresh in the sidebar for real-time monitoring")
    with footer_col2:
        st.caption(f"🕐 Last updated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    with footer_col3:
        if st.button("� View API Docs", key="api_docs_link", use_container_width=True):
            st.markdown("[Open API Documentation](http://localhost:8000/docs)")


# ==========================================
# Page 1: Discovery
# ==========================================

if page == "🔍 Discovery":
    st.title("🔍 Network Discovery")
    st.markdown("Scan your network to discover DLMS meters")
    
    col1, col2 = st.columns([2, 1])
    
    with col1:
        st.subheader("Network Scanner")
        
        # Scan configuration
        scan_col1, scan_col2 = st.columns(2)
        with scan_col1:
            start_ip = st.text_input("Start IP", value="192.168.1.1")
        with scan_col2:
            end_ip = st.text_input("End IP", value="192.168.1.254")
        
        ports = st.multiselect(
            "Ports to scan",
            options=[3333, 4059, 4061],
            default=[3333]
        )
        
        if st.button("🔍 Scan Network", type="primary"):
            with st.spinner("Scanning network... This may take a few minutes."):
                result = api_request("POST", "/scan/network", json={
                    "start_ip": start_ip,
                    "end_ip": end_ip,
                    "ports": ports
                })
                
                if result:
                    st.success(f"✓ Scan complete! Found {result['discovered_count']} meters")
                    
                    if result['meters']:
                        st.session_state['discovered_meters'] = result['meters']
    
    with col2:
        st.subheader("Quick Scan")
        st.markdown("Scan specific IP addresses")
        
        quick_ips = st.text_area(
            "IP Addresses (one per line)",
            value="192.168.1.127",
            height=100
        )
        
        if st.button("Quick Scan"):
            ip_list = [ip.strip() for ip in quick_ips.split('\n') if ip.strip()]
            with st.spinner(f"Scanning {len(ip_list)} IPs..."):
                # Use scan_network with same start/end for each IP
                discovered = []
                for ip in ip_list:
                    result = api_request("POST", "/scan/network", json={
                        "start_ip": ip,
                        "end_ip": ip,
                        "ports": [3333]
                    })
                    if result and result['meters']:
                        discovered.extend(result['meters'])
                
                if discovered:
                    st.success(f"Found {len(discovered)} meters")
                    st.session_state['discovered_meters'] = discovered
                else:
                    st.info("No meters found")
    
    # Display discovered meters
    st.markdown("---")
    st.subheader("Discovered Meters")
    
    if 'discovered_meters' in st.session_state and st.session_state['discovered_meters']:
        meters = st.session_state['discovered_meters']
        
        df = pd.DataFrame(meters)
        st.dataframe(df, use_container_width=True)
        
        st.markdown("### Add Meters to System")
        
        for i, meter in enumerate(meters):
            with st.expander(f"📍 {meter['ip_address']}:{meter['port']} ({meter['response_time']:.0f}ms)"):
                col1, col2 = st.columns([3, 1])
                
                with col1:
                    meter_name = st.text_input(
                        "Meter Name",
                        value=f"meter_{meter['ip_address'].replace('.', '_')}",
                        key=f"name_{i}"
                    )
                
                with col2:
                    if st.button("➕ Add", key=f"add_{i}"):
                        result = api_request("POST", "/meters", json={
                            "name": meter_name,
                            "ip_address": meter['ip_address'],
                            "port": meter['port'],
                            "client_id": 1,
                            "server_id": 1
                        })
                        
                        if result:
                            st.success(f"✓ Added {meter_name}")
                            time.sleep(1)
                            st.rerun()
    else:
        st.info("No meters discovered yet. Use the scanner above.")


# Page 2: Configuration

elif page == "⚙️ Configuration":
    st.title("⚙️ Meter Configuration")
    st.markdown("Configure meters and their measurements")
    
    meters = get_meters()
    
    if not meters:
        st.info("No meters configured. Go to Discovery to add meters.")
    else:
        # Meter selector
        meter_options = {f"{m['name']} ({m['ip_address']})": m['id'] for m in meters}
        selected_meter_key = st.selectbox("Select Meter", list(meter_options.keys()))
        selected_meter_id = meter_options[selected_meter_key]
        
        # Get selected meter details
        selected_meter = next(m for m in meters if m['id'] == selected_meter_id)
        
        # Meter details
        col1, col2, col3 = st.columns(3)
        with col1:
            st.metric("Status", selected_meter['status'].upper())
        with col2:
            st.metric("Error Count", selected_meter['error_count'])
        with col3:
            if selected_meter['last_seen']:
                last_seen = datetime.fromisoformat(selected_meter['last_seen'])
                ago = datetime.now() - last_seen
                st.metric("Last Seen", f"{ago.total_seconds():.0f}s ago")
            else:
                st.metric("Last Seen", "Never")
        
        st.markdown("---")
        
        # Control buttons
        col1, col2, col3, col4 = st.columns(4)
        
        with col1:
            if st.button("▶️ Start", type="primary", use_container_width=True):
                result = api_request("POST", f"/meters/{selected_meter_id}/start")
                if result:
                    st.success("Meter started")
                    time.sleep(1)
                    st.rerun()
        
        with col2:
            if st.button("⏸️ Stop", use_container_width=True):
                result = api_request("POST", f"/meters/{selected_meter_id}/stop")
                if result:
                    st.success("Meter stopped")
                    time.sleep(1)
                    st.rerun()
        
        with col3:
            if st.button("🔄 Restart", use_container_width=True):
                result = api_request("POST", f"/meters/{selected_meter_id}/restart")
                if result:
                    st.success("Meter restarted")
                    time.sleep(1)
                    st.rerun()
        
        with col4:
            if st.button("🗑️ Delete", type="secondary", use_container_width=True):
                if st.session_state.get('confirm_delete', False):
                    result = api_request("DELETE", f"/meters/{selected_meter_id}")
                    if result is not None:  # 204 returns None
                        st.success("Meter deleted")
                        time.sleep(1)
                        st.rerun()
                else:
                    st.session_state['confirm_delete'] = True
                    st.warning("Click again to confirm deletion")
        
        st.markdown("---")
        
        # ThingsBoard Configuration Section
        st.subheader("📡 ThingsBoard Configuration")
        
        # Get current ThingsBoard config
        tb_config = api_request("GET", f"/meters/{selected_meter_id}/thingsboard")
        
        if tb_config:
            with st.expander("🔧 Configure ThingsBoard Connection", expanded=False):
                col1, col2 = st.columns(2)
                
                with col1:
                    tb_enabled = st.checkbox(
                        "Enable ThingsBoard", 
                        value=tb_config.get('tb_enabled', True),
                        help="Enable/disable data publishing to ThingsBoard"
                    )
                    
                    tb_host = st.text_input(
                        "ThingsBoard Host",
                        value=tb_config.get('tb_host', 'thingsboard.cloud'),
                        help="Hostname or IP address (e.g., localhost, thingsboard.cloud)"
                    )
                    
                    tb_port = st.number_input(
                        "MQTT Port",
                        min_value=1,
                        max_value=65535,
                        value=tb_config.get('tb_port', 1883),
                        help="MQTT port (default: 1883)"
                    )
                
                with col2:
                    tb_token = st.text_input(
                        "Device Access Token",
                        value=tb_config.get('tb_token', ''),
                        type="password",
                        help="Access token from ThingsBoard device"
                    )
                    
                    tb_device_name = st.text_input(
                        "Device Name (optional)",
                        value=tb_config.get('tb_device_name', ''),
                        help="Friendly name for the device"
                    )
                
                col1, col2, col3 = st.columns(3)
                
                with col1:
                    if st.button("💾 Save ThingsBoard Config", type="primary", use_container_width=True):
                        result = api_request("PUT", f"/meters/{selected_meter_id}/thingsboard", json={
                            "tb_enabled": tb_enabled,
                            "tb_host": tb_host,
                            "tb_port": tb_port,
                            "tb_token": tb_token if tb_token else None,
                            "tb_device_name": tb_device_name if tb_device_name else None
                        })
                        
                        if result:
                            st.success("✅ ThingsBoard configuration saved")
                            time.sleep(1)
                            st.rerun()
                
                with col2:
                    if st.button("🧪 Test Connection", use_container_width=True):
                        test_result = api_request("GET", f"/meters/{selected_meter_id}/thingsboard/test")
                        
                        if test_result:
                            st.markdown("---")
                            st.markdown("**Test Results:**")
                            
                            status = test_result.get('status', 'unknown')
                            
                            if status == 'meter_not_running':
                                st.warning("⚠️ Meter is not running. Start the meter to test.")
                            elif status == 'not_configured':
                                st.warning("⚠️ ThingsBoard token not configured")
                            elif status == 'test_complete':
                                dns = test_result.get('dns_resolution', 'unknown')
                                port = test_result.get('port_connectivity', 'unknown')
                                can_publish = test_result.get('can_publish', False)
                                messages = test_result.get('messages_sent_total', 0)
                                
                                col_a, col_b = st.columns(2)
                                with col_a:
                                    if dns == 'ok':
                                        st.success(f"✅ DNS: {test_result.get('tb_host')}")
                                    else:
                                        st.error(f"❌ DNS Resolution Failed")
                                
                                with col_b:
                                    if port == 'ok':
                                        st.success(f"✅ Port {test_result.get('tb_port')} Open")
                                    else:
                                        st.error(f"❌ Port {test_result.get('tb_port')} Closed")
                                
                                if can_publish:
                                    st.success(f"✅ Can Publish Data")
                                    st.info(f"📊 Messages sent: {messages}")
                                else:
                                    st.error("❌ Cannot publish data")
                            else:
                                st.error(f"❌ Error: {test_result.get('message', 'Unknown error')}")
                
                with col3:
                    if st.button("📖 View Docs", use_container_width=True):
                        st.markdown("""
                        **Quick Guide:**
                        1. Create device in ThingsBoard
                        2. Copy Access Token
                        3. Paste token above
                        4. Set host (localhost or IP)
                        5. Save configuration
                        6. Test connection
                        7. Start meter to publish data
                        """)
            
            # Show current status
            if tb_config.get('tb_token'):
                st.success(f"✅ ThingsBoard configured: {tb_config.get('tb_host')}:{tb_config.get('tb_port')}")
            else:
                st.warning("⚠️ ThingsBoard token not configured. Configure above to enable data publishing.")
        
        st.markdown("---")
        
        # Measurement configuration
        st.subheader("Measurements Configuration")
        
        # Get current config
        config = api_request("GET", f"/meters/{selected_meter_id}/config")
        
        if config:
            df_config = pd.DataFrame(config)
            st.dataframe(df_config, use_container_width=True)
        
        # Add new measurement
        with st.expander("➕ Add New Measurement"):
            col1, col2 = st.columns(2)
            
            with col1:
                measurement_name = st.selectbox(
                    "Measurement Type",
                    ["voltage_l1", "voltage_l2", "voltage_l3",
                     "current_l1", "current_l2", "current_l3",
                     "frequency", "active_power", "reactive_power",
                     "apparent_power", "power_factor", "active_energy"]
                )
                
                obis_code = st.text_input("OBIS Code", value="1.0.32.7.0.255")
            
            with col2:
                enabled = st.checkbox("Enabled", value=True)
                sampling_interval = st.number_input("Sampling Interval (s)", min_value=0.1, value=1.0, step=0.1)
            
            if st.button("Add Measurement"):
                result = api_request("POST", f"/meters/{selected_meter_id}/config", json={
                    "measurement_name": measurement_name,
                    "obis_code": obis_code,
                    "enabled": enabled,
                    "sampling_interval": sampling_interval
                })
                
                if result:
                    st.success("Measurement added")
                    time.sleep(1)
                    st.rerun()


# Page 3: Monitoring

elif page == "📊 Monitoring":
    st.title("📊 Real-Time Monitoring")
    st.markdown("Monitor meter performance and metrics")
    
    # Auto-refresh
    auto_refresh = st.sidebar.checkbox("Auto-refresh (5s)", value=True)
    
    # Get metrics summary
    summary = get_metrics_summary()
    
    if not summary:
        st.info("No meters configured or no data available.")
    else:
        # Overall statistics
        st.subheader("System Overview")
        
        total_meters = len(summary)
        active_meters = sum(1 for m in summary if m['status'] == 'active')
        error_meters = sum(1 for m in summary if m['error_count'] > 0)
        
        col1, col2, col3, col4 = st.columns(4)
        with col1:
            st.metric("Total Meters", total_meters)
        with col2:
            st.metric("Active", active_meters, delta=None)
        with col3:
            st.metric("Errors", error_meters, delta=None, delta_color="inverse")
        with col4:
            avg_success = sum(m['latest_metric']['success_rate'] for m in summary if m['latest_metric']) / len([m for m in summary if m['latest_metric']]) if any(m['latest_metric'] for m in summary) else 0
            st.metric("Avg Success Rate", f"{avg_success:.1f}%")
        
        st.markdown("---")
        
        # Meter grid
        st.subheader("Meter Status")
        
        cols = st.columns(3)
        for i, meter_data in enumerate(summary):
            with cols[i % 3]:
                with st.container():
                    # Status indicator
                    status = meter_data['status']
                    if status == 'active':
                        status_color = "🟢"
                    elif status == 'inactive':
                        status_color = "⚪"
                    else:
                        status_color = "🔴"
                    
                    st.markdown(f"### {status_color} {meter_data['meter_name']}")
                    
                    if meter_data['latest_metric']:
                        metric = meter_data['latest_metric']
                        
                        st.metric("Read Time", f"{metric['avg_read_time']:.2f}s")
                        st.metric("Success Rate", f"{metric['success_rate']:.1f}%")
                        st.metric("Total Reads", metric['total_reads'])
                        
                        if metric['messages_sent']:
                            st.metric("MQTT Messages", metric['messages_sent'])
                    else:
                        st.info("No metrics yet")
                    
                    if meter_data['error_count'] > 0:
                        st.warning(f"⚠️ {meter_data['error_count']} errors")
        
        st.markdown("---")
        
        # Performance charts
        st.subheader("Performance Charts")
        
        # Get detailed metrics for active meters
        meters = get_meters()
        
        if meters:
            selected_meter_for_chart = st.selectbox(
                "Select meter for detailed view",
                [f"{m['name']} ({m['ip_address']})" for m in meters]
            )
            
            meter_id = next(m['id'] for m in meters if f"{m['name']} ({m['ip_address']})" == selected_meter_for_chart)
            
            # Get metrics
            metrics = api_request("GET", f"/meters/{meter_id}/metrics", params={"limit": 100})
            
            if metrics:
                df_metrics = pd.DataFrame(metrics)
                df_metrics['timestamp'] = pd.to_datetime(df_metrics['timestamp'])
                
                # Charts
                col1, col2 = st.columns(2)
                
                with col1:
                    # Read time chart
                    fig_time = px.line(
                        df_metrics,
                        x='timestamp',
                        y='avg_read_time',
                        title='Average Read Time',
                        labels={'avg_read_time': 'Time (s)', 'timestamp': 'Time'}
                    )
                    fig_time.update_traces(line_color='#1f77b4')
                    st.plotly_chart(fig_time, use_container_width=True)
                
                with col2:
                    # Success rate chart
                    fig_success = px.line(
                        df_metrics,
                        x='timestamp',
                        y='success_rate',
                        title='Success Rate',
                        labels={'success_rate': 'Success (%)', 'timestamp': 'Time'}
                    )
                    fig_success.update_traces(line_color='#2ca02c')
                    fig_success.update_yaxes(range=[0, 100])
                    st.plotly_chart(fig_success, use_container_width=True)
            else:
                st.info("⏳ Performance metrics will appear after the first monitoring cycle completes.")
            
            # Network Statistics Section
            st.markdown("---")
            st.subheader("📡 Network Statistics")
            
            try:
                # Get network stats for selected meter
                network_stats = api_request("GET", f"/meters/{meter_id}/network_stats", timeout=5)
                
                if network_stats and isinstance(network_stats, dict):
                    # Real-Time Bandwidth (in Mb/s)
                    st.markdown("#### 📊 Real-Time Bandwidth")
                    col1, col2, col3, col4 = st.columns(4)
                    
                    with col1:
                        st.metric(
                            "🔼 Upload", 
                            f"{network_stats.get('current', {}).get('bandwidth_tx_mbps', 0):.3f} Mb/s"
                        )
                    with col2:
                        st.metric(
                            "🔽 Download", 
                            f"{network_stats.get('current', {}).get('bandwidth_rx_mbps', 0):.3f} Mb/s"
                        )
                    with col3:
                        st.metric(
                            "📶 Total Bandwidth", 
                            f"{network_stats.get('current', {}).get('bandwidth_total_mbps', 0):.3f} Mb/s"
                        )
                    with col4:
                        st.metric(
                            "📦 Packets/sec", 
                            f"{network_stats.get('current', {}).get('packets_total_ps', 0):.0f}"
                        )
                    
                    # DLMS Protocol Statistics
                    st.markdown("#### 🔌 DLMS Protocol Statistics")
                    col1, col2, col3, col4 = st.columns(4)
                    
                    with col1:
                        st.metric(
                            "Requests Sent", 
                            f"{network_stats.get('application', {}).get('dlms_requests_sent', 0):,}"
                        )
                    with col2:
                        st.metric(
                            "Responses Received", 
                            f"{network_stats.get('application', {}).get('dlms_responses_recv', 0):,}"
                        )
                    with col3:
                        st.metric(
                            "Avg Payload", 
                            f"{network_stats.get('application', {}).get('dlms_avg_payload_size', 0):.0f} bytes"
                        )
                    with col4:
                        st.metric(
                            "Success Rate", 
                            f"{(network_stats.get('application', {}).get('dlms_responses_recv', 0) / max(network_stats.get('application', {}).get('dlms_requests_sent', 1), 1) * 100):.1f}%"
                        )
                    
                    # MQTT Statistics
                    st.markdown("#### 📤 MQTT Publishing")
                    col1, col2, col3 = st.columns(3)
                    
                    with col1:
                        st.metric(
                            "Messages Sent", 
                            f"{network_stats.get('application', {}).get('mqtt_messages_sent', 0):,}"
                        )
                    with col2:
                        st.metric(
                            "Total Data Sent", 
                            f"{network_stats.get('application', {}).get('mqtt_total_bytes_sent', 0) / 1024:.2f} KB"
                        )
                    with col3:
                        st.metric(
                            "Avg Message Size", 
                            f"{network_stats.get('application', {}).get('mqtt_total_bytes_sent', 0) / max(network_stats.get('application', {}).get('mqtt_messages_sent', 1), 1):.0f} bytes"
                        )
                    
                    # Network Performance Averages
                    st.markdown("#### 📈 Network Performance Averages")
                    col1, col2, col3, col4 = st.columns(4)
                    
                    with col1:
                        st.metric(
                            "Avg Upload", 
                            f"{network_stats.get('averages', {}).get('bandwidth_tx_mbps', 0):.3f} Mb/s"
                        )
                    with col2:
                        st.metric(
                            "Avg Download", 
                            f"{network_stats.get('averages', {}).get('bandwidth_rx_mbps', 0):.3f} Mb/s"
                        )
                    with col3:
                        st.metric(
                            "Peak Upload", 
                            f"{network_stats.get('peaks', {}).get('bandwidth_tx_mbps', 0):.3f} Mb/s"
                        )
                    with col4:
                        st.metric(
                            "Peak Download", 
                            f"{network_stats.get('peaks', {}).get('bandwidth_rx_mbps', 0):.3f} Mb/s"
                        )
                    
                    # Bandwidth History Chart
                    if network_stats.get('history', {}).get('timestamp'):
                        st.markdown("#### 📊 Bandwidth History")
                        
                        df_network = pd.DataFrame({
                            'timestamp': pd.to_datetime(network_stats['history']['timestamp']),
                            'Upload (Mb/s)': network_stats['history']['bandwidth_tx_mbps'],
                            'Download (Mb/s)': network_stats['history']['bandwidth_rx_mbps']
                        })
                        
                        fig_bandwidth = px.line(
                            df_network,
                            x='timestamp',
                            y=['Upload (Mb/s)', 'Download (Mb/s)'],
                            title='Network Bandwidth Over Time',
                            labels={'value': 'Bandwidth (Mb/s)', 'timestamp': 'Time', 'variable': 'Direction'}
                        )
                        fig_bandwidth.update_traces(mode='lines+markers')
                        fig_bandwidth.update_layout(hovermode='x unified')
                        st.plotly_chart(fig_bandwidth, use_container_width=True)
                        
                        # Packet Rate Chart
                        if network_stats['history'].get('packets_tx_ps'):
                            st.markdown("#### 📦 Packet Rate History")
                            
                            df_packets = pd.DataFrame({
                                'timestamp': pd.to_datetime(network_stats['history']['timestamp']),
                                'TX Packets/s': network_stats['history']['packets_tx_ps'],
                                'RX Packets/s': network_stats['history']['packets_rx_ps']
                            })
                            
                            fig_packets = px.line(
                                df_packets,
                                x='timestamp',
                                y=['TX Packets/s', 'RX Packets/s'],
                                title='Packet Rate Over Time',
                                labels={'value': 'Packets/sec', 'timestamp': 'Time', 'variable': 'Direction'}
                            )
                            fig_packets.update_traces(mode='lines')
                            fig_packets.update_layout(hovermode='x unified')
                            st.plotly_chart(fig_packets, use_container_width=True)
                    else:
                        st.info("📊 Network history will populate over time (data collected every 60 seconds)")
                    
                else:
                    st.warning("⚠️ No network statistics data available yet")
                    st.info("The system collects network metrics every 60 seconds. Please wait for the first data collection cycle.")
                
            except Exception as e:
                st.error(f"❌ Error loading network statistics: {str(e)}")
                st.info("Please verify that the API service is running and the meter is active.")
    

    # Auto-refresh
    if auto_refresh:
        time.sleep(5)
        st.rerun()


# Page 4: Alarms

elif page == "🚨 Alarms":
    st.title("🚨 Alarms & Warnings")
    st.markdown("Monitor and manage system alarms")
    
    # Filter options
    col1, col2, col3 = st.columns(3)
    
    with col1:
        show_acknowledged = st.checkbox("Show Acknowledged", value=False)
    
    with col2:
        severity_filter = st.selectbox(
            "Severity Filter",
            ["All", "critical", "error", "warning", "info"]
        )
    
    with col3:
        if st.button("🔄 Refresh"):
            st.rerun()
    
    # Get alarms
    alarms = get_alarms(acknowledged=show_acknowledged)
    
    if not alarms:
        st.success("✓ No alarms!")
    else:
        # Filter by severity
        if severity_filter != "All":
            alarms = [a for a in alarms if a['severity'] == severity_filter]
        
        # Statistics
        col1, col2, col3, col4 = st.columns(4)
        
        critical_count = sum(1 for a in alarms if a['severity'] == 'critical')
        error_count = sum(1 for a in alarms if a['severity'] == 'error')
        warning_count = sum(1 for a in alarms if a['severity'] == 'warning')
        info_count = sum(1 for a in alarms if a['severity'] == 'info')
        
        with col1:
            st.metric("Critical", critical_count)
        with col2:
            st.metric("Errors", error_count)
        with col3:
            st.metric("Warnings", warning_count)
        with col4:
            st.metric("Info", info_count)
        
        st.markdown("---")
        
        # Alarm list
        st.subheader(f"Alarms ({len(alarms)})")
        
        for alarm in alarms:
            # Severity icon
            severity_icons = {
                'critical': '🔴',
                'error': '🟠',
                'warning': '🟡',
                'info': '🔵'
            }
            icon = severity_icons.get(alarm['severity'], '⚪')
            
            # Timestamp
            timestamp = datetime.fromisoformat(alarm['timestamp'])
            ago = datetime.now() - timestamp
            time_ago = f"{ago.total_seconds() / 60:.0f}m ago" if ago.total_seconds() < 3600 else f"{ago.total_seconds() / 3600:.1f}h ago"
            
            with st.expander(f"{icon} {alarm['message']} - {time_ago}"):
                col1, col2 = st.columns([3, 1])
                
                with col1:
                    st.write(f"**Severity:** {alarm['severity'].upper()}")
                    st.write(f"**Category:** {alarm['category']}")
                    st.write(f"**Meter ID:** {alarm['meter_id']}")
                    st.write(f"**Time:** {timestamp.strftime('%Y-%m-%d %H:%M:%S')}")
                    
                    if alarm.get('details'):
                        st.write(f"**Details:** {alarm['details']}")
                
                with col2:
                    if not alarm['acknowledged']:
                        if st.button("✓ Acknowledge", key=f"ack_{alarm['id']}"):
                            result = api_request("POST", f"/alarms/{alarm['id']}/acknowledge")
                            if result:
                                st.success("Acknowledged")
                                time.sleep(1)
                                st.rerun()
                    else:
                        st.success("✓ Acknowledged")


# Page 5: Diagnósticos DLMS
elif page == "🔴 Diagnósticos":
    # Importar el módulo de alertas
    try:
        from admin.dashboard_alerts import render_alerts_page
        render_alerts_page()
    except Exception as e:
        st.error(f"Error cargando módulo de diagnósticos: {e}")
        st.info("Asegúrate de que admin/dashboard_alerts.py existe y está correctamente configurado")


# Footer
st.sidebar.markdown("---")
st.sidebar.markdown("**DLMS Multi-Meter Admin v1.0**")
st.sidebar.markdown("Built with Streamlit")
