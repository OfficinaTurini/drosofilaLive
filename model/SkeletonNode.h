#pragma once

struct SkeletonNode
{
    float x;
    float y;
    float z;
    int parent;

    SkeletonNode()
        : x(0.0f), y(0.0f), z(0.0f), parent(-1)
    {
    }

    SkeletonNode(float xx, float yy, float zz, int pp)
        : x(xx), y(yy), z(zz), parent(pp)
    {
    }
};
