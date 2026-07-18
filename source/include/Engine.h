#pragma once
#include "Object.h"
#include "Circle.h"
#include "Rectangle.h"
#include <cstddef>
#include <vector>

// Scene state
extern std::vector<Object> objects;

// Object management
Object& CreateObject(float x, float y, Circle shape);
Object& CreateObject(float x, float y, Rectangle shape);
void    RemoveObject(size_t i);

// Screen dimensions (available after Init)
float ScreenHalfWidth();
float ScreenHalfHeight();
