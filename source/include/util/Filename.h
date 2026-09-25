#pragma once
#include <cctype>
#include <string>
#include <string_view>

// Strips anything unsafe to use both as a filename and inside the double-quoted ffmpeg command
// line Recorder builds (rejects quotes in particular, which could otherwise break out of the
// quoted output path). Shared by App::StartRecording and Engine.cpp's data-save path so both
// agree on what's safe.
inline std::string SanitizeFilename(std::string_view raw, const char* fallback) {
    std::string safe;
    for (char c : raw)
        safe += (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == ' ') ? c : '_';
    if (safe.empty()) safe = fallback;
    return safe;
}
