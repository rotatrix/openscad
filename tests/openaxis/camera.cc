#include "gui/OpenAxisCamera.h"
#include <iostream>
#include <limits>
#include <stdexcept>

void require(bool condition, const char *message)
{
  if (!condition) throw std::runtime_error(message);
}
int main()
{
  using namespace OpenAxisCamera;
  try {
    auto origin = read({0, 0, 0}, {0, 0, 0}, 100, 45, true);
    require(vec(origin.t).isApprox(Eigen::Vector3d(0, -100, 0)), "default eye convention");
    auto q = openaxis::Quat::from_rotvec(origin.r);
    require(vec(q.rotate({0, 0, -1})).isApprox(Eigen::Vector3d(0, 1, 0)), "default forward convention");
    require(vec(q.rotate({0, 1, 0})).isApprox(Eigen::Vector3d(0, 0, 1)), "Z-up convention");
    for (bool perspective : {false, true}) {
      for (Eigen::Vector3d source : {Eigen::Vector3d(55, 0, 25), Eigen::Vector3d(0, 90, 0),
                                     Eigen::Vector3d(180, -90, 270), Eigen::Vector3d(-15, 179, 355)}) {
        auto pose = read(source, {12, -31, 7}, 137, 37, perspective);
        Eigen::Vector3d angles, translation(12, -31, 7);
        double distance = 137, fov = 37;
        require(write(pose, angles, translation, distance, fov), "valid pose rejected");
        auto realized = read(angles, translation, distance, fov, perspective);
        require(vec(realized.t).isApprox(vec(pose.t), 1e-9), "eye drift on round trip");
        auto expected = openaxis::Quat::from_rotvec(pose.r);
        auto actual = openaxis::Quat::from_rotvec(realized.r);
        for (auto axis : {openaxis::Vec3{1, 0, 0}, openaxis::Vec3{0, 1, 0}, openaxis::Vec3{0, 0, 1}})
          require(vec(expected.rotate(axis)).isApprox(vec(actual.rotate(axis)), 1e-9),
                  "orientation drift");
        require(std::abs(realized.fov - pose.fov) < 1e-9 &&
                  std::abs(realized.ortho_extent - pose.ortho_extent) < 1e-9,
                "projection drift");
      }
    }
    {
      // A perspective dolly keeps the native orbit centre and changes distance.
      const Eigen::Vector3d angles0(55, 0, 25), translation0(12, -31, 7);
      auto pose = read(angles0, translation0, 137, 37, true);
      const auto q = openaxis::Quat::from_rotvec(pose.r);
      const Eigen::Vector3d back = vec(q.rotate({0, 0, 1}));
      pose.t = vec(vec(pose.t) - back * 30);
      Eigen::Vector3d angles = angles0, translation = translation0;
      double distance = 137, fov = 37;
      require(write(pose, angles, translation, distance, fov), "dolly rejected");
      require(translation.isApprox(translation0, 1e-9), "dolly moved orbit centre");
      require(std::abs(distance - 107) < 1e-9, "dolly distance");
      require(vec(read(angles, translation, distance, fov, true).t).isApprox(vec(pose.t), 1e-9),
              "dolly eye");
      // Rotating the view about another point keeps the centre's depth on the new axis.
      auto turned = read({70, 10, 40}, {0, 0, 0}, 150, 37, true);
      angles = angles0, translation = translation0, distance = 137;
      require(write(turned, angles, translation, distance, fov), "turn rejected");
      const Eigen::Vector3d turned_back =
        vec(openaxis::Quat::from_rotvec(turned.r).rotate({0, 0, 1}));
      require(std::abs((vec(turned.t) + translation).dot(turned_back) -
                       (vec(turned.t) + translation0).dot(turned_back)) < 1e-9,
              "turn changed centre depth");
      require(vec(read(angles, translation, distance, fov, true).t).isApprox(vec(turned.t), 1e-9),
              "turn eye");
      // Passing the centre falls back to the previous distance.
      auto past = read(angles0, translation0, 137, 37, true);
      past.t = vec(vec(past.t) - back * 200);
      angles = angles0, translation = translation0, distance = 137;
      require(write(past, angles, translation, distance, fov), "dolly past centre rejected");
      require(std::abs(distance - 137) < 1e-9, "past-centre distance");
    }
    {
      // Orthographic writes keep the centre's depth; comparison ignores the
      // resulting axial eye difference but not view-plane movement.
      const Eigen::Vector3d angles0(55, 0, 25), translation0(12, -31, 7);
      auto pose = read(angles0, translation0, 137, 37, false);
      const Eigen::Vector3d back = vec(openaxis::Quat::from_rotvec(pose.r).rotate({0, 0, 1}));
      pose.t = vec(vec(pose.t) + back * 500);
      pose.ortho_extent *= 0.5;
      Eigen::Vector3d angles = angles0, translation = translation0;
      double distance = 137, fov = 37;
      require(write(pose, angles, translation, distance, fov), "ortho rejected");
      require(translation.isApprox(translation0, 1e-9), "ortho moved orbit centre");
      require(std::abs(distance - 68.5) < 1e-9, "ortho extent");
      const auto realized = read(angles, translation, distance, fov, false);
      require(!compare(realized, pose).changed, "axial ortho normalization reported");
      auto shifted = pose;
      shifted.t = vec(vec(pose.t) + vec(openaxis::Quat::from_rotvec(pose.r).rotate({1, 0, 0})));
      require(compare(realized, shifted).changed, "ortho view-plane movement ignored");
    }
    for (int fault = 0; fault < 4; ++fault) {
      auto invalid = origin;
      if (fault == 0) invalid.r.x = std::numeric_limits<double>::quiet_NaN();
      if (fault == 1) invalid.ortho_extent = 50;
      if (fault == 2) invalid.fov = 4;
      if (fault == 3) invalid.fov = 0;
      Eigen::Vector3d angles(1, 2, 3), translation(4, 5, 6);
      double distance = 100, fov = 45;
      require(!write(invalid, angles, translation, distance, fov), "invalid pose accepted");
      require(angles == Eigen::Vector3d(1, 2, 3) && translation == Eigen::Vector3d(4, 5, 6) &&
                distance == 100 && fov == 45,
              "failed write changed camera");
    }
    std::cout << "OpenAxis camera checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
