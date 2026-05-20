# Feature: Dual Webhook Notification System (SleepLab & Generic)

## Status

Issue description — implementation not yet started.

## 1. Overview

Implement a dual-target webhook notification system triggered upon the successful completion of a local (SMB) sync session. This allows the ESP32 to autonomously notify external systems that new EDF data is ready for processing without relying on directory polling.

## 2. User Stories

- **The Turn-Key User:** As a SleepLab user, I want to connect my ESP32 directly to my SleepLab instance by entering a Domain and User ID in my `config.txt`, so that my data is automatically imported without needing to understand generic webhook formatting.
- **The Power User:** As a self-hosting enthusiast, I want to configure a generic HTTP webhook URL that fires after a successful sync, allowing me to trigger distinct infrastructure automations (e.g., Home Assistant, Healthchecks.io).
- **The Administrator:** As a system administrator, I want these notifications to execute safely and sequentially so that a misconfiguration or timeout in one endpoint does not crash the device or block the other notification from sending.

## 3. Scope & Acceptance Criteria

1. **Configuration:** The system must parse three new variables from `config.txt`: `SLEEPLAB_DOMAIN`, `SLEEPLAB_USER_ID`, and `GENERIC_WEBHOOK_URL`.
2. **Execution Timing:** Webhooks must only fire during `FileUploader::runFullSession()` when the session state transitions to `COMPLETE` with no failed folder transfers.
3. **Memory Safety:** HTTP clients must be block-scoped to ensure network sockets and TLS arenas are instantly freed after execution, preventing heap exhaustion.
4. **Resilience:** Each webhook must have a strict 5000ms timeout. A failure (timeout or `5xx` error) must log to the internal circular buffer but must *not* change the overall session status from `COMPLETE` to `FAILED`.
5. **Sequential Operation:** If both SleepLab and Generic webhooks are defined, SleepLab executes first, followed immediately by the Generic webhook, with a `vTaskDelay` yield between them to feed the FreeRTOS watchdog.

## 4. Implementation Plan

### Phase 1: Configuration Engine

- **`include/Config.h`**: Add private strings `sleepLabDomain`, `sleepLabUserId`, `genericWebhookUrl` and their respective public getter methods.
- **`src/Config.cpp`**: Update `loadConfig()` to parse these variables using `readStringFromConfig()`. Add basic validation to strip trailing slashes from the `sleepLabDomain`.

### Phase 2: Orchestration Injection

- **`src/FileUploader.cpp`**: Include `<HTTPClient.h>`.
- Locate the success block at the end of `runFullSession()` (where `LOG("[FileUploader] All folders complete — session done")` occurs).
- Inject **Module A (SleepLab)**:
  - Construct endpoint: `[DOMAIN]/api/import/webhook/[USER_ID]`
  - Wrap `HTTPClient` in a localized scope `{ ... }`. Execute a POST request with a 5000ms timeout.
- Inject **Module B (Generic)**:
  - Check if `genericWebhookUrl` > 0.
  - Wrap `HTTPClient` in a localized scope `{ ... }`. Execute a POST request with a 5000ms timeout.

### Phase 3: Web UI (Optional for V1, Recommended for V2)

- **`web_ui.h`**: Add input fields for the new configuration variables.
- **`src/CpapWebServer.cpp`**: Ensure the new fields are serialized/deserialized when saving config via the web interface.

## 5. Testing Plan

### 5.1 Unit Tests

- **`test/test_config/test_config.cpp`**:
  - Add a test case validating that `SLEEPLAB_DOMAIN`, `SLEEPLAB_USER_ID`, and `GENERIC_WEBHOOK_URL` are correctly parsed from a mock `config.txt`.
  - Add a test case ensuring missing variables default to empty strings without causing null pointer exceptions.

### 5.2 Integration / End-to-End Tests

- **Test Case 1 (Empty State):** Flash firmware with empty webhook fields. Verify standard SMB upload succeeds and orchestrator returns `COMPLETE` without attempting network calls.
- **Test Case 2 (SleepLab Only):** Spin up a local mock server (`python3 -m http.server 8000`). Configure `SLEEPLAB_DOMAIN=http://192.168.x.x:8000` and `SLEEPLAB_USER_ID=user_123`. Verify the ESP32 successfully POSTs to `/api/import/webhook/user_123` and logs a 200/501 HTTP code.
- **Test Case 3 (Timeout Handling):** Configure the generic webhook to point to a blackholed IP (e.g., `http://10.255.255.255`). Verify the HTTP client aborts exactly at 5000ms, logs the timeout, and proceeds to finish the FSM normally without triggering a hardware restart.
- **Test Case 4 (Sequential Flow):** Configure both SleepLab and Generic webhooks to point to the mock server. Verify two distinct POST requests are received sequentially.

## 6. Documentation Requirements

To maintain consistency with the repository's documentation standards:

- **`docs/config.txt.example.both`** & **`docs/config.txt.example.smb`**: Append the new variables under a `### WEBHOOKS ###` header with commented explanations.
- **`release/README.md`**: Add a section detailing how to configure the SleepLab integration and generic webhooks, noting that HTTPS endpoints may require specific configuration depending on the ESP32's root certificate store.
- **`docs/specs/file-uploader-orchestrator.md`**: Update the state machine documentation to note that webhook resolution occurs immediately prior to the final state return.

## 7. Checklist (Updated as Work Progresses)

### Phase 1: Configuration Engine
- [ ] Add `sleepLabDomain`, `sleepLabUserId`, `genericWebhookUrl` to `include/Config.h` with getters
- [ ] Parse new fields in `src/Config.cpp` `loadConfig()`
- [ ] Add validation (strip trailing slashes from domain)
- [ ] Add config tests in `test/test_config/test_config.cpp`

### Phase 2: Orchestration Injection
- [ ] Include `<HTTPClient.h>` in `src/FileUploader.cpp`
- [ ] Locate success block in `runFullSession()`
- [ ] Implement SleepLab webhook module
- [ ] Implement Generic webhook module
- [ ] Ensure 5000ms timeout on both
- [ ] Ensure block-scoped HTTPClient for memory safety
- [ ] Ensure sequential execution with `vTaskDelay` yield

### Phase 3: Web UI
- [ ] Add config fields to `web_ui.h`
- [ ] Update serialization in `src/CpapWebServer.cpp`

### Documentation
- [ ] Update config examples in `docs/`
- [ ] Update `release/README.md`
- [ ] Update `docs/specs/file-uploader-orchestrator.md`

### Testing
- [ ] Unit tests for config parsing
- [ ] Integration: empty state
- [ ] Integration: SleepLab only
- [ ] Integration: timeout handling
- [ ] Integration: sequential flow
