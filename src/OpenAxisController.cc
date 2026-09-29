#include "QGLView.h"
#include "OpenAxisController.h"
#include <QApplication>
#include <QAction>
#include <QCursor>
#include <QPainter>
#include <QTimer>
#include <openaxis/navigation.hpp>
#include <openaxis/connection_manager.hpp>
#include <openaxis/logging.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double radians = 3.14159265358979323846 / 180.;
openaxis::Vec3 vec(const Eigen::Vector3d &v) { return {v.x(), v.y(), v.z()}; }
Eigen::Vector3d vec(openaxis::Vec3 v) { return {v.x, v.y, v.z}; }
Eigen::Matrix3d rotation(const Camera &c) {
  return (Eigen::AngleAxisd(c.object_rot.x() * radians, Eigen::Vector3d::UnitX()) *
          Eigen::AngleAxisd(c.object_rot.y() * radians, Eigen::Vector3d::UnitY()) *
          Eigen::AngleAxisd(c.object_rot.z() * radians, Eigen::Vector3d::UnitZ())).toRotationMatrix();
}
// OpenGL's look-at frame for eye=(0,-distance,0), up=(0,0,1).
Eigen::Matrix3d base() {
  Eigen::Matrix3d r;
  r << 1,0,0, 0,0,1, 0,-1,0;
  return r;
}
class Scheduler : public QObject, public openaxis::Scheduler {
public:
  std::function<void()> before;
  void post(Callback f) override {
    QTimer::singleShot(0, this, [this, f = std::move(f)] { if (before) before(); f(); });
  }
  void post_at(double deadline, Callback f) override {
    QTimer::singleShot(0, this, [this, deadline, f = std::move(f)] {
      double delay = std::ceil((deadline - openaxis::diagnostic_time()) * 1000);
      QTimer::singleShot(int(std::clamp(delay, 0., double(std::numeric_limits<int>::max()))),
                        this, [this, f] { if (before) before(); f(); });
    });
  }
};
openaxis::OpenAxisClientOptions client_options(Scheduler &scheduler) {
  openaxis::DiagnosticLog::configure("openscad");
  openaxis::OpenAxisClientOptions o;
  o.client_name = "OpenSCAD";
  o.scheduler = &scheduler;
  o.target = {{"pid", openaxis::current_process_id()}};
  return o;
}
}
struct OpenAxisController::Impl : openaxis::NavigationAdapter {
  QGLView &view;
  Scheduler scheduler;
  openaxis::OpenAxisClient client;
  openaxis::NavigationDiagnostics diagnostics;
  openaxis::NavigationSession session;
  openaxis::OpenAxisConnectionManager connection;
  bool focused = false, enabled = true, visible = false;
  std::string context;
  Eigen::Matrix3d last_rotation = Eigen::Matrix3d::Identity();
  Eigen::Vector3d last_translation = Eigen::Vector3d::Zero();
  double last_distance = 0, last_fov = 0;
  Camera::ProjectionType last_projection = Camera::ProjectionType::PERSPECTIVE;
  std::optional<openaxis::Vec3> pivot;
  std::optional<double> expiry;

  explicit Impl(QGLView &v) : view(v), client(client_options(scheduler)),
    session(client, *this, &diagnostics, [this] {
      openaxis::NavigationOptions o;
      o.scheduler = &scheduler;
      o.observation = [this](const openaxis::NavigationContext &c) -> std::optional<openaxis::Pose> {
        return is_current(c) ? std::optional<openaxis::Pose>(pose()) : std::nullopt;
      };
      return o;
    }()), connection(client, {[this] {
      openaxis::ConnectionMetadata m;
      m.tags = {"app.openscad", "workspace.modeling"};
      m.capabilities = {"navigation"};
      m.focused = focused;
      return m;
    }}) {
    scheduler.before = [this] { refresh(); };
    diagnostics.on_changed = [this] { if (visible) view.update(); };
    connection.on_state = [this](const openaxis::ConnectionStatus &) { if (visible) view.update(); };
    connection.start();
  }
  ~Impl() { scheduler.before = {}; connection.stop(); session.close(); }
  bool available() const {
    return enabled && view.isVisible() && view.width() > 0 && view.height() > 0 && view.getRenderer();
  }
  std::string key() const {
    return std::to_string(reinterpret_cast<std::uintptr_t>(view.getRenderer())) + "/" +
      std::to_string(view.sceneRevision) + "/" + std::to_string(view.width()) + "/" + std::to_string(view.height()) + "/" +
      std::to_string(view.devicePixelRatioF());
  }
  void remember() {
    last_rotation = rotation(view.cam); last_translation = view.cam.object_trans;
    last_distance = view.cam.zoomValue(); last_fov = view.cam.fov;
    last_projection = view.cam.projection;
  }
  void refresh() {
    bool next = available() && view.window()->isActiveWindow() && !QApplication::activeModalWidget();
    const std::string next_context = available() ? key() : "";
    if (next_context != context || (focused && !next)) {
      session.cancel("viewport_changed"); diagnostics.clear(); pivot.reset();
    }
    context = next_context;
    if (next != focused) { focused = next; connection.refresh_metadata(); }
    bool changed = !last_rotation.isApprox(rotation(view.cam)) ||
      !last_translation.isApprox(view.cam.object_trans) || last_distance != view.cam.zoomValue() ||
      last_fov != view.cam.fov || last_projection != view.cam.projection;
    remember();
    diagnostics.set_enabled(visible && available());
    diagnostics.set_context(context);
    if (changed && focused) session.native_camera_changed();
  }
  openaxis::Pose pose() const {
    const auto &c = view.cam;
    Eigen::Matrix3d world = rotation(c).transpose() * base().transpose();
    auto q = openaxis::Quat::from_basis(vec(world.col(0)), vec(world.col(1)), vec(world.col(2)));
    Eigen::Vector3d eye = -c.object_trans + world.col(2) * c.zoomValue();
    openaxis::Pose p{vec(eye), q.rotvec()};
    if (c.projection == Camera::ProjectionType::PERSPECTIVE) p.fov = c.fov * radians;
    else p.ortho_extent = 2 * c.zoomValue() * std::tan(c.fov * radians / 2);
    return p;
  }
  openaxis::NavigationContext capture_context() override {
    refresh();
    if (!focused) return {};
    return context;
  }
  bool is_current(const openaxis::NavigationContext &c) override {
    auto k = std::any_cast<std::string>(&c);
    return k && focused && available() && view.window()->isActiveWindow() &&
      !QApplication::activeModalWidget() && *k == key();
  }
  struct Capture : openaxis::NavigationCapture {
    Impl &owner;
    openaxis::NavigationContext context;
    openaxis::Pose initial;
    Capture(Impl &o, const openaxis::NavigationContext &c) : owner(o), context(c), initial(o.pose()) {}
    std::optional<openaxis::Pose> initial_observation() override { return initial; }
    openaxis::Value resolve(const std::string &name) override {
      if (!owner.is_current(context)) return nullptr;
      return name == "camera.pose" ? openaxis::pose_value(initial) : owner.fact(name);
    }
  };
  std::unique_ptr<openaxis::NavigationCapture> begin_query(const openaxis::NavigationContext &c) override {
    if (!is_current(c)) return {};
    return std::make_unique<Capture>(*this, c);
  }
  openaxis::WriteResult apply_pose(const openaxis::NavigationContext &c, const openaxis::NavigationPose &p,
                                  const openaxis::Value &, std::optional<openaxis::Vec3>) override {
    if (!is_current(c)) return {};
    auto q = openaxis::Quat::from_rotvec(p.r);
    Eigen::Matrix3d world;
    world.col(0) = vec(q.rotate({1,0,0})); world.col(1) = vec(q.rotate({0,1,0})); world.col(2) = vec(q.rotate({0,0,1}));
    double distance = view.cam.zoomValue();
    double fov = view.cam.fov;
    if (p.fov > 0) fov = p.fov / radians;
    else distance = p.ortho_extent / (2 * std::tan(fov * radians / 2));
    if (!std::isfinite(distance) || distance <= 0 || !std::isfinite(fov) || fov <= 0 || fov >= 180 ||
        !vec(p.t).allFinite() || !world.allFinite()) return {};
    view.cam.object_rot = (base().transpose() * world.transpose()).eulerAngles(0,1,2) / radians;
    view.cam.object_trans = -(vec(p.t) - world.col(2) * distance);
    view.cam.setVpd(distance); view.cam.setVpf(fov);
    view.cam.setProjection(p.fov > 0 ? Camera::ProjectionType::PERSPECTIVE : Camera::ProjectionType::ORTHOGONAL);
    remember(); view.update();
    return {true, pose()};
  }
  void show_pivot(const openaxis::NavigationContext &c, std::optional<openaxis::Vec3> p) override {
    if (!p || is_current(c)) { pivot = p; view.update(); }
  }
  openaxis::Value fact(const std::string &name) {
    using openaxis::vector_value;
    if (name == "document.id") return std::to_string(reinterpret_cast<std::uintptr_t>(view.getRenderer()));
    if (name == "world.orientation") return {{"forward", {0,1,0}}, {"up", {0,0,1}}, {"handedness", "right"}};
    if (name == "camera.view_target") return vector_value(vec(-view.cam.object_trans));
    if (name == "viewport.aspect") return double(view.width()) / view.height();
    QPoint cursor = view.mapFromGlobal(QCursor::pos());
    bool inside = view.rect().contains(cursor) && QApplication::widgetAt(QCursor::pos()) == &view;
    if (name == "viewport.cursor" && inside)
      return {{"x", 2. * cursor.x() / view.width() - 1}, {"y", 1 - 2. * cursor.y() / view.height()}};
    if (name == "model.bounds") {
      auto b = view.getRenderer()->getBoundingBox();
      if (b.isEmpty()) return nullptr;
      return {{"min", vector_value(vec(b.min()))}, {"max", vector_value(vec(b.max()))}};
    }
    const bool center = name == "pick.viewport_center";
    if (center || (name == "pick.cursor" && inside)) {
      const QPoint point = center ? view.rect().center() : cursor;
      const double scale = view.devicePixelRatioF();
      openaxis::Value result = {{"markerPosition", {point.x(), point.y()}}};
      view.makeCurrent();
      // Render the current camera before reading depth; queued writes may have
      // changed the camera since the last paint event.
      view.GLView::paintGL();
      GLint viewport[4]; GLdouble model[16], projection[16];
      glGetIntegerv(GL_VIEWPORT, viewport);
      glGetDoublev(GL_MODELVIEW_MATRIX, model);
      glGetDoublev(GL_PROJECTION_MATRIX, projection);
      const int x = std::clamp(int(point.x() * scale), 0, viewport[2] - 1);
      const int y = std::clamp(viewport[3] - 1 - int(point.y() * scale), 0, viewport[3] - 1);
      GLfloat depth = 1;
      glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
      GLdouble wx, wy, wz;
      if (depth < 1 && gluUnProject(x, y, depth, model, projection, viewport, &wx, &wy, &wz))
        result["point"] = {wx, wy, wz};
      view.update();
      return result;
    }
    // OpenSCAD has no persistent geometry selection. Unknown facts stay null.
    return nullptr;
  }
  void draw() {
    if (!available() || !visible) return;
    const auto frame = diagnostics.presentation();
    QPainter painter(&view);
    painter.setPen(Qt::white);
    QString status = QString("OpenAxis: %1 | %2").arg(QString::fromStdString(connection.status().state),
      focused ? "Focused" : "Inactive");
    painter.fillRect(QRect(8,8,420,28 + int(frame.lines.size()) * 18), QColor(0,0,0,180));
    painter.drawText(16,27,status);
    int y = 47;
    if (frame.context == context) for (const auto &line : frame.lines) {
      painter.drawText(16,y,QString::fromStdString(line.text)); y += 18;
    }
    if (frame.expires_at && expiry != frame.expires_at) {
      expiry = frame.expires_at;
      scheduler.post_at(*expiry, [this] { expiry.reset(); view.update(); });
    }
  }
};
OpenAxisController::OpenAxisController(QGLView &v) : QObject(&v), impl(std::make_unique<Impl>(v)) {
  qApp->installEventFilter(this);
  auto diagnostics = new QAction(tr("OpenAxis Diagnostics"), this);
  diagnostics->setCheckable(true);
  diagnostics->setShortcut(QKeySequence("Ctrl+Shift+D"));
  diagnostics->setShortcutContext(Qt::WindowShortcut);
  v.addAction(diagnostics);
  connect(diagnostics, &QAction::toggled, this, [this](bool checked) {
    impl->visible = checked; impl->refresh(); impl->view.update();
  });
}
OpenAxisController::~OpenAxisController() { qApp->removeEventFilter(this); }
void OpenAxisController::refresh() { impl->refresh(); }
void OpenAxisController::draw() { impl->draw(); }
bool OpenAxisController::eventFilter(QObject *, QEvent *event) {
  switch (event->type()) {
  case QEvent::WindowActivate: case QEvent::WindowDeactivate:
  case QEvent::Show: case QEvent::Hide: case QEvent::Resize:
    QTimer::singleShot(0, this, [this] { impl->refresh(); }); break;
  default: break;
  }
  return false;
}
