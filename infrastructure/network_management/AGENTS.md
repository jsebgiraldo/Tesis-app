# Repository Guidelines

## Project Structure & Module Organization
- Root orchestration lives here; no app source code.
- `docker-compose.yml`: defines services (`postgres`, `redis`, `influxdb`, `dashboard`, `api`, `websocket`, `celery`, `celerybeat`, `nginx`).
- Scripts: `up.sh`, `down.sh`, `status.sh`, `logs.sh` for common lifecycle tasks.
- `.env`: local configuration defaults (ports, secrets, domains). See `README.md` for ports and notes.
- Data persists in Docker volumes prefixed `ow_...`.

## Build, Run, and Development Commands
- Start stack: `./up.sh` (uses project name `openwisp`).
- Check status: `./status.sh` (shows health and ports).
- Tail logs: `./logs.sh` (Ctrl-C to stop).
- Stop stack: `./down.sh`.
- Update images: `docker compose --project-name openwisp pull`.
- Recreate services: `docker compose --project-name openwisp up -d --force-recreate`.
- Clean volumes (destructive): `docker compose --project-name openwisp down -v`.

## Coding Style & Naming Conventions
- Shell: Bash with `set -euo pipefail`; quote variables and use `"$(...)"` for substitutions.
- Filenames: lowercase with hyphens/underscores; keep the `openwisp` project name consistent across scripts/commands.
- YAML: 2-space indentation; keys lowercase; environment variables in `SCREAMING_SNAKE_CASE`.

## Testing Guidelines
- Smoke test endpoints: `curl -I http://localhost:8091` and `curl -I http://localhost:8092` should return 200/302.
- Service health: `./status.sh` (look for healthy containers) and `./logs.sh` for startup/migration output.
- No unit tests in this directory; tests are operational checks against running containers.

## Commit & Pull Request Guidelines
- Commits: present tense, concise scope prefix, e.g. `compose: expose 8092 for API`, `scripts: add logs tail`.
- PRs must include: what/why, affected services, env/port changes, and verification steps (commands + expected results). Add screenshots or logs when relevant.

## Security & Configuration Tips
- For production, change `DJANGO_SECRET_KEY`, restrict `DJANGO_ALLOWED_HOSTS`, and avoid exposing `5433/6379/8086` publicly.
- Adjust port mappings in `docker-compose.yml` to avoid conflicts with other stacks (ThingsBoard, Leshan).
- TLS: `SSL_CERT_MODE=SelfSigned` is for local use; provide real certificates for public deployments.
