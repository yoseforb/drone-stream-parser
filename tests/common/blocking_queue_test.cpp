#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <optional>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "blocking_queue.hpp"

// NOLINTBEGIN(readability-magic-numbers,readability-identifier-length,bugprone-unchecked-optional-access,readability-function-cognitive-complexity)

namespace {} // namespace

TEST(BlockingQueueTest, PushThenPopReturnsSameItem) {
  BlockingQueue<int> queue{4};
  queue.push(42);
  auto val = queue.pop();

  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, 42);
}

TEST(BlockingQueueTest, PopOnEmptyQueueBlocksUntilPush) {
  BlockingQueue<int> queue{4};
  std::optional<int> val;

  std::thread producer([&queue] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    queue.push(99);
  });

  val = queue.pop();
  producer.join();

  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, 99);
}

TEST(BlockingQueueTest, PushOnFullQueueBlocksUntilPop) {
  BlockingQueue<int> queue{2};
  queue.push(1);
  queue.push(2);

  std::thread consumer([&queue] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    auto discarded = queue.pop();
    (void)discarded;
  });

  queue.push(3);
  consumer.join();

  std::vector<int> collected;
  auto val = queue.pop();
  ASSERT_TRUE(val.has_value());
  collected.push_back(*val);
  val = queue.pop();
  ASSERT_TRUE(val.has_value());
  collected.push_back(*val);
}

TEST(BlockingQueueTest, CloseUnblocksBlockedPop) {
  BlockingQueue<int> queue{4};
  std::optional<int> result;

  std::thread popper([&queue, &result] { result = queue.pop(); });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  queue.close();
  popper.join();

  EXPECT_FALSE(result.has_value());
}

TEST(BlockingQueueTest, CloseUnblocksBlockedPush) {
  BlockingQueue<int> queue{1};
  queue.push(0);

  std::thread pusher([&queue] { queue.push(42); });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  queue.close();
  pusher.join();

  auto val = queue.pop();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, 0);
  auto val2 = queue.pop();
  EXPECT_FALSE(val2.has_value());
}

TEST(BlockingQueueTest, ItemsPushedBeforeCloseAreStillRetrievable) {
  BlockingQueue<int> queue{8};
  queue.push(10);
  queue.push(20);
  queue.push(30);
  queue.close();

  std::vector<int> drained;
  while (auto val = queue.pop()) {
    drained.push_back(*val);
  }

  ASSERT_EQ(drained.size(), 3U);
  EXPECT_EQ(drained[0], 10);
  EXPECT_EQ(drained[1], 20);
  EXPECT_EQ(drained[2], 30);
}

TEST(BlockingQueueTest,
     MultipleProducersAndConsumersDeliverAllItemsExactlyOnce) {
  BlockingQueue<int> queue{16};
  constexpr int NumProducers = 4;
  constexpr int NumConsumers = 4;
  constexpr int ItemsPerProducer = 250;
  constexpr int TotalItems = NumProducers * ItemsPerProducer;

  std::atomic<int> consumed_count{0};
  std::vector<std::vector<int>> per_consumer(NumConsumers);

  std::vector<std::thread> producers;
  producers.reserve(NumProducers);
  for (int producer_idx = 0; producer_idx < NumProducers; ++producer_idx) {
    producers.emplace_back([&queue, producer_idx] {
      for (int item = 0; item < ItemsPerProducer; ++item) {
        queue.push((producer_idx * ItemsPerProducer) + item);
      }
    });
  }

  std::vector<std::thread> consumers;
  consumers.reserve(NumConsumers);
  for (size_t consumer_idx = 0; consumer_idx < NumConsumers; ++consumer_idx) {
    consumers.emplace_back(
        [&queue, &consumed_count, &per_consumer, consumer_idx] {
          while (auto val = queue.pop()) {
            per_consumer[consumer_idx].push_back(*val);
            consumed_count.fetch_add(1, std::memory_order_relaxed);
          }
        });
  }

  for (auto& thread : producers) {
    thread.join();
  }
  queue.close();
  for (auto& thread : consumers) {
    thread.join();
  }

  EXPECT_EQ(consumed_count.load(), TotalItems);

  std::vector<int> all_values;
  for (const auto& bucket : per_consumer) {
    all_values.insert(all_values.end(), bucket.begin(), bucket.end());
  }
  std::ranges::sort(all_values);

  ASSERT_EQ(static_cast<int>(all_values.size()), TotalItems);
  for (size_t idx = 0; idx < static_cast<size_t>(TotalItems); ++idx) {
    EXPECT_EQ(all_values[idx], static_cast<int>(idx));
  }
}

// NOLINTEND(readability-magic-numbers,readability-identifier-length,bugprone-unchecked-optional-access,readability-function-cognitive-complexity)
