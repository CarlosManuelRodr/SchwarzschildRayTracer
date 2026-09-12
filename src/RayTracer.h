/**
 * @file
 * @brief Historical CPU teaching implementation, retained for reference.
 *
 * The current application uses rt::SceneData and the shared TraceCore.inl kernel.
 */
#pragma once
#include <cfloat>
#include "Sphere.h"
#include "HitableList.h"
#include "Camera.h"
#include "RandomGen.h"
#include "Material.h"

/**
 * @brief Historical recursive CPU tracer, returning a color after surface scattering.
 *
 * This retains the original emission normalization and Euler gravity for study;
 * use rt::renderCpu for validation against the current GPU implementation.
 * @param r Initial ray.
 * @param world Non-owning collection of scene geometry.
 * @param depth Current bounce count; primary rays start at zero and scattering stops at 50.
 */
Vector3 ray_trace(const Ray &r, HitableList world, int depth = 0)
{
    HitRecord rec;

    if (world.Hit(r, 0.001f, FLT_MAX, rec))
    {
        Ray scattered;
        Vector3 attenuation;
        Vector3 emitted = rec.matPtr->Emitted(rec.u, rec.v, rec.p);

        if (depth < 50 && rec.matPtr->Scatter(r, rec, attenuation, scattered))
        {
            return emitted + attenuation * ray_trace(scattered, world, depth + 1);
        }
        else
        {
            return unit_vector(emitted);
        }
    }
    else
    {
        Vector3 unitDirection = unit_vector(r.Direction());
        float t = 0.5f * (unitDirection.y() + 1.0f);

        return (1.0f - t) * Vector3(1.0f, 1.0f, 1.0f) + t * Vector3(0.5f, 0.7f, 1.0f);
    }
}
