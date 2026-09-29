#include "gui/OpenAxisCamera.h"
#include <iostream>
#include <limits>
#include <stdexcept>

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
int main() {
  using namespace OpenAxisCamera;
  try {
    auto origin = read({0,0,0}, {0,0,0}, 100, 45, true);
    require(vec(origin.t).isApprox(Eigen::Vector3d(0,-100,0)), "default eye convention");
    auto q = openaxis::Quat::from_rotvec(origin.r);
    require(vec(q.rotate({0,0,-1})).isApprox(Eigen::Vector3d(0,1,0)), "default forward convention");
    require(vec(q.rotate({0,1,0})).isApprox(Eigen::Vector3d(0,0,1)), "Z-up convention");
    for (bool perspective : {false, true}) {
      for (Eigen::Vector3d source : {Eigen::Vector3d(55,0,25), Eigen::Vector3d(0,90,0),
                                    Eigen::Vector3d(180,-90,270), Eigen::Vector3d(-15,179,355)}) {
        auto pose = read(source, {12,-31,7}, 137, 37, perspective);
        Eigen::Vector3d angles, translation;
        double distance = 137, fov = 37;
        require(write(pose, angles, translation, distance, fov), "valid pose rejected");
        auto realized = read(angles, translation, distance, fov, perspective);
        require(vec(realized.t).isApprox(vec(pose.t), 1e-9), "eye drift on round trip");
        auto expected = openaxis::Quat::from_rotvec(pose.r);
        auto actual = openaxis::Quat::from_rotvec(realized.r);
        for (auto axis : {openaxis::Vec3{1,0,0}, openaxis::Vec3{0,1,0}, openaxis::Vec3{0,0,1}})
          require(vec(expected.rotate(axis)).isApprox(vec(actual.rotate(axis)), 1e-9), "orientation drift");
        require(std::abs(realized.fov-pose.fov) < 1e-9 &&
                std::abs(realized.ortho_extent-pose.ortho_extent) < 1e-9, "projection drift");
      }
    }
    for (int fault = 0; fault < 4; ++fault) {
      auto invalid = origin;
      if (fault == 0) invalid.r.x = std::numeric_limits<double>::quiet_NaN();
      if (fault == 1) invalid.ortho_extent = 50;
      if (fault == 2) invalid.fov = 4;
      if (fault == 3) invalid.fov = 0;
      Eigen::Vector3d angles(1,2,3), translation(4,5,6);
      double distance = 100, fov = 45;
      require(!write(invalid, angles, translation, distance, fov), "invalid pose accepted");
      require(angles == Eigen::Vector3d(1,2,3) && translation == Eigen::Vector3d(4,5,6) &&
              distance == 100 && fov == 45, "failed write changed camera");
    }
    std::cout << "OpenAxis camera checks passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n'; return 1;
  }
}
