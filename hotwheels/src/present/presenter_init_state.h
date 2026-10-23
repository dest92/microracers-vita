#pragma once

namespace hw::present {

// Separates the lifetime of the shared GL context from a fully usable P8 presenter.
class PresenterInitState {
public:
    bool context_started() const { return context_started_; }
    bool ready() const { return ready_; }
    void mark_context_started() { context_started_ = true; }
    void mark_ready() { ready_ = true; }
    void mark_failed() { ready_ = false; }

private:
    bool context_started_ = false;
    bool ready_ = false;
};

}
