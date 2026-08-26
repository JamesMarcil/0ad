# 0 A.D. Observability Stack (Loki + Grafana + Alloy)

This directory contains the Docker Compose and Grafana Alloy configuration for ingesting and visualizing logs from 0 A.D. (Pyrogenesis engine).

## Quickstart

### 1. Start the Stack

```bash
cd deploy/loki
docker compose up -d
```

### 2. Verify Services

* **Grafana**: Open [http://localhost:3000](http://localhost:3000) (Default login: `admin` / `admin`).
* **Loki Readiness**: Open [http://localhost:3100/ready](http://localhost:3100/ready).
* **Alloy UI / Diagnostics**: Open [http://localhost:12345](http://localhost:12345).

### 3. Log Ingestion

When 0 A.D. runs, it writes logs to `%LOCALAPPDATA%\0ad\logs\` (Windows) or `~/.local/state/0ad/log/` (Linux).
The containerized Grafana Alloy discovers `mainlog.html`, `interestinglog.html`, `crashlog.txt`, and `oos_dump*.txt`, strips HTML formatting, normalizes log levels (`error`, `warn`, `info`), and pushes them into Loki.

### 4. Dashboards & Explore

* Navigate to **Dashboards** > **0 A.D. Engine Observability** to view live error/warning rates and streaming logs.
* Or open **Explore** in Grafana and query using LogQL:
  ```logql
  {app="0ad", level=~"error|warn"}
  ```
