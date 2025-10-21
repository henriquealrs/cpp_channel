#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>

template <typename T, int N = 0>
class Channel {
   public:
    Channel() = default;
    Channel(const Channel& other) = delete;
    Channel& operator=(const Channel& other) = delete;

   private:
    std::atomic<int> counter_{0};
    std::array<T, N> buffer_{};
    std::mutex data_mutex_;
    std::condition_variable cv_;

   public:
    void send(T& data) noexcept {
        std::unique_lock<std::mutex> lk(data_mutex_);
        cv_.wait(lk, [&cnt = counter_]() { return cnt < N; });
        buffer_[counter_++] = std::move(data);
        cv_.notify_all();
    }

    void send(T&& data) noexcept {
        std::unique_lock<std::mutex> lk(data_mutex_);
        cv_.wait(lk, [&cnt = counter_]() { return cnt < N; });
        buffer_[counter_++] = std::move(data);
        cv_.notify_all();
    }

    void send(const T& data) noexcept {
        std::unique_lock<std::mutex> lk(data_mutex_);
        cv_.wait(lk, [&cnt = counter_]() { return cnt < N; });
        buffer_[counter_++] = data;
        cv_.notify_all();
    }

    void receive(T& data) noexcept {
        std::unique_lock<std::mutex> lk(data_mutex_);
        cv_.wait(lk, [&cnt = counter_] { return cnt > 0; });
        data = std::move(buffer_[--counter_]);
        cv_.notify_all();
    }

    void receive(T&& data) noexcept {
        std::unique_lock<std::mutex> lk(data_mutex_);
        cv_.wait(lk, [&cnt = counter_] { return cnt > 0; });
        data = std::move(buffer_[--counter_]);
        cv_.notify_all();
    }

    void operator<<(const T& data) { send(data); }
    void operator<<(T& data) { send(data); }
};

template <typename T, int N>
void operator>>(Channel<T, N>& ch, T& data) {
    ch.receive(data);
}
