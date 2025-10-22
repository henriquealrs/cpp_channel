#include <atomic>
#include <channel/channel.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

TEST(ChannelTest, BasicRoundTrip) {
    Channel<int, 2> channel;

    int in = 42;
    channel.send(in);

    int out = 0;
    channel.receive(out);
    EXPECT_EQ(out, 42);
}

TEST(ChannelTest, MoveSemantics) {
    Channel<std::vector<int>, 1> channel;

    std::vector<int> payload{1, 2, 3};
    channel << std::move(payload);

    std::vector<int> received;
    channel >> received;

    EXPECT_EQ(received.size(), 3);
    EXPECT_EQ(received[0], 1);
    EXPECT_TRUE(payload.empty());
}

TEST(ChannelTest, BlockingBehavior) {
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

    EXPECT_TRUE(waiting);
    EXPECT_EQ(observed_first, 1);
    EXPECT_EQ(second_value.load(), 2);
}

TEST(ChannelTest, Consistency) {
    constexpr int n = 200;
    Channel<int, 3> ch;
    auto producer_work = [&ch](int b, int e) {
        for (int i = b; i < e; i++) {
            ch.send(i);
        }
    };

    std::thread producer1(producer_work, 0, n / 2);
    std::thread producer2(producer_work, n / 2, n);

    std::vector<int> nums(n, -1);
    std::atomic<int> cnt{0};
    auto consumer_work = [&nums, &ch, &cnt]() {
        while (true) {
            int value = -1;
            ch.receive(value);
            if (value < 0) {
                break;
            }
            nums[value] = value;
            cnt.fetch_add(1);
        }
    };

    std::thread consumer1(consumer_work);
    std::thread consumer2(consumer_work);
    std::thread consumer3(consumer_work);

    producer1.join();
    producer2.join();

    for (int i = 0; i < 3; ++i) {
        int sentinel = -1;
        ch.send(sentinel);
    }

    consumer1.join();
    consumer2.join();
    consumer3.join();

    EXPECT_EQ(cnt.load(), n);
    for (int i = 0; i < n; i++) {
        EXPECT_EQ(nums[i], i);
    }
}
