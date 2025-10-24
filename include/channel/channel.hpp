#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <utility>

template <typename T, int N = 1>
class Channel {
   public:
    Channel() = default;
    Channel(const Channel& other) = delete;
    Channel& operator=(const Channel& other) = delete;
    enum class SendResult { Success, Full, Closed };
    enum class RecvResult { Success, Empty, Closed };

   private:
    std::atomic<int> spaces_available_{N};
    std::atomic<int> receive_pos_{0};
    std::atomic<int> send_pos_{0};
    std::array<T, N> buffer_{};
    std::atomic<bool> closed_{false};
    std::mutex data_mutex_;
    std::condition_variable send_cv_;
    std::condition_variable receive_cv_;

    inline bool is_emtpy() const noexcept {
        return spaces_available_.load() == N;
    }
    inline bool is_full() const noexcept {
        return spaces_available_.load() == 0;
    }

    inline bool can_terminate() const noexcept {
        return is_closed() && is_emtpy();
    }

   public:
    class send_after_close : public std::runtime_error {
       public:
        send_after_close(std::string m) : std::runtime_error(m) {}
    };

    void send(T& data) {
        {
            std::unique_lock<std::mutex> lk(data_mutex_);
            send_cv_.wait(lk, [&]() {
                return !is_full() || closed_.load(std::memory_order_relaxed);
            });
            if (closed_.load(std::memory_order_relaxed)) {
                throw send_after_close("Send data after channel closed");
            }
            const auto pos = send_pos_.load();
            buffer_[pos] = data;
            send_pos_.store((pos + 1) % N);
            spaces_available_.fetch_sub(1);
        }
        receive_cv_.notify_all();
    }

    void send(T&& data) {
        {
            std::unique_lock<std::mutex> lk(data_mutex_);
            send_cv_.wait(lk, [&]() {
                return !is_full() || closed_.load(std::memory_order_relaxed);
            });
            if (closed_.load(std::memory_order_relaxed)) {
                throw send_after_close("Send data after channel closed");
            }
            auto pos = send_pos_.load();
            buffer_[pos] = std::move(data);
            send_pos_.store((pos + 1) % N);
            spaces_available_.fetch_sub(1);
        }
        receive_cv_.notify_all();
    }

    void send(const T& data) {
        {
            std::unique_lock<std::mutex> lk(data_mutex_);
            send_cv_.wait(lk, [&]() { return !is_full() || closed_.load(); });
            if (closed_.load(std::memory_order_relaxed)) {
                throw send_after_close("Send data after channel closed");
            }
            auto pos = send_pos_.load();
            buffer_[pos] = data;
            send_pos_.store((pos + 1) % N);
            spaces_available_.fetch_sub(1);
        }
        receive_cv_.notify_all();
    }

    std::optional<T> receive() {
        std::optional<T> ret;
        {
            std::unique_lock<std::mutex> lk(data_mutex_);
            receive_cv_.wait(lk,
                             [&]() { return !is_emtpy() || can_terminate(); });

            const int free = spaces_available_.load();
            const bool closed = closed_.load();
            if (can_terminate()) {
                return std::nullopt;
            }

            // There is data to read.
            auto pos = receive_pos_.load();
            ret.emplace(std::move(buffer_[pos]));
            receive_pos_.store((pos + 1) % N);
            spaces_available_.fetch_add(1);
        }
        send_cv_.notify_all();
        return ret;
    }

    void close() noexcept {
        {
            std::lock_guard lk(data_mutex_);
            this->closed_.store(true);
        }
        receive_cv_.notify_all();
        send_cv_.notify_all();
    }

    SendResult try_send(const T& data) {
        std::unique_lock lk(data_mutex_, std::try_to_lock);
        if (!lk.owns_lock()) {
            return SendResult::Full;
        }
        if (is_closed()) {
            return SendResult::Closed;
        }
        if (is_full()) {
            return SendResult::Full;
        }
        const auto pos = send_pos_.load();
        buffer_[pos] = data;
        send_pos_.store((pos + 1) % N);
        spaces_available_.fetch_sub(1);

        lk.unlock();
        receive_cv_.notify_all();
        return SendResult::Success;
    }

    std::pair<RecvResult, std::optional<T>> try_receive() {
        std::unique_lock lk(data_mutex_, std::try_to_lock);
        if (!lk.owns_lock()) {
            return make_pair(RecvResult::Empty, std::optional<T>{});
        }
        if (is_closed()) {
            return make_pair(RecvResult::Closed, std::optional<T>{});
        }
        if (is_emtpy()) {
            return make_pair(RecvResult::Empty, std::optional<T>{});
        }
        const auto pos = receive_pos_.load();
        std::optional<T> result = std::move(buffer_[pos]);
        receive_pos_.store((pos + 1) % N);
        spaces_available_.fetch_add(1);

        lk.unlock();
        send_cv_.notify_all();
        return std::make_pair(RecvResult::Success, result);
    }

    inline bool is_closed() const noexcept { return closed_.load(); }

    void operator<<(const T& data) { send(data); }
    void operator<<(T& data) { send(data); }
    void operator<<(T&& data) { send(std::move(data)); }
};

template <typename T, int N>
void operator>>(Channel<T, N>& ch, T& data) {
    ch.receive(data);
}

template <class... Ops>
int select_nb(Ops&&... ops) {
    std::array<std::function<bool()>, sizeof...(Ops)> v{
        std::forward<Ops>(ops)...};
    thread_local std::mt19937 rng{std::random_device{}()};
    std::shuffle(v.begin(), v.end(), rng);
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i]()) return static_cast<int>(i);
    return -1;  // default
}
