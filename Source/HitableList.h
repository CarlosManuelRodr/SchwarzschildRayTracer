#pragma once
#include <vector>
#include "Hitable.h"

class HitableList : public Hitable
{
public:
    std::vector<Hitable*> list;

    HitableList() {}
    HitableList(std::vector<Hitable*> l)
    {
        list = l;
    }

    virtual bool Hit(const Ray& r, float tMin, float tMax, HitRecord& rec) const;
};

bool HitableList::Hit(const Ray& r, float tMin, float tMax, HitRecord& rec) const
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