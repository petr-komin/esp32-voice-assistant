# Device ↔ server protocol

Version 1. Transport: WebSocket, TLS-terminated (`wss://`) in production.
Text frames carry JSON control messages; binary frames carry audio.

Audio in both directions: raw PCM, signed 16-bit little-endian, mono, 16 kHz,
sent in frames of about 100 ms (3200 bytes). The last frame may be shorter.

## Sequence

```
device                                   server
  │ ── hello {device, token, fw} ─────────▶ │  verify token (constant time)
  │ ── start {format, rate} ──────────────▶ │
  │ ◀──────────────────────────────── ack ─ │  server is alive and accepting
  │ ══ audio frames (while speaking) ═════▶ │
  │ ── end ───────────────────────────────▶ │
  │ ◀─────────────────────────── thinking ─ │  STT + LLM running
  │ ◀──────────── audio_start {format,rate} │
  │ ◀═════════════════════ reply audio ════ │  device plays frames as they arrive
  │ ◀──────────────────────── done {action} │
  │        (next start … end, or close)     │
```

## Messages

| Direction | Message | Fields |
|---|---|---|
| device → server | `hello` | `device` (id), `token` (shared secret), `fw` (firmware version) |
| device → server | `start` | `format` = `"pcm16"`, `rate` = `16000` |
| device → server | `end` | – |
| server → device | `ack` | – |
| server → device | `thinking` | – |
| server → device | `audio_start` | `format`, `rate` |
| server → device | `done` | `action`: what the server did (e.g. heating schedule set) |
| server → device | `error` | `code`: `auth`, `format`, `protocol`, `too_long`, `stt_failed`, `llm_failed`, `tts_failed` |

After an `error` the server closes the connection (WebSocket status 1008).

## Why an application-level `ack`

A TCP acknowledgement only proves that the kernel received the bytes, not that
the service is running and accepted the request. `ack` gives the device an
end-to-end liveness signal before the user has finished speaking.

## Timeouts

| Where | Limit | Reaction |
|---|---|---|
| server: connect → `hello` | 5 s | close |
| server: any idle read | 120 s | close (matches nginx `proxy_read_timeout`) |
| server: utterance length | 30 s | `error too_long` |
| device: `start` → `ack` | 3 s | "no connection" message, red LED |
| device: `end` → `audio_start` | 15 s | "error" message, red LED |
| device: connection drops mid-request | – | "error" message, red LED |
