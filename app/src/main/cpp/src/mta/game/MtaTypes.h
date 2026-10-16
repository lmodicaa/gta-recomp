#pragma once

#include <cmath>
#include <cstdint>

struct CVector {
    float x, y, z;

    CVector() : x(0.0f), y(0.0f), z(0.0f) {}
    CVector(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

    float Length() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    float LengthSquared() const {
        return x * x + y * y + z * z;
    }

    void Normalize() {
        float len = Length();
        if (len > 0.0001f) {
            x /= len;
            y /= len;
            z /= len;
        }
    }
};

struct CVector2D {
    float x, y;
    CVector2D() : x(0.0f), y(0.0f) {}
    CVector2D(float _x, float _y) : x(_x), y(_y) {}
};

struct CQuaternion {
    float x, y, z, w;
    CQuaternion() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
    CQuaternion(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};
