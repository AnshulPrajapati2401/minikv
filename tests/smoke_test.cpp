#include <gtest/gtest.h>
#include "minikv/db.h"

TEST(Smoke, BuildAndLink) {
  EXPECT_EQ(minikv::version(), 1);
}
