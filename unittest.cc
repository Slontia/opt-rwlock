#include "lock.h"
#include <future>
#include <gtest/gtest.h>

class TestRWLock : public ::testing::Test
{
 public:
  TestRWLock() {}
  virtual ~TestRWLock() {}
  virtual void SetUp() {}
  virtual void TearDown() {}
};

TEST_F(TestRWLock, ww_conflict1)
{
  opt::RWLock l;
  ASSERT_TRUE(l.WriteLock(std::chrono::milliseconds(1)));
  ASSERT_FALSE(l.WriteLock(std::chrono::milliseconds(1)));
}

TEST_F(TestRWLock, ww_conflict2)
{
  opt::RWLock l;
  ASSERT_TRUE(l.WriteLock(std::try_to_lock));
  ASSERT_FALSE(l.WriteLock(std::try_to_lock));
}

TEST_F(TestRWLock, rw_conflict1)
{
  opt::RWLock l;
  ASSERT_TRUE(l.ReadLock(std::chrono::milliseconds(1)));
  ASSERT_FALSE(l.WriteLock(std::chrono::milliseconds(1)));
  ASSERT_TRUE(l.ReadLock(std::chrono::milliseconds(1)));
}

TEST_F(TestRWLock, rw_conflict2)
{
  opt::RWLock l;
  ASSERT_TRUE(l.ReadLock(std::try_to_lock));
  ASSERT_FALSE(l.WriteLock(std::try_to_lock));
  ASSERT_TRUE(l.ReadLock(std::try_to_lock));
}

TEST_F(TestRWLock, write_priority1)
{
  opt::RWLock l;
  ASSERT_TRUE(l.ReadLock(std::chrono::milliseconds(1)));
  auto fut1 = std::async(std::launch::async, [&l] { ASSERT_FALSE(l.WriteLock(std::chrono::milliseconds(3))); });
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  ASSERT_FALSE(l.ReadLock(std::chrono::milliseconds(1)));
}

TEST_F(TestRWLock, write_priority2)
{
  opt::RWLock l;
  ASSERT_TRUE(l.ReadLock(std::try_to_lock));
  auto fut1 = std::async(std::launch::async, [&l] { ASSERT_FALSE(l.WriteLock(std::chrono::milliseconds(2))); });
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  ASSERT_FALSE(l.ReadLock(std::try_to_lock));
}

TEST_F(TestRWLock, r_unlock)
{
  opt::RWLock l;
  for (int i = 0; i < 5; ++ i) {
      ASSERT_TRUE(l.ReadLock(std::try_to_lock));
    }
  for (int i = 0; i < 5; ++ i) {
      ASSERT_FALSE(l.WriteLock(std::try_to_lock));
      ASSERT_TRUE(l.Unlock());
    }
  ASSERT_FALSE(l.Unlock());
  ASSERT_TRUE(l.WriteLock(std::try_to_lock));
}

TEST_F(TestRWLock, w_unlock)
{
  std::atomic<uint32_t> count(0);
  opt::RWLock l;
  ASSERT_TRUE(l.WriteLock(std::try_to_lock));
  auto fu1 = std::async(std::launch::async, [&l, &count] {
          ASSERT_TRUE(l.WriteLock(std::chrono::milliseconds(2)));
              ++ count;
                });
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  ASSERT_EQ(count, 0);
  ASSERT_TRUE(l.Unlock());
  fu1.wait();

  std::array<std::future<void>, 5> futs;
  for (auto& fut : futs) {
      fut = std::async(std::launch::async, [&l, &count] {
                ASSERT_TRUE(l.ReadLock(std::chrono::milliseconds(2)));
                      ++ count;
                          });
    }
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  ASSERT_EQ(count, 1);
  ASSERT_TRUE(l.Unlock());
  for (auto& fut : futs) {
      fut.wait();
    }
  ASSERT_EQ(count, 6);
  for (auto& fut : futs) {
      ASSERT_TRUE(l.Unlock());
    }
  ASSERT_FALSE(l.Unlock());
}

int main(int argc, char **argv)
{
  int ret = -1;
  testing::InitGoogleTest(&argc, argv);
  ret = RUN_ALL_TESTS();
  return ret;
}

