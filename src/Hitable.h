/**
 * @file
 * @brief Historical CPU teaching implementation, retained for reference.
 *
 * The current application uses rt::SceneData and the shared TraceCore.inl kernel.
 */
#pragma once
#include "Ray.h"

class Material;
class Hittable;

/**
 * @struct HitRecord
 * @brief Intersection result containing position, surface normal, material, and texture coordinates.
 */
struct HitRecord
{
    Vector3 p;        ///< World-space intersection position.
    Vector3 normal;   ///< Outward surface normal.
    Material* matPtr; ///< Borrowed pointer to the surface material.
    Hittable* object; ///< Borrowed pointer to the intersected object.
    float t;          ///< Ray parameter at the intersection; not necessarily a distance.
    float u;          ///< Horizontal texture coordinate at the intersection.
    float v;          ///< Vertical texture coordinate at the intersection.
};

/**
 * @class Hitable
 * @brief Abstract interface for ray-intersectable geometry.
 */
class Hitable
{
public:
    virtual ~Hitable() = default;

    /** @brief Find an intersection strictly between tMin and tMax; write rec only on success. */
    virtual bool Hit(const Ray &r, float tMin, float tMax, HitRecord &rec) const = 0;
};
