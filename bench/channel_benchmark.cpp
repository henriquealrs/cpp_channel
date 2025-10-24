#include <channel/channel.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct BenchmarkResult {
  std::string label;
  std::size_t messages{0};
  int producers{0};
  int consumers{0};
  int capacity{0};
  std::chrono::duration<double> elapsed{};
  std::size_t consumed{0};

  double throughput() const {
    if (elapsed.count() == 0.0) return 0.0;
    return static_cast<double>(messages) / elapsed.count();
  }
};

template <int Capacity>
BenchmarkResult runScenario(std::string label, std::size_t messages,
                            int producers, int consumers) {
  BenchmarkResult result{
      .label = std::move(label),
      .messages = messages,
      .producers = producers,
      .consumers = consumers,
      .capacity = Capacity,
  };

  Channel<int, Capacity> channel;
  std::atomic<std::size_t> consumed{0};

  auto start = std::chrono::steady_clock::now();

  std::vector<std::thread> consumerThreads;
  consumerThreads.reserve(consumers);
  for (int i = 0; i < consumers; ++i) {
    consumerThreads.emplace_back([&]() {
      while (true) {
        auto maybeValue = channel.receive();
        if (!maybeValue.has_value()) {
          break;
        }
        consumed.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  auto producerWork = [&](int id) {
    const std::size_t begin = (messages / producers) * id;
    const std::size_t end =
        id == producers - 1 ? messages : (messages / producers) * (id + 1);
    for (std::size_t i = begin; i < end; ++i) {
      channel.send(static_cast<int>(i));
    }
  };

  std::vector<std::thread> producerThreads;
  producerThreads.reserve(producers);
  for (int i = 0; i < producers; ++i) {
    producerThreads.emplace_back(producerWork, i);
  }

  for (auto& t : producerThreads) {
    t.join();
  }
  channel.close();

  for (auto& t : consumerThreads) {
    t.join();
  }

  auto finish = std::chrono::steady_clock::now();
  result.elapsed =
      std::chrono::duration_cast<std::chrono::duration<double>>(finish - start);
  result.consumed = consumed.load(std::memory_order_relaxed);

  return result;
}

void printResult(const BenchmarkResult& result) {
  std::cout << "\nScenario: " << result.label << '\n';
  std::cout << "  messages      : " << result.messages << '\n';
  std::cout << "  producers     : " << result.producers << '\n';
  std::cout << "  consumers     : " << result.consumers << '\n';
  std::cout << "  capacity      : " << result.capacity << '\n';
  std::cout << "  consumed      : " << result.consumed << '\n';
  std::cout << "  elapsed (s)   : " << std::fixed << std::setprecision(6)
            << result.elapsed.count() << '\n';
  std::cout << "  throughput/s  : " << std::fixed << std::setprecision(2)
            << result.throughput() << '\n';
}

}  // namespace

int main() {
  constexpr std::size_t messages = 200'000;

  std::vector<BenchmarkResult> results;
  results.reserve(3);

  results.push_back(
      runScenario<1>("Single producer/consumer (capacity 1)", messages, 1, 1));

  results.push_back(runScenario<4>("Dual producers/consumers (capacity 4)",
                                   messages, 2, 2));

  results.push_back(
      runScenario<16>("Fan-in/out (capacity 16)", messages, 4, 4));

  std::cout << "Channel throughput benchmark\n";
  std::cout << "=============================\n";
  for (const auto& result : results) {
    printResult(result);
  }

  std::cout << std::endl;
  return 0;
}
