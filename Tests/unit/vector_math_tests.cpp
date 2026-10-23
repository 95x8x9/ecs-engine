#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "Boids/Common/vector_math.h"

namespace {

TEST(VectorMathTest, NormalizesFiniteNonzeroVectorsAcrossFloatExponents) {
  for (int exponent = -149; exponent <= 127; ++exponent) {
    SCOPED_TRACE(exponent);
    const float magnitude = std::ldexp(1.0F, exponent);
    const glm::vec3 value{magnitude, -magnitude, magnitude};
    const auto actual = boids::NormalizeNonzero(value);
    const glm::dvec3 expected = glm::normalize(glm::dvec3{value});
    for (int axis = 0; axis < 3; ++axis) {
      EXPECT_NEAR(actual[axis], expected[axis], 1e-6);
    }
  }
}

TEST(VectorMathTest, LimitsTinyVectorsWithoutLosingDirection) {
  const glm::vec3 value{3e-25F, -4e-25F, 0.0F};
  for (const auto actual : {boids::LimitMagnitude(value, 1e-30F),
                            boids::LimitSpeed(value, 1e-30F)}) {
    EXPECT_NEAR(actual.x / 1e-30F, 0.6F, 1e-6F);
    EXPECT_NEAR(actual.y / 1e-30F, -0.8F, 1e-6F);
    EXPECT_EQ(actual.z, 0.0F);
  }
}

TEST(VectorMathTest, PreservesTinyVectorsBelowLimitAndStopsAtZeroLimit) {
  const glm::vec3 value{3e-30F, -4e-30F, 0.0F};
  EXPECT_EQ(boids::LimitMagnitude(value, 1e-25F), value);
  EXPECT_EQ(boids::LimitSpeed(value, 1e-25F), value);
  EXPECT_EQ(boids::LimitMagnitude(value, 0.0F), glm::vec3{0.0F});
  EXPECT_EQ(boids::LimitSpeed(value, 0.0F), glm::vec3{0.0F});
}

TEST(VectorMathTest, RejectsNonfiniteArithmeticInsteadOfReturningNaNs) {
  for (const float invalid : {std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
    const glm::vec3 value{invalid, 0, 0};
    EXPECT_THROW(static_cast<void>(boids::NormalizeNonzero(value)),
                 std::overflow_error);
    for (const float maximum : {0.0F, 1.0F}) {
      EXPECT_THROW(static_cast<void>(boids::LimitMagnitude(value, maximum)),
                   std::overflow_error);
      EXPECT_THROW(static_cast<void>(boids::LimitSpeed(value, maximum)),
                   std::overflow_error);
    }
  }
}

TEST(VectorMathTest, MagnitudeLimitsMatchDoubleReferenceAcrossExponents) {
  for (int exponent = -120; exponent <= 120; exponent += 3) {
    const float magnitude = std::ldexp(1.0F, exponent);
    const glm::vec3 value{magnitude, -magnitude * 0.5F, magnitude * 0.25F};
    for (const float factor : {0.25F, 0.75F, 1.0F, 2.0F}) {
      SCOPED_TRACE(::testing::Message() << exponent << ", " << factor);
      const float maximum = magnitude * factor;
      glm::dvec3 expected{value};
      const double length = glm::length(expected);
      if (length > maximum) expected *= static_cast<double>(maximum) / length;
      for (const auto actual : {boids::LimitMagnitude(value, maximum),
                                boids::LimitSpeed(value, maximum)}) {
        for (int axis = 0; axis < 3; ++axis) {
          EXPECT_NEAR(static_cast<double>(actual[axis]) / magnitude,
                      expected[axis] / magnitude, 1e-6);
        }
      }
    }
  }
}

TEST(VectorMathTest, SubnormalLimitsDoNotRoundBackAboveTheLimit) {
  for (const float maximum : {std::numeric_limits<float>::denorm_min(),
                              std::numeric_limits<float>::denorm_min() * 2.0F,
                              std::numeric_limits<float>::min() * 0.5F}) {
    for (const auto value :
         {glm::vec3{maximum}, glm::vec3{1.0F}, glm::vec3{1e20F}}) {
      for (const auto actual : {boids::LimitMagnitude(value, maximum),
                                boids::LimitSpeed(value, maximum)}) {
        EXPECT_LE(glm::length(glm::dvec3{actual}),
                  static_cast<double>(maximum));
      }
    }
  }
}

TEST(VectorMathTest, RadiusChecksPreserveStrictCutoffAtExtremeScales) {
  for (const float radius : {1e-30F, 1.0F, 1e20F}) {
    for (const float factor : {0.0F, 0.5F, 1.0F, 2.0F}) {
      const glm::vec3 other{radius * factor, 0, 0};
      EXPECT_EQ(
          boids::WithinRadius(glm::dot(other, other), {0, 0, 0}, other, radius),
          factor < 1.0F);
    }
  }
}

TEST(VectorMathTest, PreservesSeparationWhenSquaredDistanceOverflows) {
  const glm::vec3 offset{3e20F, 4e20F, 0};
  const auto actual = boids::InverseSquareOffset(
      offset, std::numeric_limits<float>::infinity());
  EXPECT_NEAR(actual.x / 1e-21F, 1.2F, 1e-6F);
  EXPECT_NEAR(actual.y / 1e-21F, 1.6F, 1e-6F);
  EXPECT_EQ(actual.z, 0.0F);
}

}  // namespace
