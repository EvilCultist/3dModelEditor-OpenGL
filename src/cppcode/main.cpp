#include <algorithm>
#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <iostream>
#include <ostream>
#include <stdio.h>
#define IMGUI_IMPL_OPENGL_ES3
#define IM_VEC2_CLASS_EXTRA
#include "ImGuiFileDialog.h"
#include "canvas.h"
#include "glad/gl.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#define GL_SILENCE_DEPRECATION
#include <GL/gl.h>
#include <GLFW/glfw3.h>

std::ostream &operator<<(std::ostream &out, ImVec2 point) {
  out << point.x << ", " << point.y;
  return out;
}

static void glfw_error_callback(int error, const char *description) {
  fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

struct Line {
  ImVec2 p1, p2;
};

int main(int, char **) {
  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit())
    return 1;

  const char *glsl_version = "#version 300 es";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);

  GLFWwindow *window = glfwCreateWindow(
      1280, 720, "teddy-impl — Experimental 3D Modeling", nullptr, nullptr);
  if (window == nullptr)
    return 1;
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  if (!gladLoaderLoadGL()) {
    std::cerr << "Failed to initialize GLAD" << std::endl;
    return 1;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

  ImGui::StyleColorsDark();

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  keyframes.push_back({0, 0, 0, {}});
  int n_points = 8;
  bool editing = true;
  float lerp_anim_t = 0.0f;
  float yaw = 0.0f, pitch = 0.0f, zoom = 1.5f;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
      ImGui_ImplGlfw_Sleep(10);
      continue;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    {
      // main view — full screen
      ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
      ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
      ImGui::Begin("canvas", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoBringToFrontOnFocus);
      if (editing) {
        RenderCanvas();
      } else {
        // orbit camera — drag to rotate, scroll to zoom
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
          yaw += io.MouseDelta.x * 0.005f;
          pitch += io.MouseDelta.y * 0.005f;
          pitch = glm::clamp(pitch, -1.5f, 1.5f);
        }
        if (ImGui::IsWindowHovered() && io.MouseWheel != 0.0f) {
          zoom *= (1.0f - io.MouseWheel * 0.1f);
          zoom = glm::clamp(zoom, 0.5f, 10.0f);
        }
        Render3D(yaw, pitch, zoom);
      }
      ImGui::End();
      ImGui::PopStyleVar(1);
    }

    {
      ImGui::Begin("controls");

      ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                  1000.0f / io.Framerate, io.Framerate);

      if (ImGui::Checkbox("Edit Mode", &editing)) {
        if (!editing) {
          // entering view mode — resample all to uniform point count
          uint64_t total = 0;
          for (auto &kf : keyframes)
            total += kf.points.size();
          long double n_p = (long double)total / keyframes.size();
          std::cout << "resampling to " << n_p << " points" << std::endl;
          for (int i = 0; i < keyframes.size(); i++)
            resample(i, n_p);
          buildMesh();
        }
      }

      if (editing) {
        if (ImGui::Button("Save Frame")) {
          std::rotate(keyframes[0].points.begin(),
                      keyframes[0].points.begin() +
                          keyframes[0].index_of_max_y,
                      keyframes[0].points.end());
          keyframes[0].index_of_max_y = 0;
          float newZ = keyframes[0].z - 0.5f;
          keyframes.insert(keyframes.begin(), {0, 0, 0, {}, newZ});
          markMeshDirty();
        }
        ImGui::SliderInt("n_points", &n_points, 4, 16);
        ImGui::Text("resample count: %lu", (long unsigned int)1 << n_points);
        if (ImGui::Button("Resample")) {
          std::rotate(keyframes[0].points.begin(),
                      keyframes[0].points.begin() +
                          keyframes[0].index_of_max_y,
                      keyframes[0].points.end());
          keyframes[0].index_of_max_y = 0;
          resample(0, 1 << n_points);
        }
      } else {
        // view-mode controls
        ImGui::SeparatorText("3D View");
        ImGui::SliderFloat("Rot Y", &yaw, -3.1416f, 3.1416f);
        ImGui::SliderFloat("Rot X", &pitch, -1.5f, 1.5f);
        ImGui::SliderFloat("Zoom", &zoom, 0.5f, 10.0f);

        ImGui::SeparatorText("Cross-section Z positions");
        for (int i = 0; i < (int)keyframes.size(); i++) {
          ImGui::PushID(i);
          std::string label = "Z[" + std::to_string(i) + "]";
          if (ImGui::InputFloat(label.c_str(), &keyframes[i].z, 0.1f, 0.5f, "%.3f")) {
            markMeshDirty();
          }
          ImGui::PopID();
        }

        ImGui::SeparatorText("Export");
        if (ImGui::Button("Export OBJ")) {
          IGFD::FileDialogConfig cfg;
          cfg.path = ".";
          cfg.countSelectionMax = 1;
          cfg.flags = ImGuiFileDialogFlags_ConfirmOverwrite;
          ImGuiFileDialog::Instance()->OpenDialog(
              "ExportOBJ", "Save OBJ File", ".obj", cfg);
        }
      }

      ImGui::End();
    }

    // file dialog handling
    if (ImGuiFileDialog::Instance()->Display("ExportOBJ")) {
      if (ImGuiFileDialog::Instance()->IsOk()) {
        std::string path = ImGuiFileDialog::Instance()->GetFilePathName();
        ExportOBJ(path.c_str());
      }
      ImGuiFileDialog::Instance()->Close();
    }

    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w,
                 clear_color.z * clear_color.w, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);

    if (ImGui::IsKeyDown(ImGuiKey_Escape))
      break;
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
