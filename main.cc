#include "lock.h"
#include <thread>
#include <vector>
#include <iostream>
#include <chrono>

static const constexpr int kReadThreadNum = 1000;
static const constexpr int kWriteThreadNum = 1;
static const constexpr int kAddCount = 10;
static const constexpr int kAddNum = 5;

class StdRWLock
{
 public:
  bool ReadLock()
  {
    m_.lock_shared();
    is_unique_ = false;
    return true;
  }
  bool WriteLock()
  {
    m_.lock();
    is_unique_ = true;
    return true;
  }
  void Unlock() { is_unique_ ? m_.unlock() : m_.unlock_shared(); }
 private:
  bool is_unique_;
  std::shared_mutex m_;
};

class CondRWLock
{
 public:
  CondRWLock() : reading_count_(0), all_write_count_(0), is_writing_(false) {}
  CondRWLock(const CondRWLock&) = delete;
  CondRWLock(CondRWLock&&) = delete;

  bool ReadLock()
  {
    std::unique_lock<std::mutex> l(mu_);
    cv_.wait(l, [this] { return all_write_count_ == 0; });
    ++ reading_count_;
    return true;
  }

  bool WriteLock() // same as OptRWLock
  {
    std::unique_lock<std::mutex> l(mu_);
    ++ all_write_count_;
    cv_.wait(l, [this] { return reading_count_ == 0 && !is_writing_; });
    is_writing_ = true;
    return true;
  }

  void Unlock() // same as OptRWLock
  {
    // Read is_writing_ need not hold mutex because is_writing_ will not be changed during
    // rwlock is held.
    if (is_writing_) {
      WriteUnlock_();
    } else {
      ReadUnlock_();
    }
  }
 private:
  void ReadUnlock_()
  {
    std::unique_lock<std::mutex> l(mu_);
    if (-- reading_count_ == 0) {
      l.unlock();
      cv_.notify_one();
    }
  }

  void WriteUnlock_() // same as OptRWLock
  {
    {
      std::lock_guard<std::mutex> l(mu_);
      is_writing_ = false;
      -- all_write_count_;
    }
    cv_.notify_all();
  }

  uint64_t reading_count_;
  uint64_t all_write_count_;
  bool is_writing_;
  std::condition_variable cv_;
  std::mutex mu_;
};


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
  int64_t a = 0;
  int64_t b = 0;
  std::atomic<bool> is_over(false);

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
  TestLock<OptRWLock>();
  TestLock<StdRWLock>();
  TestLock<CondRWLock>();
  return 0;
}

