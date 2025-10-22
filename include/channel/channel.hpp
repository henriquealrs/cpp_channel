#include <array>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>

template <typename T, int N = 1>
class Channel {
   public:
    Channel() = default;
    Channel(const Channel& other) = delete;
    Channel& operator=(const Channel& other) = delete;

   private:
    std::atomic<int> spaces_available_{N};
    std::atomic<int> receive_pos_{0};
    std::atomic<int> send_pos_{0};
    std::array<T, N> buffer_{};
    std::mutex data_mutex_;
    std::condition_variable send_cv_;
    std::condition_variable receive_cv_;

   public:
    void send(T& data) {
        {
            std::unique_lock<std::mutex> lk(data_mutex_);
            send_cv_.wait(
                lk, [&cnt = spaces_available_]() { return cnt.load() > 0; });
            buffer_[send_pos_] = data;
            send_pos_.store((send_pos_.load() + 1) % N);
            spaces_available_.fetch_sub(1);
        }
        receive_cv_.notify_all();
    }

    void send(T&& data) {
        {
            std::unique_lock<std::mutex> lk(data_mutex_);
            send_cv_.wait(
                lk, [&cnt = spaces_available_]() { return cnt.load() > 0; });
            buffer_[send_pos_] = std::move(data);
            send_pos_.store((send_pos_.load() + 1) % N);
            spaces_available_.fetch_sub(1);
        }
        receive_cv_.notify_all();
    }

    void send(const T& data) {
        {
            std::unique_lock<std::mutex> lk(data_mutex_);
            send_cv_.wait(
                lk, [&cnt = spaces_available_]() { return cnt.load() > 0; });
            buffer_[send_pos_] = data;
            send_pos_.store((send_pos_.load() + 1) % N);
            spaces_available_.fetch_sub(1);
        }
        receive_cv_.notify_all();
    }

    void receive(T& data) {
        {
            std::unique_lock<std::mutex> lk(data_mutex_);
            receive_cv_.wait(
                lk, [&cnt = spaces_available_] { return cnt.load() < N; });
            data = buffer_[receive_pos_.load()];
            receive_pos_.store((receive_pos_.load() + 1) % N);
            spaces_available_.fetch_add(1);
        }
        send_cv_.notify_all();
    }

    void operator<<(const T& data) { send(data); }
    void operator<<(T& data) { send(data); }
    void operator<<(T&& data) { send(std::move(data)); }
};

template <typename T, int N>
void operator>>(Channel<T, N>& ch, T& data) {
    ch.receive(data);
}
