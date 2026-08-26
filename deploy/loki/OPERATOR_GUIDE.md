# 0 A.D. Observability & Operator Guide

This guide describes how to operate the **0 A.D. + Grafana Loki + Grafana Alloy** observability stack in development, modding, and production dedicated server environments.

---

## 1. Mod Developer Troubleshooting Workflow

When creating or testing mods, SpiderMonkey JavaScript errors, template XML validation errors, and pathfinding warnings are written to `mainlog.html` and `interestinglog.html`.

### 1.1 Live JS Error Tailing

In Grafana (Explore view or Dashboard), run the following LogQL query to track script execution errors in real time:

```logql
{app="0ad", level="error"} |~ "(?i)javascript|error in|uncaught exception|gui/|simulation/"
```

### 1.2 Subsystem Filter

To isolate a specific subsystem while modding:
* **Audio / Sound:** `{app="0ad", subsystem="Sound"}`
* **Renderer / Shaders:** `{app="0ad", subsystem="Renderer"}`
* **Network & NetServer:** `{app="0ad", subsystem="Net"}`
* **Entity Components:** `{app="0ad"} |= "ComponentManager"`

---

## 2. Dedicated Server Setup (Linux & systemd)

For running headless 0 A.D. multiplayer servers on Linux:

### 2.1 Headless Server Execution

Run Pyrogenesis in headless server mode:
```bash
pyrogenesis -autostart="multiplayer" -autostart-nonvisual -mod=public
```

Logs will be written to `~/.local/state/0ad/log/` (or `/var/log/0ad-server/` when using `-writableRoot`).

### 2.2 Installing and Starting Grafana Alloy as a Service

1. Copy `deploy/loki/server.alloy` to `/etc/alloy/server.alloy`.
2. Copy `deploy/loki/systemd/0ad-alloy.service` to `/etc/systemd/system/0ad-alloy.service`.
3. Set your central Loki push URL in `/etc/systemd/system/0ad-alloy.service` or `/etc/default/alloy`:
   ```bash
   Environment="LOKI_URL=http://<central-loki-ip>:3100/loki/api/v1/push"
   ```
4. Enable and start the service:
   ```bash
   sudo systemctl daemon-reload
   sudo systemctl enable --now 0ad-alloy
   sudo systemctl status 0ad-alloy
   ```

---

## 3. Multiplayer Out-of-Sync (OOS) Triaging Workflow

When a multiplayer desync occurs, 0 A.D. writes state dumps into `oos_dump.txt` and `oos_logs/<timestamp>/`:

1. **Alert Notification**: Grafana's alerting rule `0AD Multiplayer Out-of-Sync (OOS) Detected` fires immediately upon encountering `"Out of sync"`.
2. **Find Turn & Mismatched Hashes**:
   ```logql
   {app=~"0ad.*"} |= "oos_dump" or |= "Out of sync"
   ```
3. **Inspect Client Dumps**:
   Query the OOS log stream across all connected client hosts for that match timestamp:
   ```logql
   {app=~"0ad.*", log_type="oos"}
   ```

---

## 4. Retention & Capacity Planning

| Component | Default | Configuration File |
| :--- | :--- | :--- |
| **Log Retention Period** | 14 Days (336h) | [`loki-config.yaml`](file:///C:/Users/james/0ad/deploy/loki/loki-config.yaml#L35) (`retention_period`) |
| **Compactor Cleanup Cycle**| Every 10 min | [`loki-config.yaml`](file:///C:/Users/james/0ad/deploy/loki/loki-config.yaml#L40) (`compaction_interval`) |
| **Ingestion Burst Limit** | 32 MB/s | [`loki-config.yaml`](file:///C:/Users/james/0ad/deploy/loki/loki-config.yaml#L33) (`ingestion_burst_size_mb`) |
| **Target Chunk Size** | 1.5 MiB | [`loki-config.yaml`](file:///C:/Users/james/0ad/deploy/loki/loki-config.yaml#L26) (`chunk_target_size`) |
