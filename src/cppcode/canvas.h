#pragma once

#include "imgui.h"
#include <cstddef>
#include <vector>
#define GL_SILENCE_DEPRECATION
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

struct point {
  float x;
  float y;
};

struct keyframe {
  size_t index_of_max_y;
  float max_y;
  long double perimeter;
  std::vector<point> points;
  float z = 0.0f;
};

extern std::vector<keyframe> keyframes;

void buildMesh();
void markMeshDirty();
void RenderCanvas();
void resample(size_t kf_index, size_t n);
void RenderCanvasLerp(float t);
void Render3D(float yaw, float pitch, float zoom);
void ExportOBJ(const char *filename);
