#include <condition_variable>
#include <mutex>
#include <atomic>
#include <shared_mutex>

class EffectiveRWLock
{
 public:
  EffectiveRWLock() : reading_count_(0), all_write_count_(0), is_writing_(false) {}
  bool ReadLock()
  {
    do {
      if (all_write_count_.load() == 0) {
        ++ reading_count_;
        if (all_write_count_.load() == 0) {
          return true;
        } else if (-- reading_count_ == 0) {
          std::unique_lock<std::mutex> l(m_);
          cv_.notify_all();
        }
      }
    } while ([&cv = cv_, &m = m_, &all_write_count = all_write_count_] {
               std::unique_lock<std::mutex> l(m);
               cv.wait(l, [&all_write_count] { return all_write_count.load() == 0; });
               return true;
             }());
    return false;
  }

  bool WriteLock()
  {
    ++ all_write_count_;
    std::unique_lock<std::mutex> l(m_);
    cv_.wait(l, [&reading_count = reading_count_, &is_writing = is_writing_] {
                  return reading_count.load() == 0 && !is_writing;
                });
    is_writing_ = true;
    return true;
  }

  void Unlock()
  {
    if (is_writing_) {
      WriteUnlock_();
    } else {
      ReadUnlock_();
    }
  }

 private:
  void WriteUnlock_()
  {
    is_writing_ = false;
    -- all_write_count_;
    std::unique_lock<std::mutex> l(m_);
    cv_.notify_all();
  }

  void ReadUnlock_()
  {
    if (-- reading_count_ == 0) {
      std::unique_lock<std::mutex> l(m_);
      cv_.notify_all();
    }
  }

  std::mutex m_;
  std::condition_variable cv_;
  std::atomic<uint64_t> reading_count_;
  std::atomic<uint64_t> all_write_count_;
  bool is_writing_;
};

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
