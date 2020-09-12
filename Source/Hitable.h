#pragma once
#include "Ray.h"

class Material;
class Hittable;

struct HitRecord
{
    Vector3 p;          // Position where collision occurs
    Vector3 normal;     // Normal of the collided surface
    Material* matPtr;   // Pointer to material of the collided object
    Hittable* object;   // Pointer to the collided object
    float t;            // Parameter where collision ocurred
    float u;            // Texture U coordinate of the collision
    float v;            // Texture V coordinate of the collision
};

class Hitable
{
public:
    virtual bool Hit(const Ray& r, float tMin, float tMax, HitRecord& rec) const = 0;
};