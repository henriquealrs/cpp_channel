#include <gtest/gtest.h>

#include <atomic>
#include <channel/channel.hpp>
#include <chrono>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

TEST(ChannelTest, BasicRoundTrip) {
    Channel<int, 2> channel;

    int in = 42;
    channel.send(in);

    auto out = channel.receive();
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out.value(), 42);
}

TEST(ChannelTest, MoveSemantics) {
    Channel<std::vector<int>, 1> channel;

    std::vector<int> payload{1, 2, 3};
    channel << std::move(payload);

    std::vector<int> received;
    auto maybeReceived = channel.receive();
    ASSERT_TRUE(maybeReceived.has_value());
    received = std::move(maybeReceived.value());

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

    auto first_result = channel.receive();
    ASSERT_TRUE(first_result.has_value());
    int observed_first = first_result.value();

    std::thread consumer([&]() {
        auto second_result = channel.receive();
        if (second_result.has_value()) {
            second_value.store(second_result.value());
        }
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
            auto maybeValue = ch.receive();
            ASSERT_TRUE(maybeValue.has_value());
            int value = maybeValue.value();
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

TEST(ChannelTest, ConsistencyWithClose) {
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
            auto maybeValue = ch.receive();
            if (!maybeValue.has_value()) {
                break;
            }
            nums[maybeValue.value()] = maybeValue.value();
            cnt.fetch_add(1);
        }
    };

    std::thread consumer1(consumer_work);
    std::thread consumer2(consumer_work);
    std::thread consumer3(consumer_work);

    producer1.join();
    producer2.join();

    ch.close();

    consumer1.join();
    consumer2.join();
    consumer3.join();

    EXPECT_EQ(cnt.load(), n);
    for (int i = 0; i < n; i++) {
        EXPECT_EQ(nums[i], i);
    }
}

TEST(ChannelTest, HighVolumeMultiProducerMultiConsumer) {
    constexpr int producers = 4;
    constexpr int consumers = 4;
    constexpr int per_producer = 500;
    constexpr int total = producers * per_producer;
    Channel<int, 16> ch;

    auto producer_work = [&ch, per_producer](int offset) {
        for (int i = 0; i < per_producer; ++i) {
            ch.send(offset + i);
        }
    };

    std::vector<std::thread> producer_threads;
    for (int p = 0; p < producers; ++p) {
        producer_threads.emplace_back(producer_work, p * per_producer);
    }

    std::vector<int> counts(total, 0);
    std::mutex counts_mutex;
    std::atomic<int> received{0};
    auto consumer_work = [&]() {
        while (true) {
            auto maybeValue = ch.receive();
            ASSERT_TRUE(maybeValue.has_value());
            int value = maybeValue.value();
            if (value < 0) {
                break;
            }
            EXPECT_LT(value, total);
            {
                std::lock_guard<std::mutex> lock(counts_mutex);
                counts[value]++;
            }
            received.fetch_add(1);
        }
    };

    std::vector<std::thread> consumer_threads;
    for (int c = 0; c < consumers; ++c) {
        consumer_threads.emplace_back(consumer_work);
    }

    for (auto& t : producer_threads) {
        t.join();
    }

    for (int i = 0; i < consumers; ++i) {
        int sentinel = -1;
        ch.send(sentinel);
    }

    for (auto& t : consumer_threads) {
        t.join();
    }

    EXPECT_EQ(received.load(), total);
    for (int i = 0; i < total; ++i) {
        EXPECT_EQ(counts[i], 1);
    }
}

TEST(ChannelTest, ProducerSequenceIntegrity) {
    struct Packet {
        int producer{-1};
        int sequence{-1};
    };

    constexpr int n_producers = 5;
    constexpr int per_producer = 2000;
    Channel<Packet, 8> ch;
    std::mutex log_mut;

    auto producer_work = [&ch, per_producer, &log_mut](const int id) {
        for (int i = 0; i < per_producer; ++i) {
            ch.send(Packet{id, i});
        }
    };

    std::vector<std::thread> producers;
    for (int i = 0; i < n_producers; ++i) {
        producers.emplace_back(producer_work, i);
    }

    std::this_thread::yield();

    std::vector<std::vector<int>> results(n_producers);
    std::mutex consumer_mutex;
    auto consumer_work = [&ch, &consumer_mutex, &results, &log_mut]() {
        while (1) {
            std::lock_guard lk(consumer_mutex);
            auto maybePacked = ch.receive();
            if (!maybePacked.has_value()) break;
            Packet p = maybePacked.value();
            const int id = p.producer;
            const int val = p.sequence;
            results[id].push_back(val);
        }
    };

    constexpr int n_consumers = 5;
    std::vector<std::thread> consumers;
    for (int i = 0; i < n_consumers; ++i) {
        consumers.emplace_back(consumer_work);
    }

    std::this_thread::yield();

    for (auto& t : producers) {
        t.join();
    }
    ch.close();

    for (auto& t : consumers) {
        t.join();
    }

    // ASSERT number of messages
    for (const auto& res : results) {
        ASSERT_EQ(res.size(), per_producer);
    }

    // ASSERT Consistency
    for (const auto& res : results) {
        for (int i = 0; i < res.size(); ++i) {
            ASSERT_EQ(i, res[i]);
        }
    }
}

TEST(ChannelTest, ThroughputExpectation) {
    constexpr int iterations = 2000;
    Channel<int, 4> ch;

    auto start = std::chrono::steady_clock::now();

    std::thread producer([&]() {
        for (int i = 0; i < iterations; ++i) {
            ch.send(i);
        }
        int sentinel = -1;
        ch.send(sentinel);
    });

    std::vector<int> received_values;
    received_values.reserve(iterations);

    std::thread consumer([&]() {
        while (true) {
            auto maybeValue = ch.receive();
            ASSERT_TRUE(maybeValue.has_value());
            int value = maybeValue.value();
            if (value < 0) {
                break;
            }
            received_values.push_back(value);
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::steady_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_EQ(received_values.size(), static_cast<size_t>(iterations));
    EXPECT_LT(duration.count(), 30);  // Expect completion within 3
}

TEST(ChannelCloseTest, ReceiversDrainThenExit) {
    Channel<int, 2> ch;
    std::thread producer([&]() {
        for (int i = 0; i < 10; ++i) {
            ch.send(i);
        }
        ch.close();
    });

    std::vector<int> received;
    std::thread consumer([&]() {
        while (true) {
            auto value = ch.receive();
            if (!value.has_value()) {
                break;
            }
            received.push_back(value.value());
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(received.size(), 10);
    EXPECT_EQ(received.front(), 0);
    EXPECT_EQ(received.back(), 9);
}

TEST(ChannelCloseTest, CloseIsIdempotent) {
    Channel<int, 1> ch;
    ch.close();
    EXPECT_NO_THROW(ch.close());

    auto value = ch.receive();
    EXPECT_FALSE(value.has_value());
}

TEST(ChannelCloseTest, SendAfterCloseThrows) {
    Channel<int, 1> ch;
    ch.close();
    int payload = 7;
    EXPECT_THROW(ch.send(payload), std::runtime_error);
}
