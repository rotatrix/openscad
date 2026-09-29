#pragma once
#include <Eigen/Geometry>
#include <openaxis/geometry.hpp>

namespace OpenAxisCamera {
constexpr double radians = 3.14159265358979323846 / 180.;
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
  double next_fov = p.fov > 0 ? p.fov / radians : fov;
  double next_distance = p.fov > 0 ? distance : p.ortho_extent / (2 * std::tan(next_fov * radians / 2));
  if (!std::isfinite(next_distance) || next_distance <= 0 || !std::isfinite(next_fov) || next_fov <= 0 ||
      next_fov >= 180 || !world.allFinite())
    return false;
  angles = (base().transpose() * world.transpose()).eulerAngles(0, 1, 2) / radians;
  translation = -(vec(p.t) - world.col(2) * next_distance);
  distance = next_distance;
  fov = next_fov;
  return true;
}
}  // namespace OpenAxisCamera