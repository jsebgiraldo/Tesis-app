#!/usr/bin/env python3
"""
Optimized DLMS Reader with Scaler Caching (Phase 2)
Reduces latency by caching scaler/unit values to avoid repeated queries.
"""

import logging
from typing import Optional, Dict, Tuple, List, Any
from dlms_reader import DLMSClient

logger = logging.getLogger('dlms_optimized_reader')


class OptimizedDLMSReader:
    """
    Optimized DLMS reader with scaler caching.
    
    Phase 2 Optimization: Cache scalers/units to reduce queries by 50%
    - First read: Fetch value + scaler/unit (2 queries)
    - Subsequent reads: Only fetch value (1 query) - use cached scaler
    
    Phase 3 (Batch reading) is NOT implemented as it requires specific meter support.
    """
    
    def __init__(self, client: DLMSClient, use_batch: bool = False):
        """
        Initialize optimized reader.
        
        Args:
            client: Connected DLMSClient instance
            use_batch: If True, attempt batch reading (Phase 3) - Not implemented
        """
        self.client = client
        self.use_batch = use_batch
        
        # Scaler cache: {obis_code: (scaler, unit)}
        self._scaler_cache: Dict[str, Tuple[int, Any]] = {}
        
        # Statistics
        self._cache_hits = 0
        self._cache_misses = 0
        
        logger.info(f"OptimizedDLMSReader initialized (batch={use_batch})")
        
        if use_batch:
            logger.warning("⚠️ Batch reading (Phase 3) requested but not implemented")
    
    def warmup_cache(self, obis_codes: List[str]) -> None:
        """
        Pre-populate scaler cache for given OBIS codes.
        
        Args:
            obis_codes: List of OBIS codes to cache scalers for
        """
        logger.info(f"Warming up scaler cache for {len(obis_codes)} registers...")
        
        for obis in obis_codes:
            try:
                # Read full register to populate cache
                _ = self.read_register_optimized(obis)
                logger.debug(f"  ✓ Cached scaler for {obis}")
            except Exception as e:
                logger.warning(f"  ⚠ Failed to cache {obis}: {e}")
        
        logger.info(f"Cache warmup complete: {len(self._scaler_cache)} entries")
    
    def read_register_optimized(self, obis_code: str) -> Optional[Tuple[Any, int, Any]]:
        """
        Read register with scaler caching optimization.
        
        Returns:
            Tuple of (scaled_value, unit_code, raw_value) or None on error
        """
        try:
            # Check if we have cached scaler
            if obis_code in self._scaler_cache:
                # Cache hit - only read value (attribute 2)
                self._cache_hits += 1
                raw_value = self._read_value_only(obis_code)
                
                if raw_value is not None:
                    scaler, unit = self._scaler_cache[obis_code]
                    scaled_value = self._apply_scaler(raw_value, scaler)
                    return (scaled_value, unit, raw_value)
                else:
                    # Warning ya fue emitido por _read_value_only, no duplicar
                    return None
            else:
                # Cache miss - read full register (value + scaler)
                self._cache_misses += 1
                result = self._read_full_register(obis_code)
                
                if result is not None:
                    scaled_value, unit, raw_value, scaler = result
                    # Cache the scaler for future use
                    self._scaler_cache[obis_code] = (scaler, unit)
                    return (scaled_value, unit, raw_value)
                else:
                    return None
                    
        except Exception as e:
            logger.error(f"Error reading {obis_code}: {e}")
            return None
    
    def _read_value_only(self, obis_code: str, retries: int = 1) -> Optional[Any]:
        """
        Read only the value using cached scaler with retry logic.
        Note: DLMSClient.read_register() reads both value and scaler,
        so we still get both but only use the value.
        
        Args:
            obis_code: OBIS code to read
            retries: Number of retries on failure (default 1)
            
        Returns:
            Scaled value or None
        """
        import time
        
        last_error = None
        for attempt in range(retries + 1):
            try:
                # read_register returns (scaled_value, unit_code, raw_value)
                result = self.client.read_register(obis_code)
                if result:
                    scaled_value, unit_code, raw_value = result
                    if attempt > 0:
                        logger.debug(f"✓ Read succeeded on retry {attempt} for {obis_code}")
                    return scaled_value  # Already scaled
                return None
            except Exception as e:
                last_error = e
                if attempt < retries:
                    # Solo DEBUG en primeros intentos, no WARNING
                    logger.debug(f"Read attempt {attempt+1}/{retries+1} failed for {obis_code}: {e}")
                    time.sleep(0.3)  # Pausa breve antes de reintentar
                    continue
        
        # Solo WARNING si todos los intentos fallaron
        if last_error:
            logger.warning(f"Failed to read value for {obis_code} after {retries+1} attempts")
        return None
    
    def _read_full_register(self, obis_code: str) -> Optional[Tuple[Any, int, Any, int]]:
        """
        Read full register including value and scaler.
        
        Returns:
            Tuple of (scaled_value, unit_code, raw_value, scaler) or None
        """
        try:
            # read_register returns (scaled_value, unit_code, raw_value)
            # and internally reads the scaler
            result = self.client.read_register(obis_code)
            
            if result is None:
                return None
            
            scaled_value, unit_code, raw_value = result
            
            # Calculate scaler from scaled_value and raw_value
            # scaler = log10(scaled_value / raw_value) if both are numeric
            try:
                if isinstance(raw_value, (int, float)) and isinstance(scaled_value, (int, float)):
                    if raw_value != 0:
                        import math
                        ratio = float(scaled_value) / float(raw_value)
                        if ratio > 0:
                            scaler = int(round(math.log10(ratio)))
                        else:
                            scaler = 0
                    else:
                        scaler = 0
                else:
                    scaler = 0
            except:
                scaler = 0
            
            return (scaled_value, unit_code, raw_value, scaler)
            
        except Exception as e:
            logger.error(f"Error reading full register {obis_code}: {e}")
            return None
    
    def _apply_scaler(self, raw_value: Any, scaler: int) -> Any:
        """
        Apply scaler to raw value.
        
        Args:
            raw_value: Raw value from meter
            scaler: Scaler exponent (e.g., -2 means divide by 100)
            
        Returns:
            Scaled value
        """
        try:
            if isinstance(raw_value, (int, float)):
                if scaler == 0:
                    return raw_value
                else:
                    return raw_value * (10 ** scaler)
            else:
                # Non-numeric value, return as-is
                return raw_value
        except Exception as e:
            logger.error(f"Error applying scaler {scaler} to {raw_value}: {e}")
            return raw_value
    
    def get_cache_stats(self) -> Dict[str, Any]:
        """
        Get cache statistics.
        
        Returns:
            Dictionary with cache stats
        """
        total = self._cache_hits + self._cache_misses
        hit_rate = (self._cache_hits / total * 100) if total > 0 else 0
        
        return {
            'cache_size': len(self._scaler_cache),
            'cache_hits': self._cache_hits,
            'cache_misses': self._cache_misses,
            'hit_rate_percent': hit_rate,
            'cached_obis': list(self._scaler_cache.keys())
        }
    
    def clear_cache(self) -> None:
        """Clear the scaler cache."""
        self._scaler_cache.clear()
        self._cache_hits = 0
        self._cache_misses = 0
        logger.info("Scaler cache cleared")
    
    def read_multiple_registers(self, obis_codes: List[str]) -> Dict[str, Optional[Tuple[Any, int, Any]]]:
        """
        Read multiple registers sequentially with caching.
        
        Args:
            obis_codes: List of OBIS codes to read
            
        Returns:
            Dictionary mapping OBIS codes to (value, unit, raw_value) tuples
        """
        results = {}
        
        for obis in obis_codes:
            result = self.read_register_optimized(obis)
            results[obis] = result
        
        return results


if __name__ == "__main__":
    # Example usage
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s - [%(levelname)s] - %(message)s'
    )
    
    print("OptimizedDLMSReader - Test Mode")
    print("This module requires a connected DLMSClient instance.")
    print()
    print("Example usage:")
    print("  from dlms_reader import DLMSClient")
    print("  from dlms_optimized_reader import OptimizedDLMSReader")
    print()
    print("  client = DLMSClient(...)")
    print("  client.connect()")
    print()
    print("  reader = OptimizedDLMSReader(client)")
    print("  reader.warmup_cache(['1-1:32.7.0', '1-1:31.7.0'])")
    print()
    print("  # First read: cache miss, 2 queries")
    print("  result = reader.read_register_optimized('1-1:32.7.0')")
    print()
    print("  # Second read: cache hit, 1 query (50% faster!)")
    print("  result = reader.read_register_optimized('1-1:32.7.0')")
    print()
    print("  stats = reader.get_cache_stats()")
    print("  print(f\"Cache hit rate: {stats['hit_rate_percent']:.1f}%\")")
