"""
Local overrides for Django settings used by Celery inside OpenWISP

These values are loaded by openwisp/settings.py via:
    from .configuration.custom_django_settings import *

They help avoid missing Celery defaults that cause AttributeError in Celery 5.x
when accessing app.conf.* during CLI option parsing/initialization.
"""

# Celery worker logging formats
CELERY_WORKER_LOG_FORMAT = "[%(asctime)s: %(levelname)s/%(processName)s] %(message)s"
CELERY_WORKER_TASK_LOG_FORMAT = (
    "[%(asctime)s: %(levelname)s/%(processName)s] Task %(task_name)s[%(task_id)s]: %(message)s"
)

# Explicitly set worker pool and prefetch to avoid missing defaults
CELERY_WORKER_POOL = "prefork"
CELERY_WORKER_PREFETCH_MULTIPLIER = 1

# Persistent state/schedule files
CELERY_WORKER_STATE_DB = "/opt/openwisp/celery.state"
CELERY_BEAT_SCHEDULE_FILENAME = "/opt/openwisp/celerybeat-schedule"
CELERY_BEAT_SCHEDULER = "celery.beat:PersistentScheduler"

