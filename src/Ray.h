/**
 * @file
 * @brief Historical CPU teaching implementation, retained for reference.
 *
 * The current application uses rt::SceneData and the shared TraceCore.inl kernel.
 */
#pragma once
#include "Vector3.h"

/**
 * @class Ray
 * @brief Ray starting at position A with direction B (not necessarily normalized).
 */
class Ray
{
public:
    Vector3 A, B;

    Ray() {}

    Ray(const Vector3 &a, const Vector3 &b)
    {
        A = a;
        B = b;
    }

    /** @brief Return the ray origin. */
    Vector3 Origin() const
    {
        return A;
    }

    /** @brief Return the stored, possibly non-unit direction. */
    Vector3 Direction() const
    {
        return B;
    }

    /** @brief Evaluate A + t*B; t measures distance only when B is a unit vector. */
    Vector3 PointAtParameter(float t) const
    {
        return A + t * B;
    }
};
