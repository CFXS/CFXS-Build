#pragma once
#include <thread>
#include <functional>

class FunctionWorker {
public:
    FunctionWorker(int idx) : m_index(idx) {
        auto t = new std::thread([=]() {
            while (!m_terminate) {
                if (m_busy) {
                    m_e();
                    m_busy = false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            m_running = false;
        });
    }
    int get_index() const { return m_index; }
    bool is_busy() const { return m_busy; }
    bool is_running() const { return m_running; }
    void execute(std::function<void()> e) {
        m_e    = e;
        m_busy = true;
    }
    void terminate() { m_terminate = true; }

private:
    int m_index;
    std::function<void()> m_e;
    bool m_terminate = false;
    bool m_busy      = false;
    bool m_running   = true;
};