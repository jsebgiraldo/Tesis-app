"""
Local custom settings for OpenWISP Django services.
This file is imported at the end of openwisp/settings.py.
Includes Celery sane defaults and web/CSRF/proxy cookie settings for nginx HTTPS.
"""

# Celery workers defaults
CELERY_WORKER_LOG_FORMAT = "[%(asctime)s: %(levelname)s/%(processName)s] %(message)s"
CELERY_WORKER_TASK_LOG_FORMAT = (
    "[%(asctime)s: %(levelname)s/%(processName)s] Task %(task_name)s[%(task_id)s]: %(message)s"
)
CELERY_WORKER_POOL = "prefork"
CELERY_WORKER_PREFETCH_MULTIPLIER = 1
CELERY_WORKER_STATE_DB = "/opt/openwisp/celery.state"
CELERY_BEAT_SCHEDULE_FILENAME = "/opt/openwisp/celerybeat-schedule"
CELERY_BEAT_SCHEDULER = "celery.beat:PersistentScheduler"

# Web security/proxy cookie settings
CSRF_TRUSTED_ORIGINS = [
    "https://dashboard.localhost",
    "https://dashboard.localhost:18445",
    "https://api.localhost",
    "https://api.localhost:18445",
]
SECURE_PROXY_SSL_HEADER = ("HTTP_X_FORWARDED_PROTO", "https")
SESSION_COOKIE_SECURE = True
CSRF_COOKIE_SECURE = True
SESSION_COOKIE_SAMESITE = "Lax"
CSRF_COOKIE_SAMESITE = "Lax"
SESSION_COOKIE_DOMAIN = None
CSRF_COOKIE_DOMAIN = None
