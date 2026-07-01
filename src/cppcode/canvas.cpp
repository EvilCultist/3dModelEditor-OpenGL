#include "canvas.h"
#include "glad/gl.h"
#include "shader.h"
#include <GL/gl.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/trigonometric.hpp>
#include <imgui.h>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

std::vector<keyframe> keyframes;

Shader *shdr = nullptr;

float canvasheight, canvaswidth;

struct MeshData {
  GLuint VAO = 0, VBO = 0, EBO = 0;
  GLsizei indexCountSurface = 0;
  GLsizei indexCountTopCap = 0;
  GLsizei indexCountBottomCap = 0;
  std::vector<glm::vec3> positions;
  std::vector<glm::vec3> normals;
  std::vector<unsigned int> indicesSurface;
  std::vector<unsigned int> indicesTopCap;
  std::vector<unsigned int> indicesBottomCap;
};
static MeshData g_mesh;
static bool g_meshDirty = true;

// FBO for offscreen 3D rendering
static GLuint g_fbo = 0, g_fboTex = 0, g_fboRBO = 0;
static int g_fboW = 0, g_fboH = 0;

static void ensureFBO(int w, int h) {
  if (g_fbo && g_fboW == w && g_fboH == h)
    return;
  glDeleteFramebuffers(1, &g_fbo);
  glDeleteTextures(1, &g_fboTex);
  glDeleteRenderbuffers(1, &g_fboRBO);
  g_fbo = g_fboTex = g_fboRBO = 0;

  glGenFramebuffers(1, &g_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);

  glGenTextures(1, &g_fboTex);
  glBindTexture(GL_TEXTURE_2D, g_fboTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE,
               NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         g_fboTex, 0);

  glGenRenderbuffers(1, &g_fboRBO);
  glBindRenderbuffer(GL_RENDERBUFFER, g_fboRBO);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                            GL_RENDERBUFFER, g_fboRBO);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    std::cerr << "FBO not complete!" << std::endl;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  g_fboW = w;
  g_fboH = h;
}

void markMeshDirty() { g_meshDirty = true; }

void RenderCanvasLerp(float t_raw) {
  ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
  ImVec2 canvas_size = ImGui::GetContentRegionAvail();
  canvasheight = canvas_size.y;
  canvaswidth = canvas_size.x;

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  int i = 0, j = 0;
  float t = t_raw;

  if (keyframes.size() == 2) {
    j = 1;
  }
  if (keyframes.size() > 2) {
    t_raw *= (keyframes.size() - 1);
    i = floor(t_raw);
    j = i + 1;
    t = t_raw - i;
  }

  std::vector<ImVec2> lerpresult;

  {
    // Lerp
    for (int index = 0; index < keyframes[i].points.size(); index++) {
      lerpresult.push_back(
          ImVec2((1 - t) * keyframes[i].points[index].x +
                     t * keyframes[j].points[index].x,
                 (1 - t) * keyframes[i].points[index].y +
                     t * keyframes[j].points[index].y));
    }
    lerpresult.push_back(lerpresult[0]);
  }

  {
    // render
    for (int index = 1; index < lerpresult.size(); index++) {
      draw_list->AddLine(ImVec2(canvas_pos.x + lerpresult[index - 1].x,
                                canvas_pos.y + lerpresult[index - 1].y),
                         ImVec2(canvas_pos.x + lerpresult[index].x,
                                canvas_pos.y + lerpresult[index].y),
                         IM_COL32(255, 255, 255, 255), 2.0f);
    }
  }
}

void RenderCanvas() {
  static bool is_drawing = false;

  ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
  ImVec2 canvas_size = ImGui::GetContentRegionAvail();
  if (canvas_size.x < 50.0f)
    canvas_size.x = 50.0f;
  if (canvas_size.y < 50.0f)
    canvas_size.y = 50.0f;

  ImDrawList *draw_list = ImGui::GetWindowDrawList();

  ImGui::InvisibleButton("canvas", canvas_size,
                         ImGuiButtonFlags_MouseButtonLeft |
                             ImGuiButtonFlags_MouseButtonRight);
  bool is_hovered = ImGui::IsItemHovered();
  bool is_active = ImGui::IsItemActive();
  ImVec2 mouse_pos = ImGui::GetIO().MousePos;
  ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;

  if (is_hovered && ImGui::IsMouseClicked(0)) {
    is_drawing = true;
    if (keyframes[0].points.size() > 0)
      keyframes[0].points.pop_back();
  }

  if (is_hovered && ImGui::IsMouseDown(1)) {
    keyframes[0].points.pop_back();
  }

  if (is_hovered && ImGui::IsKeyPressed(ImGuiKey_Tab)) {
    keyframes[0].points.clear();
  }

  if (is_drawing) {
    if (is_active && ImGui::IsMouseDown(0)) {
      point current_pos;
      current_pos.x = mouse_pos.x - canvas_pos.x;
      current_pos.y = mouse_pos.y - canvas_pos.y;
      if (current_pos.y > keyframes[0].max_y) {
        keyframes[0].max_y = current_pos.y;
        keyframes[0].index_of_max_y = keyframes[0].points.size();
      }
      keyframes[0].points.push_back(current_pos);
    } else {
      is_drawing = false;
    }
  }

  // ghost overlay of the last saved frame
  if (keyframes.size() > 1 && keyframes[1].points.size() > 1) {
    for (int i = 1; i < (int)keyframes[1].points.size(); i++) {
      draw_list->AddLine(
          ImVec2(canvas_pos.x + keyframes[1].points[i - 1].x,
                 canvas_pos.y + keyframes[1].points[i - 1].y),
          ImVec2(canvas_pos.x + keyframes[1].points[i].x,
                 canvas_pos.y + keyframes[1].points[i].y),
          IM_COL32(120, 120, 140, 80), 1.5f);
    }
    size_t last = keyframes[1].points.size() - 1;
    draw_list->AddLine(
        ImVec2(canvas_pos.x + keyframes[1].points[0].x,
               canvas_pos.y + keyframes[1].points[0].y),
        ImVec2(canvas_pos.x + keyframes[1].points[last].x,
               canvas_pos.y + keyframes[1].points[last].y),
        IM_COL32(120, 120, 140, 80), 1.5f);
  }

  {
    // render
    for (int i = 1; i < keyframes[0].points.size(); i++) {
      draw_list->AddLine(
          ImVec2(canvas_pos.x + keyframes[0].points[i - 1].x,
                 canvas_pos.y + keyframes[0].points[i - 1].y),
          ImVec2(canvas_pos.x + keyframes[0].points[i].x,
                 canvas_pos.y + keyframes[0].points[i].y),
          IM_COL32(255, 255, 255, 255), 2.0f);
    }
    if (keyframes[0].points.size() > 0) {
      size_t i = keyframes[0].points.size() - 1;
      draw_list->AddLine(ImVec2(canvas_pos.x + keyframes[0].points[0].x,
                                canvas_pos.y + keyframes[0].points[0].y),
                         ImVec2(canvas_pos.x + keyframes[0].points[i].x,
                                canvas_pos.y + keyframes[0].points[i].y),
                         IM_COL32(155, 0, 0, 255), 1.0f);
    }
  }
}

float distance(const point &first, const point &other) {
  return std::sqrt((first.x - other.x) * (first.x - other.x) +
                   (first.y - other.y) * (first.y - other.y));
}

float randomFloat(float min, float max) {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  std::uniform_real_distribution<float> dis(min, max);
  return dis(gen);
}

void resample(size_t kf_index, size_t n) {
  std::vector<point> &pt = keyframes[kf_index].points;
  if (pt.size() < 2 || n < 2)
    return;

  size_t pointCount = pt.size();
  std::vector<float> segmentLengths(pointCount);
  float totalLength = 0.0f;

  for (size_t i = 0; i < pointCount; ++i) {
    const point &a = pt[i];
    const point &b = pt[(i + 1) % pointCount];
    float len = distance(a, b);
    segmentLengths[i] = len;
    totalLength += len;
  }

  if (totalLength < 1e-5f) {
    pt.resize(n, pt[0]);
    return;
  }

  std::vector<point> nwPolyline;
  float targetSpacing = totalLength / n;
  float accumulated = 0.0f;
  size_t currSeg = 0;

  for (size_t i = 0; i < n; ++i) {
    float distAlong = i * targetSpacing;

    while (currSeg < segmentLengths.size() &&
           accumulated + segmentLengths[currSeg] < distAlong) {
      accumulated += segmentLengths[currSeg];
      currSeg++;
    }

    currSeg %= pointCount;
    float segStart = accumulated;
    float segLen = segmentLengths[currSeg];
    float t = segLen > 0 ? (distAlong - segStart) / segLen : 0.0f;

    const point &p0 = pt[currSeg];
    const point &p1 = pt[(currSeg + 1) % pointCount];

    point interp(p0.x + t * (p1.x - p0.x), p0.y + t * (p1.y - p0.y));

    nwPolyline.push_back(interp);
  }

  size_t index_of_max_y = 0;
  float max_y = 0;

  for (int i = 0; i < nwPolyline.size(); i++) {
    if (nwPolyline[i].y > max_y) {
      max_y = nwPolyline[i].y;
      index_of_max_y = i;
    }
  }

  std::rotate(nwPolyline.begin(), nwPolyline.begin() + index_of_max_y,
              nwPolyline.end());

  pt.swap(nwPolyline);
  g_meshDirty = true;
}

static glm::vec3 safeNormalize(const glm::vec3 &v) {
  float len = glm::length(v);
  if (len < 1e-8f)
    return glm::vec3(0.0f, 0.0f, 1.0f);
  return v / len;
}

static glm::vec3 keyframePos(size_t kf_idx, size_t pt_idx) {
  return glm::vec3(keyframes[kf_idx].points[pt_idx].x,
                   keyframes[kf_idx].points[pt_idx].y,
                   keyframes[kf_idx].z);
}

static glm::vec3 keyframeCentroid(size_t kf_idx) {
  size_t M = keyframes[kf_idx].points.size();
  glm::vec3 c(0.0f);
  for (auto &pt : keyframes[kf_idx].points) {
    c += glm::vec3(pt.x, pt.y, 0.0f);
  }
  c /= (float)M;
  c.z = keyframes[kf_idx].z;
  return c;
}

void buildMesh() {
  // Find M from the first viable keyframe (has >= 3 points)
  size_t M = 0;
  for (auto &kf : keyframes) {
    if (kf.points.size() >= 3) {
      M = kf.points.size();
      break;
    }
  }
  if (M < 3)
    return;

  // Normalize pixel coords to ~[-1, 1] so the camera sees the mesh
  float minX = FLT_MAX, maxX = -FLT_MAX;
  float minY = FLT_MAX, maxY = -FLT_MAX;
  for (auto &kf : keyframes) {
    for (auto &p : kf.points) {
      minX = std::min(minX, p.x);
      maxX = std::max(maxX, p.x);
      minY = std::min(minY, p.y);
      maxY = std::max(maxY, p.y);
    }
  }
  float cx = (minX + maxX) * 0.5f;
  float cy = (minY + maxY) * 0.5f;
  float range = std::max(maxX - minX, maxY - minY);
  float scale = range > 0.001f ? 2.0f / range : 1.0f;

  auto normPos = [&](size_t kf_idx, size_t pt_idx) -> glm::vec3 {
    return glm::vec3(
        (keyframes[kf_idx].points[pt_idx].x - cx) * scale,
        (keyframes[kf_idx].points[pt_idx].y - cy) * scale,
        keyframes[kf_idx].z);
  };

  auto normCentroid = [&](size_t kf_idx) -> glm::vec3 {
    size_t npts = keyframes[kf_idx].points.size();
    glm::vec3 c(0.0f);
    for (auto &p : keyframes[kf_idx].points) {
      c += glm::vec3((p.x - cx) * scale, (p.y - cy) * scale, 0.0f);
    }
    c /= (float)npts;
    c.z = keyframes[kf_idx].z;
    return c;
  };

  // Only use keyframes with enough points; sort by Z
  std::vector<size_t> order;
  for (size_t i = 0; i < keyframes.size(); i++)
    if (keyframes[i].points.size() >= M)
      order.push_back(i);
  std::sort(order.begin(), order.end(), [](size_t a, size_t b) {
    return keyframes[a].z < keyframes[b].z;
  });
  if (order.size() < 2)
    return;
  size_t N = order.size();

  // clean up old GL buffers
  if (g_mesh.VAO)
    glDeleteVertexArrays(1, &g_mesh.VAO);
  if (g_mesh.VBO)
    glDeleteBuffers(1, &g_mesh.VBO);
  if (g_mesh.EBO)
    glDeleteBuffers(1, &g_mesh.EBO);
  g_mesh = MeshData{};

  // --- compute surface vertex normals via face normal accumulation ---
  std::vector<glm::vec3> surfNormals(N * M, glm::vec3(0.0f));

  for (size_t ri = 0; ri < N - 1; ri++) {
    size_t i0 = order[ri];
    size_t i1 = order[ri + 1];
    for (size_t j = 0; j < M; j++) {
      size_t jn = (j + 1) % M;
      glm::vec3 p00 = normPos(i0, j);
      glm::vec3 p01 = normPos(i0, jn);
      glm::vec3 p10 = normPos(i1, j);
      glm::vec3 p11 = normPos(i1, jn);

      // tri 1: p00-p01-p10
      glm::vec3 n1 = safeNormalize(glm::cross(p01 - p00, p10 - p00));
      surfNormals[ri * M + j] += n1;
      surfNormals[ri * M + jn] += n1;
      surfNormals[(ri + 1) * M + j] += n1;

      // tri 2: p01-p11-p10
      glm::vec3 n2 = safeNormalize(glm::cross(p11 - p01, p10 - p01));
      surfNormals[ri * M + jn] += n2;
      surfNormals[(ri + 1) * M + jn] += n2;
      surfNormals[(ri + 1) * M + j] += n2;
    }
  }

  for (auto &n : surfNormals) {
    n = safeNormalize(n);
  }

  // --- build interleaved vertex buffer ---
  std::vector<float> verts;
  std::vector<unsigned int> idxSurface, idxTop, idxBot;

  auto pushVtx = [&](const glm::vec3 &pos, const glm::vec3 &nrm) -> unsigned int {
    unsigned int idx = (unsigned int)(verts.size() / 6);
    verts.push_back(pos.x);
    verts.push_back(pos.y);
    verts.push_back(pos.z);
    verts.push_back(nrm.x);
    verts.push_back(nrm.y);
    verts.push_back(nrm.z);
    return idx;
  };

  // 1. surface vertices (sorted by Z order)
  for (size_t ri = 0; ri < N; ri++) {
    size_t kfi = order[ri];
    for (size_t j = 0; j < M; j++) {
      pushVtx(normPos(kfi, j), surfNormals[ri * M + j]);
    }
  }

  // 2. surface indices (quad strip → triangles)
  for (size_t ri = 0; ri < N - 1; ri++) {
    for (size_t j = 0; j < M; j++) {
      size_t jn = (j + 1) % M;
      unsigned int i00 = (unsigned int)(ri * M + j);
      unsigned int i01 = (unsigned int)(ri * M + jn);
      unsigned int i10 = (unsigned int)((ri + 1) * M + j);
      unsigned int i11 = (unsigned int)((ri + 1) * M + jn);
      idxSurface.push_back(i00);
      idxSurface.push_back(i01);
      idxSurface.push_back(i10);
      idxSurface.push_back(i01);
      idxSurface.push_back(i11);
      idxSurface.push_back(i10);
    }
  }

  // 3. top cap (first keyframe in Z-sorted order)
  {
    size_t kfTop = order.front();
    glm::vec3 center = normCentroid(kfTop);
    unsigned int ci = pushVtx(center, glm::vec3(0.0f, 0.0f, 1.0f));
    unsigned int rimStart = (unsigned int)(verts.size() / 6);
    for (size_t j = 0; j < M; j++) {
      pushVtx(normPos(kfTop, j), glm::vec3(0.0f, 0.0f, 1.0f));
    }
    for (size_t j = 0; j < M; j++) {
      size_t jn = (j + 1) % M;
      idxTop.push_back(ci);
      idxTop.push_back(rimStart + j);
      idxTop.push_back(rimStart + jn);
    }
  }

  // 4. bottom cap (last keyframe in Z-sorted order)
  {
    size_t kfBot = order.back();
    glm::vec3 center = normCentroid(kfBot);
    unsigned int ci = pushVtx(center, glm::vec3(0.0f, 0.0f, -1.0f));
    unsigned int rimStart = (unsigned int)(verts.size() / 6);
    for (size_t j = 0; j < M; j++) {
      pushVtx(normPos(kfBot, j), glm::vec3(0.0f, 0.0f, -1.0f));
    }
    for (size_t j = 0; j < M; j++) {
      size_t jn = (j + 1) % M;
      // reversed winding for downward-facing cap
      idxBot.push_back(ci);
      idxBot.push_back(rimStart + jn);
      idxBot.push_back(rimStart + j);
    }
  }

  // --- upload to GPU ---
  glGenVertexArrays(1, &g_mesh.VAO);
  glGenBuffers(1, &g_mesh.VBO);
  glGenBuffers(1, &g_mesh.EBO);

  glBindVertexArray(g_mesh.VAO);

  glBindBuffer(GL_ARRAY_BUFFER, g_mesh.VBO);
  glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(),
               GL_STATIC_DRAW);

  // combined index buffer
  std::vector<unsigned int> allIdx;
  allIdx.insert(allIdx.end(), idxSurface.begin(), idxSurface.end());
  allIdx.insert(allIdx.end(), idxTop.begin(), idxTop.end());
  allIdx.insert(allIdx.end(), idxBot.begin(), idxBot.end());

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_mesh.EBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               allIdx.size() * sizeof(unsigned int), allIdx.data(),
               GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);

  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        (void *)(3 * sizeof(float)));

  glBindVertexArray(0);

  // store counts
  g_mesh.indexCountSurface = (GLsizei)idxSurface.size();
  g_mesh.indexCountTopCap = (GLsizei)idxTop.size();
  g_mesh.indexCountBottomCap = (GLsizei)idxBot.size();

  // store for OBJ export
  g_mesh.positions.clear();
  g_mesh.normals.clear();
  for (size_t i = 0; i < verts.size(); i += 6) {
    g_mesh.positions.push_back(
        glm::vec3(verts[i], verts[i + 1], verts[i + 2]));
    g_mesh.normals.push_back(
        glm::vec3(verts[i + 3], verts[i + 4], verts[i + 5]));
  }
  g_mesh.indicesSurface = idxSurface;
  g_mesh.indicesTopCap = idxTop;
  g_mesh.indicesBottomCap = idxBot;

  g_meshDirty = false;
}

void Render3D(float yaw, float pitch, float zoom) {
  if (g_meshDirty)
    buildMesh();
  if (!g_mesh.VAO)
    return;

  if (!shdr) {
    shdr = new Shader("../src/shaders/mesh.vert", "../src/shaders/mesh.frag");
  }

  ImVec2 csz = ImGui::GetContentRegionAvail();
  if (csz.x < 1.0f || csz.y < 1.0f)
    return;

  int w = (int)csz.x, h = (int)csz.y;
  ensureFBO(w, h);

  // render to offscreen FBO
  glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
  glViewport(0, 0, w, h);
  glEnable(GL_DEPTH_TEST);

  glClearColor(0.18f, 0.18f, 0.24f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  shdr->use();

  // fixed camera looking along +Z at distance zoom
  glm::vec3 eye(0.0f, 0.5f, zoom);
  glm::vec3 center(0.0f, 0.0f, 0.0f);

  glm::mat4 view = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));
  glm::mat4 proj =
      glm::perspective(glm::radians(45.0f), (float)w / (float)h, 0.1f, 100.0f);

  // model rotates in place, light stays fixed
  glm::mat4 model(1.0f);
  model = glm::rotate(model, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
  model = glm::rotate(model, pitch, glm::vec3(1.0f, 0.0f, 0.0f));
  glm::mat3 normMat = glm::transpose(glm::inverse(glm::mat3(model)));

  shdr->setMat4("model", model);
  shdr->setMat4("view", view);
  shdr->setMat4("proj", proj);
  shdr->setMat3("normalMatrix", normMat);

  // overhead light source, static relative to camera
  shdr->setVec3("uLightPos", 0.0f, 5.0f, 2.0f);
  shdr->setVec3("uViewPos", eye.x, eye.y, eye.z);
  shdr->setVec3("uLightColor", 1.0f, 1.0f, 1.0f);
  shdr->setVec3("uObjectColor", 0.85f, 0.60f, 0.40f);
  shdr->setFloat("uAmbientStrength", 0.35f);
  shdr->setFloat("uSpecularStrength", 0.4f);
  shdr->setFloat("uShininess", 48.0f);

  glBindVertexArray(g_mesh.VAO);

  glDrawElements(GL_TRIANGLES, g_mesh.indexCountSurface, GL_UNSIGNED_INT, 0);

  GLsizeiptr off = g_mesh.indexCountSurface * sizeof(unsigned int);
  glDrawElements(GL_TRIANGLES, g_mesh.indexCountTopCap, GL_UNSIGNED_INT,
                 (void *)off);

  off += g_mesh.indexCountTopCap * sizeof(unsigned int);
  glDrawElements(GL_TRIANGLES, g_mesh.indexCountBottomCap, GL_UNSIGNED_INT,
                 (void *)off);

  glDisable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // display FBO texture as ImGui image (V-flipped)
  ImGui::Image((ImTextureID)(uintptr_t)g_fboTex, csz, ImVec2(0, 1),
               ImVec2(1, 0));
}

static void writeOBJFaces(std::ofstream &file,
                          const std::vector<unsigned int> &indices) {
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    file << "f " << (indices[i] + 1) << "//" << (indices[i] + 1) << " "
         << (indices[i + 1] + 1) << "//" << (indices[i + 1] + 1) << " "
         << (indices[i + 2] + 1) << "//" << (indices[i + 2] + 1) << "\n";
  }
}

void ExportOBJ(const char *filename) {
  if (g_meshDirty)
    buildMesh();
  if (!g_mesh.VAO)
    return;

  std::ofstream file(filename);
  if (!file.is_open()) {
    std::cerr << "ExportOBJ: failed to open " << filename << std::endl;
    return;
  }

  file << "# teddy-impl export\n";

  for (auto &p : g_mesh.positions)
    file << "v " << p.x << " " << p.y << " " << p.z << "\n";
  for (auto &n : g_mesh.normals)
    file << "vn " << n.x << " " << n.y << " " << n.z << "\n";

  writeOBJFaces(file, g_mesh.indicesSurface);
  writeOBJFaces(file, g_mesh.indicesTopCap);
  writeOBJFaces(file, g_mesh.indicesBottomCap);

  file.close();
  std::cout << "Exported " << filename << std::endl;
}
