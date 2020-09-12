#pragma once
#include "Hitable.h"

void get_sphere_uv(const Vector3& p, float& u, float& v)
{
    auto phi = atan2(p.z(), p.x());
    auto theta = asin(p.y());
    u = 1 - (phi + pi) / (2 * pi);
    v = (theta + pi / 2) / pi;
}

class Sphere : public Hitable
{
public:
    Vector3 center;
    float radius;
    Material* matPtr;

    Sphere(Vector3 cen, float r, Material* m) : center(cen), radius(r), matPtr(m) {};
    virtual bool Hit(const Ray& r, float tMin, float tMax, HitRecord& rec) const;
};

bool Sphere::Hit(const Ray& r, float tMin, float tMax, HitRecord& rec) const
{
    Vector3 oc = r.Origin() - center;
    float a = dot(r.Direction(), r.Direction());
    float b = dot(oc, r.Direction());
    float c = dot(oc, oc) - radius * radius;
    float discriminant = b*b - a*c;

    if (discriminant > 0)
    {
        float temp = (-b - sqrt(b*b - a*c)) / a;
        if (temp < tMax && temp > tMin)
        {
            rec.t = temp;
            rec.p = r.PointAtParameter(rec.t);
            rec.normal = (rec.p - center) / radius;
            rec.matPtr = matPtr;
            rec.object = (Hittable*) this;
            get_sphere_uv((rec.p - center) / radius, rec.u, rec.v);
            return true;
        }

        temp = (-b + sqrt(b*b - a*c)) / a;
        if (temp < tMax && temp > tMin)
        {
            rec.t = temp;
            rec.p = r.PointAtParameter(rec.t);
            rec.normal = (rec.p - center) / radius;
            rec.matPtr = matPtr;
            rec.object = (Hittable*)this;
            get_sphere_uv((rec.p - center) / radius, rec.u, rec.v);
            return true;
        }
    }
    return false;
}