#pragma once
#include <thread>
#include <functional>
#include <memory>

class FunctionWorker {
public:
    static std::vector<std::unique_ptr<FunctionWorker>> create_workers(int num_threads) {
        std::vector<std::unique_ptr<FunctionWorker>> workers;
        for (int i = 0; i < num_threads; i++) {
            workers.emplace_back(std::make_unique<FunctionWorker>(i));
        }
        return workers;
    }

public:
    // thread sanitizer would report a data race on m_busy and m_running,
    // but this is ok because usage should not allow actual accidental races
    FunctionWorker(int idx) : m_index(idx) {
        m_thread = new std::thread([=]() {
            while (!m_terminate) {
                if (is_busy()) {
                    m_exec_mutex.lock();
                    m_exec();
                    m_exec = nullptr;
                    m_exec_mutex.unlock();
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        });
    }
    ~FunctionWorker() { delete m_thread; }

    int get_index() const { return m_index; }
    bool is_busy() const { return m_exec != nullptr; }
    void execute(std::function<void()> e) {
        m_exec_mutex.lock();
        m_exec = e;
        m_exec_mutex.unlock();
    }
    void terminate() {
        m_terminate = true;
        m_thread->join();
    }

private:
    std::thread* m_thread;
    std::mutex m_exec_mutex;
    std::function<void()> m_exec = nullptr;
    int m_index;
    bool m_terminate = false;
};