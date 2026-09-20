# S7 project status and next-step plan

Date: 2026-09-20

## Where we are now

We just finished a focused architecture update around node identity and headless discovery, and the changes were committed in:

- `ba9462e` — `Add node identity metadata to studio discovery`

### Completed work

- `s7studio`
  - Headless nodes discovery now consumes the newer node identity / certificate metadata.
  - Discovery merging is more stable by using logical identity, not only endpoint strings.
  - The UI now shows certificate identity details in the node list and overview.

- `s7runtime`
  - Control-plane responses now expose node certificate metadata.
  - `HELLO`, `GET_NODE_INFO`, `GET_PEERS`, and `PEER_ANNOUNCE` carry certificate-related identity fields.
  - `RuntimeService` derives identity from the configured certificate when present.

- Tests
  - Runtime control-plane smoke coverage was extended for the new identity fields.
  - `s7studio` smoke still passes after the discovery updates.

## Current design direction

We agreed on these priorities for `s7system`:

1. Modern, easy-to-use plugin system
2. Clean transport backend abstraction
3. External schema-backed element storage, preferably zipped
4. Node identity bound to logical identity and X.509 certificate fields
5. Backward compatibility later, not now
6. `s7studio` improvements later, not the current focus

## What to do next

### Next development phase

1. **Plugin system rollout**
   - Make plugin loading simpler and clearer to use.
   - Keep registration and metadata discovery explicit.
   - Prefer a model that is easy to extend without touching core code everywhere.

2. **Transport/backend abstraction**
   - Keep backend selection clean and understandable.
   - Separate websocket / in-memory / future transports behind a stable interface.
   - Avoid leaking backend-specific details into UI or runtime code.

3. **Schema-backed element storage**
   - Move element libraries to external files.
   - Prefer a zipped or archive-based format if it keeps things compact and easy to distribute.
   - Make sure runtime loading stays straightforward.

4. **Node identity / certificate integration**
   - Treat `node_uuid` as the logical identity field.
   - Keep certificate subject/common-name and fingerprint visible in control-plane data.
   - Ensure discovery and peer records remain stable across restarts.

## Things to remember

- We do **not** need compatibility work yet.
- We should **not** spend time on `s7studio` polish beyond the discovery/identity integration already done.
- The repo is currently in a good state after the commit above.

## Suggested next session start

When we come back, start by reviewing the current plugin loading path and the transport abstraction boundary, then implement the simplest version of the plugin rollout first.

