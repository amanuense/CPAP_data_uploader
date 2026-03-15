#ifndef UPLOAD_FSM_H
#define UPLOAD_FSM_H

// Upload FSM states — shared between main.cpp and WebServer
enum class UploadState {
    IDLE,
    LISTENING,
    ACQUIRING,
    UPLOADING,
    RELEASING,
    COOLDOWN,
    COMPLETE,
    MONITORING
};

inline const char* getStateName(UploadState state) {
    switch (state) {
        case UploadState::IDLE: return "IDLE";
        case UploadState::LISTENING: return "LISTENING";
        case UploadState::ACQUIRING: return "ACQUIRING";
        case UploadState::UPLOADING: return "UPLOADING";
        case UploadState::RELEASING: return "RELEASING";
        case UploadState::COOLDOWN: return "COOLDOWN";
        case UploadState::COMPLETE: return "COMPLETE";
        case UploadState::MONITORING: return "MONITORING";
        default: return "UNKNOWN";
    }
}

// ── Bus yield mechanism ──────────────────────────────────────────────────────
// GPIO33 (CS_SENSE) is tapped on the CPAP side of the MUX, upstream of the
// GPIO26 switch. A FALLING edge while ESP owns the bus means the CPAP is
// trying to access the SD card and being silently blocked.
//
// The ISR sets this flag; upload loops check it at chunk boundaries and
// return YIELD_NEEDED so the FSM can release the bus within microseconds.
extern volatile bool g_cpapYieldRequest;

void IRAM_ATTR cpapYieldISR();

#endif // UPLOAD_FSM_H
