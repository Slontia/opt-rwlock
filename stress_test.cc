#include "lock.h"
#include <thread>
#include <vector>
#include <iostream>
#include <chrono>
#include <gflags/gflags.h>

DEFINE_uint64(read_threads, 100, "Number of reading threads");
DEFINE_uint64(write_threads, 1, "Number of writing threads");
DEFINE_uint64(increase_times, 100, "Increase times");
DEFINE_uint64(increase_size, 5, "Increase size for each writing");

class StdRWLock
{
 public:
  StdRWLock() {}
  StdRWLock(const StdRWLock&) = delete;

  void ReadLock()
  {
    m_.lock_shared();
    is_unique_ = false;
  }

  void WriteLock()
  {
    m_.lock();
    is_unique_ = true;
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

  void ReadLock()
  {
    std::unique_lock<std::mutex> l(mu_);
    cv_.wait(l, [this] { return all_write_count_ == 0; });
    ++ reading_count_;
  }

  void WriteLock() // same as OptRWLock
  {
    std::unique_lock<std::mutex> l(mu_);
    ++ all_write_count_;
    cv_.wait(l, [this] { return reading_count_ == 0 && !is_writing_; });
    is_writing_ = true;
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

  CreateThreads(write_threads, FLAGS_write_threads, [&a, &b, &lock] {
        for (int i = 0; i < FLAGS_increase_times; ++ i) {
          lock.WriteLock();
          for (int j = 0; j < FLAGS_increase_size; ++ j) { ++ a; }
          for (int j = 0; j < FLAGS_increase_size; ++ j) { -- b; }
          lock.Unlock();
        }
      });

  CreateThreads(read_threads, FLAGS_read_threads, [&is_over, &a, &b, &lock] {
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

  if (a + b != 0 || a != FLAGS_write_threads * FLAGS_increase_times * FLAGS_increase_size) {
    std::cerr << "[ERROR] mismatch (" << a << ", " << b << ") expected: " << FLAGS_write_threads * FLAGS_increase_times * FLAGS_increase_size << std::endl;
  }

  std::cout << "[INFO] write over: " << write_over_diff.count() << "s, read over: " << read_over_diff.count() << "s" << std::endl;
};

int main()
{
  std::cout << "[INFO] reading threads: " << FLAGS_read_threads << std::endl;
  std::cout << "[INFO] writing threads: " << FLAGS_write_threads << std::endl;
  std::cout << "[INFO] increase times: " << FLAGS_increase_times << std::endl;
  std::cout << "[INFO] increase size: " << FLAGS_increase_size << std::endl;

  TestLock<opt::RWLock>();
  TestLock<CondRWLock>();
  TestLock<StdRWLock>();
  return 0;
}

