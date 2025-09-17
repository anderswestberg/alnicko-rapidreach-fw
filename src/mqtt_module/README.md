## MQTT Module (Zephyr) – Design and Usage Guide

This document describes the MQTT module located in `src/mqtt_module/`. It covers architecture, threading, connection management, subscriptions/publishing, and how very large messages are handled safely on constrained devices.

### Contents
- Architecture overview
- Initialization and connection lifecycle
- Keepalive and heartbeat
- Subscriptions and message delivery
- Publishing
- Handling very large messages (JSON + binary)
- Reconnection and socket cleanup
- Threading model and performance
- Configuration (Kconfig)
- Best practices and gotchas
- Troubleshooting

---

### Architecture overview
- **Files**:
  - `mqtt_module.c`: Core client, connection lifecycle, maintenance thread, subscriptions, large-message RX path, heartbeat scheduling.
  - `mqtt_message_parser.c`: Extracts JSON header from mixed JSON+binary payload and parses metadata.
  - `mqtt_audio_handler.c`: Example subscriber for audio alerts; demonstrates file-based playback and volume handling.
  - `file_manager.*`: LittleFS wrapper used by the module to store large payloads.

- **Key components**:
  - A single Zephyr `struct mqtt_client` with RX/TX buffers sized for control traffic (not full audio).
  - An MQTT maintenance thread that runs `mqtt_input()` and `mqtt_live()` to process packets and keep the session alive.
  - A work queue (`audio_work_queue`) to process JSON metadata and invoke handlers outside of the blocking RX path.

---

### Initialization and connection lifecycle
1. Call `mqtt_init()` once to prepare the client, initialize queues, and start the maintenance thread.
2. Call `mqtt_module_connect()` to initiate the connection; it waits for `CONNACK` (up to 5s).
3. Use `mqtt_enable_auto_reconnect()` to let the maintenance thread handle reconnection with exponential backoff.
4. Use `mqtt_module_disconnect()` for a clean shutdown (stops heartbeat; joins maintenance thread).

On every connection attempt, client ID is generated from the device ID. The broker host/port and keepalive are configurable via Kconfig.

---

### Keepalive and heartbeat
- `client.keepalive` is set by `CONFIG_RPR_MQTT_KEEPALIVE_SEC` (default 60s). The maintenance thread periodically calls `mqtt_live()` to drive PINGREQ/PINGRESP.
- A separate heartbeat work item publishes JSON heartbeats every `CONFIG_RPR_MQTT_HEARTBEAT_INTERVAL_SEC` seconds to `CONFIG_RPR_MQTT_HEARTBEAT_TOPIC/<clientId>`.

---

### Subscriptions and message delivery
- Call `mqtt_module_subscribe(topic, qos, handler)` to register a topic and a `handler(const char*, const uint8_t*, size_t)`.
- Incoming messages are dispatched in two ways:
  - Small payloads: read entirely in the event handler and invoke the handler directly.
  - Large payloads: see the next section. The handler is eventually called with only the JSON header (metadata) once the binary has been stored to a file.

---

### Publishing
- Use `mqtt_module_publish(topic, payload, len)` (QoS 1). After publishing, the maintenance thread processes PUBACK in the background.

---

### Handling very large messages (JSON + binary)
Large audio messages arrive on topics prefixed with `rapidreach/audio/` and contain:
- A JSON header (metadata like `opus_data_size`, `priority`, `play_count`, `volume`, optional `filename`), followed immediately by
- Binary Opus data of length `opus_data_size`.

Constraints and strategy:
- RAM is insufficient to buffer full audio; therefore, the payload is streamed directly from the socket to LittleFS.
- Zephyr’s `mqtt_read_publish_payload_blocking()` ties up the MQTT socket while reading a PUBLISH; concurrent `mqtt_input()`/`mqtt_live()` calls would return `-EBUSY`. The module therefore does not call them during payload reads.

Flow for large messages:
1. On `MQTT_EVT_PUBLISH`, send PUBACK immediately for QoS 1 to prevent broker timeouts while processing.
2. Read up to 512 bytes into a temporary buffer and detect the JSON boundary using brace counting that respects strings and escapes.
3. Determine where the binary begins; stream the remaining payload in very small chunks (64 bytes) to a temporary file in `/lfs/`:
   - Yield after every chunk (`k_yield()`) to keep the system responsive.
   - Do not call `mqtt_input()`/`mqtt_live()` while the blocking read is in progress.
4. Use alternating temp files (`/lfs/mqtt_audio_0.opus`, `/lfs/mqtt_audio_1.opus`) to avoid truncating a file that may still be in use by the audio player.
5. After the file is complete, briefly sleep/yield, then queue a work item that:
   - Passes only the JSON header to the registered subscription handler.
   - The handler (e.g., audio) infers the temp file path and starts non-blocking playback from the file. Volume mapping for codecs like TAS6422 is applied.

Parser behavior (`mqtt_message_parser.c`):
- Extracts JSON header length safely (brace counting with string/escape handling).
- Parses metadata with Zephyr’s JSON library; falls back to manual parsing for certain fields if needed.
- If `payload_after_json < opus_data_size` and equals 0, it assumes the binary was offloaded to the filesystem and returns success with `opus_data_len = 0`.

Important gotchas:
- Do not call `mqtt_input()` or `mqtt_live()` while `mqtt_read_publish_payload_blocking()` is active; the socket is busy and will return `-EBUSY`.
- Keep chunk size small and yield frequently during file writes so other threads (audio, system) can run.
- PUBACK must be sent before processing the payload to avoid broker retransmits/timeouts during long reads.

---

### Reconnection and socket cleanup
- The maintenance thread monitors `mqtt_input()`/`mqtt_live()` errors. On disconnection (`-ENOTCONN`, `-ECONNRESET`, `-EPIPE`, `-EBUSY`), it:
  - Marks `mqtt_connected = false` and sets `client.transport.tcp.sock = -1` to force cleanup on the next attempt.
  - Notifies the optional event handler/state machine of `DISCONNECTED`.
- Before reconnecting, the module performs a defensive cleanup:
  - Calls `mqtt_disconnect()` if a socket is present.
  - Re-prepares the client (`prepare_mqtt_client()`), then calls `mqtt_connect()`.
- Auto-reconnect uses exponential backoff between 5 seconds and 5 minutes.

---

### Threading model and performance
- MQTT maintenance thread (`K_PRIO_COOP(7)`): runs `mqtt_input()` and `mqtt_live()`; sleeps ~50 ms between loops.
- Audio work queue thread (`K_PRIO_PREEMPT(10)`): processes JSON metadata and calls handlers off the MQTT thread.
- During large payload reads, the event handler reads/writes in 64-byte chunks and yields after each write. This minimizes the time any single runnable starves others.

---

### Configuration (Kconfig)
Relevant options in `src/mqtt_module/Kconfig.mqtt`:
- `CONFIG_RPR_MODULE_MQTT`: enable module
- `CONFIG_RPR_MODULE_MQTT_LOG_LEVEL`: 0..4 (OFF..DEBUG)
- `CONFIG_RPR_MQTT_BROKER_HOST`, `CONFIG_RPR_MQTT_BROKER_PORT`
- `CONFIG_RPR_MQTT_CLIENT_ID`: base client ID
- `CONFIG_RPR_MQTT_HEARTBEAT_TOPIC`, `CONFIG_RPR_MQTT_HEARTBEAT_INTERVAL_SEC`
- `CONFIG_RPR_MQTT_KEEPALIVE_SEC`: keepalive (default 60s)

Other modules:
- `CONFIG_RPR_MODULE_FILE_MANAGER`: required for file-based offload of large payloads.

---

### Best practices and gotchas
- Always send PUBACK immediately on QoS 1 PUBLISH before long processing.
- Never call `mqtt_input()`/`mqtt_live()` while `mqtt_read_publish_payload_blocking()` is in progress.
- Use alternating temp files to avoid truncating an in-use file.
- Keep chunk size small (e.g., 64 bytes) and yield frequently.
- Do not block the MQTT maintenance thread with long operations; do heavy work in a separate work queue.
- Ensure the file system is mounted (the module calls `file_manager_init()` before use).
- If using a state machine, avoid double-connecting; let the state machine own the connect flow.

---

### Troubleshooting
- Disconnections right after processing a large message:
  - Verify PUBACK is sent before reading the payload.
  - Ensure no `mqtt_input()`/`mqtt_live()` calls happen during the blocking read (they will fail with `-EBUSY`).
  - Confirm chunk size/yield frequency; large single writes may starve other threads.

- Reconnect attempts fail with `-2` (EAGAIN):
  - Ensure the previous socket is explicitly disconnected and the client is re-prepared before `mqtt_connect()`.
  - Check broker-side timeouts.

- JSON parsing errors for valid headers:
  - Make sure the JSON boundary detection (brace counting) is used, not a simple search for `}`.
  - Confirm the first 512 bytes include the full JSON header; increase if headers may exceed this size.

- Audio file conflicts or playback errors:
  - Ensure alternating files are used; avoid truncating the file currently in use by the audio player.

---

### Extending the module
- To add a new large-binary topic:
  - Subscribe to the topic.
  - Reuse the JSON boundary detection and file-offload pattern.
  - Move any heavy processing out of the MQTT event handler into a work queue.
  - Consider per-topic temp files to avoid collisions.


