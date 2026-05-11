# C++ Windows Roadmap

## Product Definition

- Windows-only shared keyboard and mouse tool
- Focus on low-latency local network input sharing
- Not a remote desktop project

## Architecture

- `apps/`
  Sender and receiver executable entry points
- `src/core`
  Configuration, logging, shared runtime utilities
- `src/protocol`
  Binary protocol definitions and serializers
- `src/service`
  High-level sender and receiver application flows
- `include/shared_km`
  Public headers for internal modules

## Delivery Plan

1. Build skeleton and compile pipeline
2. TCP transport and protocol framing
3. Receiver-side `SendInput`
4. Sender-side low-level hook capture
5. Edge switching and reconnect handling
6. Tray app, persistent config, pairing UX

## Protocol Step 1

- Fixed-size header with `magic`, `version`, `kind`, `payload_size`
- Initial message kinds:
  - `Hello`
  - `Auth`
  - `AuthResult`
  - `Heartbeat`
- `InputEvent` kept reserved for the next step
- String payloads use `uint32 length + bytes`

## Network Step 1

- Blocking TCP server/client for bring-up
- Receiver flow:
  - listen
  - accept
  - receive `Hello`
  - receive `Auth`
  - reply `AuthResult`
  - receive `Heartbeat`
- Sender flow:
  - connect
  - send `Hello`
  - send `Auth`
  - receive `AuthResult`
  - send `Heartbeat`

## Input Event Step 1

- `InputEvent` payload introduced
- First concrete event type: `MouseMove`
- Current validation goal:
  - sender serializes and sends a mouse move
  - receiver parses it and logs the coordinates
