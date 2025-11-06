#!/usr/bin/env python3
"""
Network Statistics Monitor for DLMS Bridge
Captures network metrics: bandwidth, packet counts, latency, payload size
"""

import psutil
import time
import threading
from datetime import datetime
from typing import Dict, Optional, List
from collections import deque
import logging

logger = logging.getLogger(__name__)


class NetworkMonitor:
    """Monitor network statistics for DLMS communication"""
    
    def __init__(self, interface: str = None, history_size: int = 300):
        """
        Args:
            interface: Network interface to monitor (None = all interfaces)
            history_size: Number of samples to keep in history (300 = 5min at 1s interval)
        """
        self.interface = interface
        self.history_size = history_size
        
        # Current statistics
        self.current_stats = {
            'bytes_sent': 0,
            'bytes_recv': 0,
            'packets_sent': 0,
            'packets_recv': 0,
            'errors_in': 0,
            'errors_out': 0,
            'drops_in': 0,
            'drops_out': 0,
        }
        
        # Rate statistics (per second)
        self.rates = {
            'bandwidth_tx_bps': 0.0,  # bits per second transmitted
            'bandwidth_rx_bps': 0.0,  # bits per second received
            'bandwidth_total_bps': 0.0,
            'packets_tx_ps': 0.0,     # packets per second transmitted
            'packets_rx_ps': 0.0,     # packets per second received
            'packets_total_ps': 0.0,
        }
        
        # Historical data (time series)
        self.history = {
            'timestamp': deque(maxlen=history_size),
            'bandwidth_tx': deque(maxlen=history_size),
            'bandwidth_rx': deque(maxlen=history_size),
            'packets_tx': deque(maxlen=history_size),
            'packets_rx': deque(maxlen=history_size),
            'errors': deque(maxlen=history_size),
        }
        
        # Application-level statistics (DLMS-specific)
        self.app_stats = {
            'dlms_requests_sent': 0,
            'dlms_responses_recv': 0,
            'dlms_avg_payload_size': 0,
            'dlms_total_bytes_sent': 0,
            'dlms_total_bytes_recv': 0,
            'mqtt_messages_sent': 0,
            'mqtt_total_bytes_sent': 0,
        }
        
        # Monitoring state
        self._monitoring = False
        self._monitor_thread = None
        self._last_stats = None
        self._last_update = None
        
        # Initialize
        self._initialize()
    
    def _initialize(self):
        """Initialize network counters"""
        try:
            if self.interface:
                stats = psutil.net_io_counters(pernic=True).get(self.interface)
                if not stats:
                    logger.warning(f"Interface {self.interface} not found, using all interfaces")
                    stats = psutil.net_io_counters()
            else:
                stats = psutil.net_io_counters()
            
            self._last_stats = stats
            self._last_update = time.time()
            
            logger.info(f"✓ Network monitor initialized (interface: {self.interface or 'all'})")
        except Exception as e:
            logger.error(f"Failed to initialize network monitor: {e}")
    
    def start_monitoring(self, interval: float = 1.0):
        """Start background monitoring thread"""
        if self._monitoring:
            logger.warning("Network monitor already running")
            return
        
        self._monitoring = True
        self._monitor_thread = threading.Thread(
            target=self._monitor_loop,
            args=(interval,),
            daemon=True
        )
        self._monitor_thread.start()
        logger.info(f"✓ Network monitoring started (interval: {interval}s)")
    
    def stop_monitoring(self):
        """Stop background monitoring"""
        self._monitoring = False
        if self._monitor_thread:
            self._monitor_thread.join(timeout=2.0)
        logger.info("✓ Network monitoring stopped")
    
    def _monitor_loop(self, interval: float):
        """Background monitoring loop"""
        while self._monitoring:
            try:
                self._update_stats()
                time.sleep(interval)
            except Exception as e:
                logger.error(f"Error in monitoring loop: {e}")
                time.sleep(interval)
    
    def _update_stats(self):
        """Update network statistics"""
        try:
            # Get current network stats
            if self.interface:
                current = psutil.net_io_counters(pernic=True).get(self.interface)
                if not current:
                    current = psutil.net_io_counters()
            else:
                current = psutil.net_io_counters()
            
            now = time.time()
            time_delta = now - self._last_update
            
            if time_delta > 0 and self._last_stats:
                # Calculate deltas
                bytes_sent_delta = current.bytes_sent - self._last_stats.bytes_sent
                bytes_recv_delta = current.bytes_recv - self._last_stats.bytes_recv
                packets_sent_delta = current.packets_sent - self._last_stats.packets_sent
                packets_recv_delta = current.packets_recv - self._last_stats.packets_recv
                
                # Calculate rates
                self.rates['bandwidth_tx_bps'] = (bytes_sent_delta * 8) / time_delta  # bits per second
                self.rates['bandwidth_rx_bps'] = (bytes_recv_delta * 8) / time_delta
                self.rates['bandwidth_total_bps'] = self.rates['bandwidth_tx_bps'] + self.rates['bandwidth_rx_bps']
                
                self.rates['packets_tx_ps'] = packets_sent_delta / time_delta
                self.rates['packets_rx_ps'] = packets_recv_delta / time_delta
                self.rates['packets_total_ps'] = self.rates['packets_tx_ps'] + self.rates['packets_rx_ps']
                
                # Update current totals
                self.current_stats = {
                    'bytes_sent': current.bytes_sent,
                    'bytes_recv': current.bytes_recv,
                    'packets_sent': current.packets_sent,
                    'packets_recv': current.packets_recv,
                    'errors_in': current.errin,
                    'errors_out': current.errout,
                    'drops_in': current.dropin,
                    'drops_out': current.dropout,
                }
                
                # Add to history
                self.history['timestamp'].append(datetime.now())
                self.history['bandwidth_tx'].append(self.rates['bandwidth_tx_bps'] / 1_000_000)  # Mbps
                self.history['bandwidth_rx'].append(self.rates['bandwidth_rx_bps'] / 1_000_000)  # Mbps
                self.history['packets_tx'].append(self.rates['packets_tx_ps'])
                self.history['packets_rx'].append(self.rates['packets_rx_ps'])
                self.history['errors'].append(current.errin + current.errout)
            
            self._last_stats = current
            self._last_update = now
            
        except Exception as e:
            logger.error(f"Error updating network stats: {e}")
    
    def record_dlms_request(self, bytes_sent: int):
        """Record a DLMS request sent"""
        self.app_stats['dlms_requests_sent'] += 1
        self.app_stats['dlms_total_bytes_sent'] += bytes_sent
        
        # Update average payload size
        if self.app_stats['dlms_requests_sent'] > 0:
            self.app_stats['dlms_avg_payload_size'] = (
                self.app_stats['dlms_total_bytes_sent'] / 
                self.app_stats['dlms_requests_sent']
            )
    
    def record_dlms_response(self, bytes_recv: int):
        """Record a DLMS response received"""
        self.app_stats['dlms_responses_recv'] += 1
        self.app_stats['dlms_total_bytes_recv'] += bytes_recv
    
    def record_mqtt_message(self, bytes_sent: int):
        """Record an MQTT message sent"""
        self.app_stats['mqtt_messages_sent'] += 1
        self.app_stats['mqtt_total_bytes_sent'] += bytes_sent
    
    def get_current_stats(self) -> Dict:
        """Get current network statistics"""
        # Force update if not monitoring in background
        if not self._monitoring:
            self._update_stats()
        
        return {
            # Raw totals
            'totals': dict(self.current_stats),
            
            # Rates
            'rates': {
                'bandwidth_tx_mbps': self.rates['bandwidth_tx_bps'] / 1_000_000,
                'bandwidth_rx_mbps': self.rates['bandwidth_rx_bps'] / 1_000_000,
                'bandwidth_total_mbps': self.rates['bandwidth_total_bps'] / 1_000_000,
                'bandwidth_tx_kbps': self.rates['bandwidth_tx_bps'] / 1_000,
                'bandwidth_rx_kbps': self.rates['bandwidth_rx_bps'] / 1_000,
                'bandwidth_total_kbps': self.rates['bandwidth_total_bps'] / 1_000,
                'packets_tx_ps': self.rates['packets_tx_ps'],
                'packets_rx_ps': self.rates['packets_rx_ps'],
                'packets_total_ps': self.rates['packets_total_ps'],
            },
            
            # Application stats
            'application': dict(self.app_stats),
            
            # Timestamp
            'timestamp': datetime.now().isoformat(),
        }
    
    def get_history(self) -> Dict:
        """Get historical data for graphing"""
        return {
            'timestamp': [t.isoformat() for t in self.history['timestamp']],
            'bandwidth_tx_mbps': list(self.history['bandwidth_tx']),
            'bandwidth_rx_mbps': list(self.history['bandwidth_rx']),
            'packets_tx_ps': list(self.history['packets_tx']),
            'packets_rx_ps': list(self.history['packets_rx']),
            'errors': list(self.history['errors']),
        }
    
    def get_summary(self) -> Dict:
        """Get summary statistics"""
        stats = self.get_current_stats()
        
        # Calculate averages from history
        avg_bandwidth_tx = sum(self.history['bandwidth_tx']) / len(self.history['bandwidth_tx']) if self.history['bandwidth_tx'] else 0
        avg_bandwidth_rx = sum(self.history['bandwidth_rx']) / len(self.history['bandwidth_rx']) if self.history['bandwidth_rx'] else 0
        max_bandwidth_tx = max(self.history['bandwidth_tx']) if self.history['bandwidth_tx'] else 0
        max_bandwidth_rx = max(self.history['bandwidth_rx']) if self.history['bandwidth_rx'] else 0
        
        return {
            'current': stats['rates'],
            'application': stats['application'],
            'averages': {
                'bandwidth_tx_mbps': avg_bandwidth_tx,
                'bandwidth_rx_mbps': avg_bandwidth_rx,
                'bandwidth_total_mbps': avg_bandwidth_tx + avg_bandwidth_rx,
            },
            'peaks': {
                'bandwidth_tx_mbps': max_bandwidth_tx,
                'bandwidth_rx_mbps': max_bandwidth_rx,
            },
            'totals': stats['totals'],
        }
    
    def reset_app_stats(self):
        """Reset application-level statistics"""
        self.app_stats = {
            'dlms_requests_sent': 0,
            'dlms_responses_recv': 0,
            'dlms_avg_payload_size': 0,
            'dlms_total_bytes_sent': 0,
            'dlms_total_bytes_recv': 0,
            'mqtt_messages_sent': 0,
            'mqtt_total_bytes_sent': 0,
        }
        logger.info("✓ Application statistics reset")


# Global instance
_network_monitor: Optional[NetworkMonitor] = None


def get_network_monitor() -> NetworkMonitor:
    """Get global network monitor instance"""
    global _network_monitor
    if _network_monitor is None:
        _network_monitor = NetworkMonitor()
        _network_monitor.start_monitoring(interval=1.0)
    return _network_monitor


if __name__ == "__main__":
    # Test network monitor
    logging.basicConfig(level=logging.INFO)
    
    monitor = NetworkMonitor()
    monitor.start_monitoring(interval=1.0)
    
    print("Network Monitor Test")
    print("=" * 60)
    
    try:
        for i in range(10):
            time.sleep(1)
            stats = monitor.get_current_stats()
            
            print(f"\n[Sample {i+1}]")
            print(f"  TX: {stats['rates']['bandwidth_tx_kbps']:.2f} kbps ({stats['rates']['packets_tx_ps']:.1f} pkt/s)")
            print(f"  RX: {stats['rates']['bandwidth_rx_kbps']:.2f} kbps ({stats['rates']['packets_rx_ps']:.1f} pkt/s)")
            print(f"  Total: {stats['rates']['bandwidth_total_kbps']:.2f} kbps")
    
    except KeyboardInterrupt:
        print("\n\nStopping...")
    
    finally:
        monitor.stop_monitoring()
        
        print("\n" + "=" * 60)
        print("Summary:")
        summary = monitor.get_summary()
        print(f"  Avg TX: {summary['averages']['bandwidth_tx_mbps']:.3f} Mbps")
        print(f"  Avg RX: {summary['averages']['bandwidth_rx_mbps']:.3f} Mbps")
        print(f"  Peak TX: {summary['peaks']['bandwidth_tx_mbps']:.3f} Mbps")
        print(f"  Peak RX: {summary['peaks']['bandwidth_rx_mbps']:.3f} Mbps")
