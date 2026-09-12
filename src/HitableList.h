/**
 * @file
 * @brief Historical CPU teaching implementation, retained for reference.
 *
 * The current application uses rt::SceneData and the shared TraceCore.inl kernel.
 */
#pragma once
#include <vector>
#include "Hitable.h"

/**
 * @class HitableList
 * @brief Non-owning collection of ray-intersectable objects.
 */
class HitableList : public Hitable
{
public:
    std::vector<Hitable*> list;

    HitableList() {}

    HitableList(std::vector<Hitable*> l)
    {
        list = l;
    }

    /// @brief Return the closest intersection within the requested ray-parameter interval.
    /// @param r Input ray.
    /// @param tMin Exclusive lower ray-parameter bound.
    /// @param tMax Exclusive upper ray-parameter bound.
    /// @param rec Output intersection record, written only on a hit.
    virtual bool Hit(const Ray &r, float tMin, float tMax, HitRecord &rec) const;
};

bool HitableList::Hit(const Ray &r, float tMin, float tMax, HitRecord &rec) const
{
    HitRecord tempRec;
    bool hitAnything = false;
    float closestSoFar = tMax;

    for (unsigned i = 0; i < list.size(); i++)
    {
        if (list[i]->Hit(r, tMin, closestSoFar, tempRec))
        {
            hitAnything = true;
            closestSoFar = tempRec.t;
            rec = tempRec;
        }
    }

    return hitAnything;
}
