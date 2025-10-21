#include <atomic>
#include <cassert>
#include <channel/channel.hpp>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace {

bool basic_round_trip() {
    Channel<int, 2> channel;

    int in = 42;
    channel.send(in);

    int out = 0;
    channel.receive(out);
    return out == 42;
}

bool move_semantics() {
    Channel<std::vector<int>, 1> channel;

    std::vector<int> payload{1, 2, 3};
    channel << std::move(payload);

    std::vector<int> received;
    channel >> received;

    return received.size() == 3 && received[0] == 1 && payload.empty();
}

bool blocking_behavior() {
    Channel<int, 1> channel;
    std::atomic<bool> second_send_started{false};
    std::atomic<bool> second_send_completed{false};
    std::atomic<int> second_value{0};

    int first = 1;
    channel.send(first);  // fills the buffer

    std::thread producer([&]() {
        int value = 2;
        second_send_started.store(true);
        channel.send(value);  // should block until a receive happens
        second_send_completed.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // second send should be waiting because buffer is full
    bool waiting = second_send_started.load() && !second_send_completed.load();

    int observed_first = 0;
    channel.receive(observed_first);

    std::thread consumer([&]() {
        int observed_second = 0;
        channel.receive(observed_second);
        second_value.store(observed_second);
    });

    producer.join();
    consumer.join();

    return waiting && observed_first == 1 && second_value.load() == 2;
}

bool consistency(const int n = 200) {
    std::atomic<int> cnt;
    Channel<int, 3> ch;
    auto producer_work = [&ch](int b, int e) {
        for (int i = b; i < e; i++) {
            std::cout << "sending " << i << "\n";
            ch.send(i);
        }
    };

    std::thread producer1(producer_work, 0, n);  // , n / 2);
    // std::thread producer2(producer_work, n / 2, n);

    std::vector<int> nums(n);
    auto consumer_work = [&nums, &ch]() {
        int i = 0;
        ch.receive(i);
        std::cout << "Received " << i << "\n";
        nums[i] = i;
    };

    std::thread consumer1(consumer_work);
    // std::thread consumer2(consumer_work);

    producer1.join();
    // producer2.join();
    consumer1.join();
    // consumer2.join();
    // consumer3.join();

    bool res = true;
    for (int i = 0; i < n; i++) {
        res &= (nums[i] == i);
    }
    return res;
}

}  // namespace

int main() {
    assert(basic_round_trip());
    assert(move_semantics());
    assert(blocking_behavior());
    assert(consistency());
    return 0;
}
