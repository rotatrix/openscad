#pragma once
#include <Eigen/Geometry>
#include <openaxis/geometry.hpp>
#include <openaxis/diagnostics.hpp>
#include <cmath>

namespace OpenAxisCamera {
constexpr double radians = 3.14159265358979323846 / 180.;
// Smallest centre depth, relative to the current distance, that write() keeps.
constexpr double min_depth = 1e-3;
inline openaxis::Vec3 vec(const Eigen::Vector3d& v)
{
  return {v.x(), v.y(), v.z()};
}
inline Eigen::Vector3d vec(openaxis::Vec3 v)
{
  return {v.x, v.y, v.z};
}
inline Eigen::Matrix3d rotation(const Eigen::Vector3d& angles)
{
  return (Eigen::AngleAxisd(angles.x() * radians, Eigen::Vector3d::UnitX()) *
          Eigen::AngleAxisd(angles.y() * radians, Eigen::Vector3d::UnitY()) *
          Eigen::AngleAxisd(angles.z() * radians, Eigen::Vector3d::UnitZ()))
    .toRotationMatrix();
}
// OpenGL's look-at frame for eye=(0,-distance,0), up=(0,0,1).
inline Eigen::Matrix3d base()
{
  Eigen::Matrix3d r;
  r << 1, 0, 0, 0, 0, 1, 0, -1, 0;
  return r;
}
inline openaxis::Pose read(const Eigen::Vector3d& angles, const Eigen::Vector3d& translation,
                           double distance, double fov, bool perspective)
{
  Eigen::Matrix3d world = rotation(angles).transpose() * base().transpose();
  auto q = openaxis::Quat::from_basis(vec(world.col(0)), vec(world.col(1)), vec(world.col(2)));
  openaxis::Pose p{vec(-translation + world.col(2) * distance), q.rotvec()};
  if (perspective) p.fov = fov * radians;
  else p.ortho_extent = 2 * distance * std::tan(fov * radians / 2);
  return p;
}
inline bool write(const openaxis::Pose& p, Eigen::Vector3d& angles, Eigen::Vector3d& translation,
                  double& distance, double& fov)
{
  if (!vec(p.t).allFinite() || !vec(p.r).allFinite() || !std::isfinite(p.fov) ||
      !std::isfinite(p.ortho_extent) || p.fov < 0 || p.ortho_extent < 0 ||
      ((p.fov > 0) == (p.ortho_extent > 0)))
    return false;
  auto q = openaxis::Quat::from_rotvec(p.r);
  Eigen::Matrix3d world;
  world.col(0) = vec(q.rotate({1, 0, 0}));
  world.col(1) = vec(q.rotate({0, 1, 0}));
  world.col(2) = vec(q.rotate({0, 0, 1}));
  // OpenSCAD orbits the mouse around -translation, placed `distance` in front of
  // the eye. Keep that centre at its previous depth on the new view axis so
  // Rotatrix dolly and orbit do not drag the native orbit centre with the eye.
  const Eigen::Vector3d eye = vec(p.t), back = world.col(2), centre = -translation;
  double next_fov = p.fov > 0 ? p.fov / radians : fov;
  double next_distance;
  Eigen::Vector3d target;
  if (p.fov > 0) {
    // A centre at or behind the eye cannot be kept; retain the previous distance.
    const double depth = (eye - centre).dot(back);
    next_distance = depth > min_depth * distance ? depth : distance;
    target = eye - back * next_distance;
  } else {
    // Orthographic distance sets the visible extent. The image does not depend
    // on eye depth, so the realized eye moves axially to keep the centre's depth.
    next_distance = p.ortho_extent / (2 * std::tan(next_fov * radians / 2));
    target = eye - back * (eye - centre).dot(back);
  }
  if (!std::isfinite(next_distance) || next_distance <= 0 || !std::isfinite(next_fov) || next_fov <= 0 ||
      next_fov >= 180 || !world.allFinite() || !target.allFinite())
    return false;
  angles = (base().transpose() * world.transpose()).eulerAngles(0, 1, 2) / radians;
  translation = -target;
  distance = next_distance;
  fov = next_fov;
  return true;
}
// Orthographic writes normalize eye depth (see write). Ignore that axial
// difference so it is not reported as a correction; view-plane movement,
// orientation and extent are still compared.
inline openaxis::PoseDifference compare(const openaxis::Pose& a, const openaxis::Pose& b)
{
  if (a.ortho_extent > 0 && b.ortho_extent > 0) {
    const auto back = vec(openaxis::Quat::from_rotvec(a.r).rotate({0, 0, 1}));
    openaxis::Pose aligned = a;
    aligned.t = vec(vec(a.t) + back * (vec(b.t) - vec(a.t)).dot(back));
    return openaxis::compare_poses(aligned, b);
  }
  return openaxis::compare_poses(a, b);
}
}  // namespace OpenAxisCamera