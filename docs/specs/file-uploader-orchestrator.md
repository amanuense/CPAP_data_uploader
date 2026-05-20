# File Uploader Orchestrator

## Overview
The File Uploader (`FileUploader.cpp/.h`) is the central orchestrator that coordinates all upload operations across multiple backends (SMB, Cloud, WebDAV). It manages upload state, performs pre-flight scans, handles the complete upload lifecycle, and selects which backend to run each session via timestamp-based cycling.

## Core Architecture

### Single-Backend Session Strategy
Each upload session runs **exactly one backend** (SMB or Cloud), selected by cycling:
1. **Backend selection** — at `begin()`, read `.backend_summary.smb` / `.backend_summary.cloud` and pick the backend with the **oldest `sessionStartTs`** (never-run backends have ts=0, so they go first)
2. **Session start** — write a placeholder summary entry with the current timestamp (advances the cycling pointer even if the session crashes)
3. **Upload pass** — run only the selected backend
4. **Session end** — overwrite the summary with full stats (done/total/empty). If session completes with no incomplete folders, any configured webhook notifications (SleepLab and/or Generic) fire immediately before returning COMPLETE.
5. **Soft reboot** — FSM always reboots after releasing the SD card, restoring heap

### Pre-flight Scans
Before writing the session-start timestamp (and before any network activity), performs SD-only scans across **all configured backends**. Uses a **dedicated `preflightFolderHasWork()`** instead of `scanDatalogFolders()` to avoid several critical false-positive conditions that cause endless reboot loops:

```
preflightFolderHasWork() rules (evaluated per folder):
  1. not completed AND not pending AND (recent OR canUploadOldData()) → WORK
  2. completed AND recent → scan files; WORK only if ≥1 file changed size
  3. Everything else (old completed, pending, or old incomplete outside window) → skip
```

**Critical design constraints** — three root causes of endless loops, each addressed:

| Root cause | Fix |
|---|---|
| `scanDatalogFolders()` always includes recently-completed folders, making `hasWork=true` on every boot | Pre-flight uses dedicated `preflightFolderHasWork()` that only returns true for genuinely changed recent files |
| Mandatory files (STR.edf, Identification.*, SETTINGS) grow every time the CPAP writes to the SD card; including them in the pre-flight check triggers a new upload session on every boot | Mandatory/settings files are **never** counted as pre-flight work — they are uploaded as a bonus during DATALOG-triggered sessions only |
| Old incomplete folders (e.g. uploaded to Cloud but not SMB) are detected as `smb_work=1` by pre-flight, but Phase 2 of the session is gated by `canUploadOldData()` (upload window). Outside the window, pre-flight detects work that the session cannot perform → reboot → repeat | Old (`!recent`) incomplete folders in pre-flight are gated by the same `canUploadOldData()` call used in Phase 2 |

```cpp
if (!smbWork && !cloudWork) {
    return UploadResult::NOTHING_TO_DO;  // → FSM enters COOLDOWN, no reboot
}
```
- Returns `UploadResult::NOTHING_TO_DO` when every backend is fully synced (or only out-of-window old work remains)
- The session-start summary is NOT written in this case — cycling pointer does not advance
- FSM responds by entering `COOLDOWN` directly without an `esp_restart()`, preventing endless reboot cycles

## Key Features

### Intelligent Folder Scanning
- **Recent completed folders**: Always rescanned — CPAP may extend/add files. Per-file size tracking (`hasFileChanged`) skips unchanged files
- **Old completed folders**: Skipped entirely
- **Pending folders**: Tracked for when they acquire content
- **Fresh vs Old data**: Different scheduling rules

### Backend Cycling
- `selectActiveBackend(sd)` compares `sessionStartTs` from `.backend_summary.smb` and `.backend_summary.cloud`
- Backend with **oldest timestamp** is selected; ties go to SMB
- Missing summary file → treated as ts=0 (oldest possible) so never-run backends are prioritized
- Written at session START so the pointer advances even on crashes
- **Redirect balancing**: when pre-flight detects that the selected backend has no work but the other backend does, it redirects to the working backend AND also writes a session-start summary for the original backend. Without this, the original backend's timestamp would stay frozen, causing it to be selected as "oldest" every boot → perpetual redirect → endless loop

### Uploaders
- **SMBUploader**: Network share uploads with transport resilience
- **SleepHQUploader**: Cloud uploads with OAuth and import sessions
- **WebDAVUploader**: Placeholder for future implementation

### Upload State Management
- **UploadStateManager**: Tracks file/folder completion status
- **Snapshot + Journal**: Efficient state persistence
- **Size-only tracking**: Optimized for recent DATALOG files
- **Checksum tracking**: For mandatory/SETTINGS files

### Memory Optimization
- **Single backend per session**: No concurrent SMB+TLS heap pressure
- **Soft reboot between sessions**: Restores full contiguous heap via `esp_restart()` (fast-boot path skips delays)
- **Buffer management**: Dynamic SMB buffer sizing based on heap
- **TLS reuse**: Persistent connections for cloud operations

### Mark-Complete Strategy
- **Recent folders**: Always marked complete (per-file size entries track changed/new files for next rescan)
- **Old folders**: Only marked complete when ALL files uploaded — failed old folders are retried whole next session

## Upload Process Flow

### 1. Initialization
```cpp
bool begin(fs::FS &sd) {
    // Create state managers for each backend
    smbStateManager = new UploadStateManager(...);
    cloudStateManager = new UploadStateManager(...);
    
    // Create uploaders
    smbUploader = new SMBUploader(...);
    sleephqUploader = new SleepHQUploader(...);
}
```

### 2. Upload Execution
```cpp
UploadResult uploadWithExclusiveAccess(fs::FS &sd, DataFilter filter) {
    // Pre-flight: check ALL backends (SD-only, no network)
    if (!anyBackendHasWork) return UploadResult::NOTHING_TO_DO;
    
    // Write session-start summary (advances cycling pointer)
    writeBackendSummaryStart(sd, activeBackend, sessionTs);
    
    // Run ONLY the active backend (SMB or CLOUD, not both)
    if (activeBackend == SMB) {
        uploadMandatoryFilesSmb(...);
        for (folder : folders) uploadDatalogFolderSmb(...);
    } else if (activeBackend == CLOUD) {
        sleephqUploader->begin();  // OAuth + team + import
        for (folder : folders) uploadDatalogFolderCloud(...);
        finalizeCloudImport();
    }
    
    // Write full summary (done/total/empty)
    writeBackendSummaryFull(...);
    return UploadResult::COMPLETE;
}
```

### 3. File Upload Logic
- **Mandatory files**: Identification.*, STR.edf, SETTINGS/
- **DATALOG folders**: Date-named folders with therapy data
- **Change detection**: Size comparison for recent files, checksums for critical files
- **Progress tracking**: Real-time status updates via WebStatus

## Advanced Features

### Transport Resilience (SMB)
- **Recoverable errors**: EAGAIN, EWOULDBLOCK, "Wrong signature"
- **WiFi cycling**: Reclaim poisoned sockets
- **Retry logic**: Up to 3 connect attempts with backoff
- **Connection reuse**: Per-folder to avoid socket exhaustion

### Cloud Import Management
- **Lazy import creation**: Only when files exist
- **Session reuse**: OAuth + team + import in one TLS session
- **Streaming uploads**: Stack-allocated multipart buffers
- **Low-memory handling**: Graceful degradation when max_alloc < 40KB

### Empty Folder Handling
- **7-day waiting**: Before marking empty folders complete
- **Pending tracking**: Monitors folders that acquire content
- **Automatic promotion**: From pending to normal processing

## Configuration Integration
- **Schedule Manager**: Enforces upload windows for old data
- **Traffic Monitor**: Detects SD bus activity for smart mode
- **Web Server**: Real-time progress monitoring
- **WiFi Manager**: Network connectivity for cloud operations

## Error Handling
- **Graceful degradation**: Skip failed backends, continue with others
- **State preservation**: Always save progress even on failures
- **Retry mechanisms**: Built into each backend
- **Timeout protection**: Per-file and per-session timeouts

### Progress Metric — Excluding Empty Folders
- `foldersTotal` = `done + incomplete` only — pending (empty) folders are excluded from the total
- Empty folders are reported separately as `folders_pending` and shown as a note in the GUI `(N empty)` 
- Progress bar = `done / (done + incomplete)` so empty folders never prevent reaching 100%

## Performance Optimizations
- **Pre-flight gating**: No network if nothing to upload; no session-start written either
- **Bulk operations**: Directory creation, batch uploads
- **Memory awareness**: Buffer sizing based on available heap
- **Connection reuse**: Persistent sessions where possible

## Integration Points
- **UploadFSM**: Main state machine calls uploadWithExclusiveAccess
- **SDCardManager**: Provides SD access control
- **Config**: Supplies all backend and timing parameters
- **WebStatus**: Real-time progress reporting
- **All Backend Uploaders**: Orchestrates their operations
