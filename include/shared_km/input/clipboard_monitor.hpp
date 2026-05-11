#pragma once

#include <functional>
#include <string>

namespace shared_km::input {

class ClipboardMonitor final {
public:
    using Callback = std::function<void(const std::string& utf8_text)>;

    ClipboardMonitor() = default;
    ~ClipboardMonitor();

    ClipboardMonitor(const ClipboardMonitor&) = delete;
    ClipboardMonitor& operator=(const ClipboardMonitor&) = delete;

    bool Start(Callback callback);
    void Stop();
    bool IsRunning() const { return thread_id_ != 0; }

    std::string LastError() const { return last_error_; }

    // Called from the clipboard listener thread; public so the window proc can reach it
    void OnClipboardChange(const std::string& utf8_text);

private:

    unsigned long thread_id_ = 0;
    void* thread_handle_ = nullptr;
    void* hwnd_ = nullptr;
    std::string last_error_;
    Callback callback_;
};

} // namespace shared_km::input
