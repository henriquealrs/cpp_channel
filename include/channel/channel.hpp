#ifndef CHANNEL_CHANNEL_HPP
#define CHANNEL_CHANNEL_HPP

#include <array>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>

template <typename T, int N = 0> class Channel {
  static_assert(N > 0, "Channel capacity N must be greater than zero");

public:
  Channel() = default;
  Channel(const Channel &other) = delete;
  Channel &operator=(const Channel &other) = delete;

private:
  std::array<std::optional<T>, N> buffer_{};
  std::size_t head_ = 0;
  std::size_t tail_ = 0;
  std::size_t count_ = 0;
  std::mutex data_mutex_;
  std::condition_variable data_available_;
  std::condition_variable space_available_;

  template <typename U> void enqueue(U &&data) {
    std::unique_lock<std::mutex> lock(data_mutex_);
    space_available_.wait(lock, [this]() { return count_ < N; });

    buffer_[tail_] = std::forward<U>(data);
    tail_ = (tail_ + 1) % N;
    ++count_;

    lock.unlock();
    data_available_.notify_one();
  }

public:
  void send(const T &data) { enqueue(data); }

  void send(T &&data) { enqueue(std::move(data)); }

  void receive(T &data) {
    std::unique_lock<std::mutex> lock(data_mutex_);
    data_available_.wait(lock, [this]() { return count_ > 0; });

    std::optional<T> &slot = buffer_[head_];
    data = std::move(*slot);
    slot.reset();

    head_ = (head_ + 1) % N;
    --count_;

    lock.unlock();
    space_available_.notify_one();
  }

  void operator<<(const T &data) { send(data); }

  void operator<<(T &&data) { send(std::move(data)); }
};

template <typename T, int N> void operator>>(Channel<T, N> &ch, T &data) {
  ch.receive(data);
}

#endif // CHANNEL_CHANNEL_HPP
