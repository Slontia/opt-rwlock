#include <condition_variable>
#include <mutex>
#include <atomic>
#include <shared_mutex>

class OptRWLock
{
 public:
  OptRWLock() : reading_count_(0), all_write_count_(0), is_writing_(false) {}
  OptRWLock(const OptRWLock&) = delete;

  bool ReadLock()
  {
    do {
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
    } while ([this] {
               std::unique_lock<std::mutex> l(m_);
               cv_.wait(l, [this] { return all_write_count_.load() == 0; });
               return true;
             }());
    return false;
  }

  bool WriteLock()
  {
    std::unique_lock<std::mutex> l(m_);
    ++ all_write_count_;
    cv_.wait(l, [this] { return reading_count_.load() == 0 && !is_writing_; });
    is_writing_ = true;
    return true;
  }

  void Unlock()
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

  std::mutex m_;
  std::condition_variable cv_;
  std::atomic<uint64_t> reading_count_;
  std::atomic<uint64_t> all_write_count_;
  bool is_writing_;
};


