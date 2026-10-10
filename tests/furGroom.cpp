#include "../src/vkh/systems/wolf/furGroom.hpp"

#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>

struct Vertex {
  glm::vec3 pos{}, normal{};
  glm::vec2 uv{};
  glm::uvec4 jointIndices{};
  glm::vec4 jointWeights{};
};
static_assert(sizeof(Vertex) == 64);
int main(int argc, char **argv) {
  std::vector<Vertex> vertices{
      {{0, 0, 0}, {0, 1, 0}, {.1, .2}, {2, 0, 0, 0}, {1, 0, 0, 0}},
      {{.02, 0, 0}, {0, 1, 0}, {.3, .2}, {3, 0, 0, 0}, {1, 0, 0, 0}},
      {{0, 0, .02}, {0, 1, 0}, {.1, .4}, {4, 0, 0, 0}, {1, 0, 0, 0}}};
  std::vector<uint32_t> indices{0, 1, 2};
  std::vector<glm::vec3> parameters(3, {.06f, 1, .5f});
  std::vector<glm::vec3> flow(3, {0, 0, -1});
  auto build = [&] {
    return vkh::buildSurfaceFurGroom<Vertex>(vertices, indices, parameters,
                                             flow);
  };
  auto groom = build();
  assert(groom.size() == 1);
  assert(std::abs(groom[0].parameters.x - 9.f) < .001f);
  for (size_t i = 0; i < 3; ++i) {
    assert(glm::distance(glm::vec3(groom[0].guides[i].position),
                         vertices[i].pos) == 0);
    assert(groom[0].guides[i].position.w == vertices[i].uv.y);
    assert(groom[0].guides[i].normal.w == vertices[i].uv.x);
    assert(groom[0].guides[i].joints.x == vertices[i].jointIndices.x);
    assert(groom[0].guides[i + 9].position.y > 0);
    assert(groom[0].guides[i + 9].position.z < vertices[i].pos.z);
  }
  for (auto &p : parameters)
    p.y = 0;
  assert(build().empty()); // eyes, nose, lips and claws never emit fur
  for (auto &p : parameters)
    p.y = 1;
  parameters[1].y = 0;
  assert(build().empty()); // mixed triangles cannot leak onto bare features
  parameters[1].y = 1;
  vertices[1].pos.x = .2f;
  vertices[2].pos.z = .2f;
  groom = build();
  assert(groom.size() > 1); // large triangles split without changing density
  float strands = 0;
  for (const auto &b : groom) {
    assert(b.parameters.x <= 64.f);
    strands += b.parameters.x;
    for (const auto &g : b.guides)
      assert(std::abs(glm::dot(g.weights, glm::vec4(1)) - 1) < 1e-5f);
  }
  assert(std::abs(strands - 900.f) < .01f);
  auto rejects = [&](auto mutation) {
    mutation();
    bool rejected = false;
    try {
      build();
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    assert(rejected);
  };
  rejects([&] { parameters[0].x = std::numeric_limits<float>::quiet_NaN(); });
  parameters[0].x = .06f;
  rejects([&] { indices[0] = 9; });
  if (argc == 2) {
    std::ifstream file(argv[1], std::ios::binary);
    uint32_t counts[2]{};
    file.read(reinterpret_cast<char *>(counts), sizeof(counts));
    assert(file && counts[0] && counts[1]);
    vertices.resize(counts[0]);
    indices.resize(counts[1]);
    parameters.resize(counts[0]);
    flow.resize(counts[0]);
    file.read(reinterpret_cast<char *>(vertices.data()),
              vertices.size() * sizeof(Vertex));
    file.read(reinterpret_cast<char *>(indices.data()),
              indices.size() * sizeof(uint32_t));
    file.read(reinterpret_cast<char *>(parameters.data()),
              parameters.size() * sizeof(glm::vec3));
    file.read(reinterpret_cast<char *>(flow.data()),
              flow.size() * sizeof(glm::vec3));
    assert(file);
    groom = build();
    assert(groom.size() > 1000);
    strands = 0;
    for (const auto &bundle : groom) {
      strands += bundle.parameters.x;
      for (const auto &guide : bundle.guides) {
        assert(std::abs(glm::dot(guide.weights, glm::vec4(1)) - 1) < .0001f);
        assert(glm::all(glm::lessThan(guide.joints, glm::uvec4(24))));
      }
    }
    assert(strands > 100000 && strands < 1000000);
    std::cout << "Wolf: " << groom.size() << " surface patches, " << strands
              << " strands\n";
  }
  std::cout << "Surface fur conversion checks passed\n";
}
