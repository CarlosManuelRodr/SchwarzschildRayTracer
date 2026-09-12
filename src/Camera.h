/**
 * @file
 * @brief Historical CPU teaching implementation, retained for reference.
 *
 * The current application uses rt::SceneData and the shared TraceCore.inl kernel.
 */
#pragma once
#include "Ray.h"

/**
 * @class Camera
 * @brief Pinhole camera with configurable position and view direction.
 *
 * Generates the primary rays traced for each image pixel.
 */
class Camera
{
private:
    Vector3 origin;
    Vector3 lowerLeftCorner;
    Vector3 horizontal;
    Vector3 vertical;

    Vector3 m_lookFrom, m_lookAt, m_vUp;
    float m_vfov, m_aspect;

    void SetUp()
    {
        Vector3 u, v, w;
        float theta = m_vfov * pi / 180.0f;
        float halfHeight = tan(theta / 2.0f);
        float halfWidth = m_aspect * halfHeight;
        origin = m_lookFrom;

        w = unit_vector(m_lookFrom - m_lookAt);
        u = unit_vector(cross(m_vUp, w));
        v = cross(w, u);
        lowerLeftCorner = origin - halfWidth * u - halfHeight * v - w;
        horizontal = 2.0f * halfWidth * u;
        vertical = 2.0f * halfHeight * v;
    }

public:
    /** @brief Construct the image plane from a view pose, vertical FOV in degrees, and width/height aspect.
     */
    Camera(Vector3 lookFrom, Vector3 lookAt, Vector3 vUp, float vfov, float aspect)
    {
        m_lookFrom = lookFrom;
        m_lookAt = lookAt;
        m_vUp = vUp;
        m_vfov = vfov;
        m_aspect = aspect;

        SetUp();
    }

    /** @brief Move the ray origin and rebuild the image plane. */
    void SetLookFrom(Vector3 pos)
    {
        m_lookFrom = pos;
        SetUp();
    }

    /** @brief Change the target point and rebuild the image plane. */
    void SetLookAt(Vector3 pos)
    {
        m_lookAt = pos;
        SetUp();
    }

    /** @brief Return the unnormalized ray through image coordinates (u,v), measured from the lower left. */
    Ray GetRay(float u, float v) const
    {
        return Ray(origin, lowerLeftCorner + u * horizontal + v * vertical - origin);
    }
};
