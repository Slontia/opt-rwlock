#include "lock.h"
#include <thread>
#include <vector>
#include <iostream>
#include <chrono>

static const constexpr int kReadThreadNum = 10;
static const constexpr int kWriteThreadNum = 1;
static const constexpr int kAddCount = 10;
static const constexpr int kAddNum = 5;

template <typename Task>
void CreateThreads(std::vector<std::thread>& threads, const int num, Task&& task)
{
  for (int i = 0; i < num; ++ i) {
    threads.emplace_back(std::forward<Task>(task));
  }
}

template <typename RWLock>
void TestLock()
{
  std::cout << "[INFO] === start test " << typeid(RWLock).name() << " ===" << std::endl;

  std::vector<std::thread> read_threads;
  std::vector<std::thread> write_threads;
  RWLock lock;
  volatile int64_t a = 0;
  volatile int64_t b = 0;
  std::atomic<bool> is_over = false;

  auto start = std::chrono::system_clock::now();

  CreateThreads(write_threads, kWriteThreadNum, [&a, &b, &lock] {
        for (int i = 0; i < kAddCount; ++ i) {
          lock.WriteLock();
          for (int j = 0; j < kAddNum; ++ j) { ++ a; }
          for (int j = 0; j < kAddNum; ++ j) { -- b; }
          lock.Unlock();
        }
      });

  CreateThreads(read_threads, kReadThreadNum, [&is_over, &a, &b, &lock] {
        while (!is_over.load()) {
          lock.ReadLock();
          if (a + b != 0) {
            std::cerr << "[ERROR] read thread mismatch (" << a << ", " << b << ")" << std::endl;
            lock.Unlock();
            return;
          }
          lock.Unlock();
        }
      });

  for (auto& t : write_threads) { t.join(); }

  std::chrono::duration<double> write_over_diff = std::chrono::system_clock::now() - start;

  is_over = true;
  for (auto& t : read_threads) { t.join(); }

  std::chrono::duration<double> read_over_diff = std::chrono::system_clock::now() - start;

  if (a + b != 0 || a != kWriteThreadNum * kAddCount * kAddNum) {
    std::cerr << "[ERROR] mismatch (" << a << ", " << b << ") expected: " << kWriteThreadNum * kAddCount * kAddNum << std::endl;
  }

  std::cout << "[INFO] write over: " << write_over_diff.count() << "s, read over: " << read_over_diff.count() << "s" << std::endl;
};

int main()
{
  TestLock<EffectiveRWLock>();
  TestLock<StdRWLock>();
  return 0;
}

