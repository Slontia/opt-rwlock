#include <condition_variable>
#include <mutex>
#include <atomic>
#include <shared_mutex>
#include <chrono>

namespace opt
{

class RWLock
{
 public:
  RWLock() : reading_count_(0), all_write_count_(0), is_writing_(false) {}
  RWLock(const RWLock&) = delete;

  template <typename ...Args>
  bool ReadLock(Args&&... args)
  {
    if (all_write_count_.load() == 0) {
      ++ reading_count_;
      if (all_write_count_.load() == 0) {
        return true;
      } else if (-- reading_count_ == 0) {
        // Shortly holding mutex is necessary because condition judgement and waiting lock
        // are not atomical.
        // Correct order to notify: 1) modify variables; 2) shortly lock; 3) notify
        (std::lock_guard<std::mutex>(m_));
        cv_.notify_all();
      }
    }
    std::unique_lock<std::mutex> l(m_);
    if (Wait_(l, [this] { return all_write_count_.load() == 0; }, std::forward<Args>(args)...)) {
      ++ reading_count_;
      return true;
    }
    return false;
  }

  template <typename ...Args>
  bool WriteLock(Args&&... args)
  {
    std::unique_lock<std::mutex> l(m_);
    ++ all_write_count_;
    if (Wait_(l, [this] { return reading_count_.load() == 0 && !is_writing_; }, std::forward<Args>(args)...)) {
      is_writing_ = true;
      return true;
    }
    -- all_write_count_;
    return false;
  }

  bool Unlock()
  {
    // Read is_writing_ need not hold mutex because is_writing_ will not be changed during
    // rwlock is held.
    if (is_writing_) {
      WriteUnlock_();
      return true;
    } else if (reading_count_ > 0) {
      ReadUnlock_();
      return true;
    }
    return false;
  }

 private:
  void ReadUnlock_()
  {
    if (-- reading_count_ == 0) {
      (std::lock_guard<std::mutex>(m_)); // shortly hold mutex
      cv_.notify_all();
    }
  }

  void WriteUnlock_()
  {
    {
      std::lock_guard<std::mutex> l(m_);
      is_writing_ = false;
      -- all_write_count_;
    }
    cv_.notify_all();
  }

  template <typename Lock, typename Cond>
  bool Wait_(Lock&& l, Cond&& cond)
  {
    cv_.wait(l, cond);
    return true;
  }

  template <typename Lock, typename Cond>
  bool Wait_(Lock&& l, Cond&& cond, std::try_to_lock_t)
  {
    return cond();
  }

  template <typename Lock, typename Cond, typename Rep, typename Period>
  bool Wait_(Lock&& l, Cond&& cond, const std::chrono::duration<Rep, Period>& duration)
  {
    if (!l) {
      l.lock();
    }
    return cv_.wait_for(l, duration, cond);
  }

  template <typename Lock, typename Cond, typename Clock, typename Duration>
  bool Wait_(Lock&& l, Cond&& cond, const std::chrono::time_point<Clock, Duration>& time_point)
  {
    if (!l) {
      l.lock();
    }
    return cv_.wait_until(l, time_point, cond);
  }

  std::mutex m_;
  std::condition_variable cv_;
  std::atomic<uint64_t> reading_count_;
  std::atomic<uint64_t> all_write_count_;
  bool is_writing_;
};

}
