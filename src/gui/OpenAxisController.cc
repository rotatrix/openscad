#include "gui/QGLView.h"
#include "gui/OpenAxisController.h"
#include "gui/OpenAxisCamera.h"
#include <QApplication>
#include <QAction>
#include <QCursor>
#include <QPainter>
#include <QMenu>
#include <QTimer>
#pragma push_macro("emit")
#undef emit
#include <openaxis/navigation.hpp>
#include <openaxis/connection_manager.hpp>
#include <openaxis/logging.hpp>
#pragma pop_macro("emit")
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
using namespace OpenAxisCamera;
class Scheduler : public QObject, public openaxis::Scheduler
{
public:
  std::function<void()> before;
  void post(Callback f) override
  {
    QTimer::singleShot(0, this, [this, f = std::move(f)] {
      if (before) before();
      f();
    });
  }
  void post_at(double deadline, Callback f) override
  {
    QTimer::singleShot(0, this, [this, deadline, f = std::move(f)] {
      double delay = std::ceil((deadline - openaxis::diagnostic_time()) * 1000);
      QTimer::singleShot(int(std::clamp(delay, 0., double(std::numeric_limits<int>::max()))), this,
                         [this, f] {
                           if (before) before();
                           f();
                         });
    });
  }
};
openaxis::OpenAxisClientOptions client_options(Scheduler& scheduler)
{
  openaxis::DiagnosticLog::configure("openscad");
  openaxis::OpenAxisClientOptions o;
  o.client_name = "OpenSCAD";
  o.scheduler = &scheduler;
  o.target = {{"pid", openaxis::current_process_id()}};
  return o;
}
}  // namespace
struct OpenAxisController::Impl : openaxis::NavigationAdapter {
  QGLView& view;
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

  explicit Impl(QGLView& v)
    : view(v),
      client(client_options(scheduler)),
      session(
        client, *this, &diagnostics,
        [this] {
          openaxis::NavigationOptions o;
          o.scheduler = &scheduler;
          o.observation = [this](const openaxis::NavigationContext& c) -> std::optional<openaxis::Pose> {
            return is_current(c) ? std::optional<openaxis::Pose>(pose()) : std::nullopt;
          };
          return o;
        }()),
      connection(client, {[this] {
                   openaxis::ConnectionMetadata m;
                   m.tags = {"app.openscad", "workspace.modeling"};
                   m.capabilities = {"navigation"};
                   m.focused = focused;
                   return m;
                 }})
  {
    scheduler.before = [this] { refresh(); };
    diagnostics.on_changed = [this] {
      if (visible) view.update();
    };
    connection.on_state = [this](const openaxis::ConnectionStatus&) {
      if (visible) view.update();
    };
    connection.start();
  }
  ~Impl()
  {
    scheduler.before = {};
    connection.stop();
    session.close();
  }
  bool available() const
  {
    return enabled && view.isValid() && view.isVisible() && view.width() > 0 && view.height() > 0 &&
           view.getRenderer();
  }
  std::string key() const
  {
    return std::to_string(reinterpret_cast<std::uintptr_t>(view.getRenderer())) + "/" +
           std::to_string(view.sceneRevision) + "/" + std::to_string(view.width()) + "/" +
           std::to_string(view.height()) + "/" + std::to_string(view.devicePixelRatioF());
  }
  void remember()
  {
    last_rotation = rotation(view.cam.object_rot);
    last_translation = view.cam.object_trans;
    last_distance = view.cam.zoomValue();
    last_fov = view.cam.fov;
    last_projection = view.cam.projection;
  }
  void refresh()
  {
    bool next = available() && view.window()->isActiveWindow() && !QApplication::activeModalWidget();
    const std::string next_context = available() ? key() : "";
    if (next_context != context || (focused && !next)) {
      session.cancel("viewport_changed");
      diagnostics.clear();
      pivot.reset();
    }
    context = next_context;
    if (next != focused) {
      focused = next;
      connection.refresh_metadata();
    }
    bool changed = !last_rotation.isApprox(rotation(view.cam.object_rot)) ||
                   !last_translation.isApprox(view.cam.object_trans) ||
                   last_distance != view.cam.zoomValue() || last_fov != view.cam.fov ||
                   last_projection != view.cam.projection;
    remember();
    diagnostics.set_enabled(visible && available());
    diagnostics.set_context(context);
    if (changed && focused) session.native_camera_changed();
  }
  openaxis::Pose pose() const
  {
    const auto& c = view.cam;
    return OpenAxisCamera::read(c.object_rot, c.object_trans, c.zoomValue(), c.fov,
                                c.projection == Camera::ProjectionType::PERSPECTIVE);
  }
  openaxis::NavigationContext capture_context() override
  {
    refresh();
    if (!focused) return {};
    return context;
  }
  bool is_current(const openaxis::NavigationContext& c) override
  {
    auto k = std::any_cast<std::string>(&c);
    return k && focused && available() && view.window()->isActiveWindow() &&
           !QApplication::activeModalWidget() && *k == key();
  }
  struct Capture : openaxis::NavigationCapture {
    Impl& owner;
    openaxis::NavigationContext context;
    openaxis::Pose initial;
    Capture(Impl& o, const openaxis::NavigationContext& c) : owner(o), context(c), initial(o.pose()) {}
    std::optional<openaxis::Pose> initial_observation() override { return initial; }
    openaxis::Value resolve(const std::string& name) override
    {
      if (!owner.is_current(context)) return nullptr;
      return name == "camera.pose" ? openaxis::pose_value(initial) : owner.fact(name);
    }
  };
  std::unique_ptr<openaxis::NavigationCapture> begin_query(const openaxis::NavigationContext& c) override
  {
    if (!is_current(c)) return {};
    return std::make_unique<Capture>(*this, c);
  }
  openaxis::WriteResult apply_pose(const openaxis::NavigationContext& c,
                                   const openaxis::NavigationPose& p, const openaxis::Value&,
                                   std::optional<openaxis::Vec3>) override
  {
    if (!is_current(c)) return {};
    double distance = view.cam.zoomValue(), fov = view.cam.fov;
    if (!OpenAxisCamera::write(p, view.cam.object_rot, view.cam.object_trans, distance, fov)) return {};
    view.cam.setVpd(distance);
    view.cam.setVpf(fov);
    view.cam.setProjection(p.fov > 0 ? Camera::ProjectionType::PERSPECTIVE
                                     : Camera::ProjectionType::ORTHOGONAL);
    remember();
    view.update();
    emit view.cameraChanged();
    return {true, pose()};
  }
  void show_pivot(const openaxis::NavigationContext& c, std::optional<openaxis::Vec3> p) override
  {
    if (!p || is_current(c)) {
      pivot = p;
      view.update();
    }
  }
  openaxis::Value fact(const std::string& name)
  {
    using openaxis::vector_value;
    if (name == "document.id")
      return std::to_string(reinterpret_cast<std::uintptr_t>(view.getRenderer()));
    if (name == "world.orientation")
      return {{"forward", {0, 1, 0}}, {"up", {0, 0, 1}}, {"handedness", "right"}};
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
      auto *old_context = getGLContext();
      view.makeCurrent();
      // Render the current camera before reading depth; queued writes may have
      // changed the camera since the last paint event.
      const bool axes = view.showAxes(), crosshairs = view.showCrosshairs();
      view.setShowAxes(false);
      view.setShowCrosshairs(false);
      view.GLView::paintGL();
      view.setShowAxes(axes);
      view.setShowCrosshairs(crosshairs);
      GLint viewport[4];
      GLdouble model[16], projection[16];
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
      view.doneCurrent();
      setGLContext(old_context);
      view.update();
      return result;
    }
    // OpenSCAD has no persistent geometry selection. Unknown facts stay null.
    return nullptr;
  }
  void draw()
  {
    if (!available() || (!visible && !pivot)) return;
    const auto frame = diagnostics.presentation();
    std::optional<QPointF> pivot_pixel;
    if (pivot) {
      view.setupCamera();
      glTranslated(view.cam.object_trans.x(), view.cam.object_trans.y(), view.cam.object_trans.z());
      GLdouble model[16], projection[16], x, y, z;
      GLint viewport[4];
      glGetDoublev(GL_MODELVIEW_MATRIX, model);
      glGetDoublev(GL_PROJECTION_MATRIX, projection);
      glGetIntegerv(GL_VIEWPORT, viewport);
      if (gluProject(pivot->x, pivot->y, pivot->z, model, projection, viewport, &x, &y, &z) && z >= 0 &&
          z <= 1)
        pivot_pixel =
          QPointF(x / view.devicePixelRatioF(), (viewport[3] - y) / view.devicePixelRatioF());
    }
    QPainter painter(&view);
    if (pivot_pixel) {
      painter.setPen(QPen(Qt::black, 1.5));
      painter.setBrush(Qt::green);
      painter.drawEllipse(*pivot_pixel, 4., 4.);
    }
    if (!visible) return;
    painter.setPen(Qt::white);
    QString status =
      QString("OpenAxis: %1 | %2")
        .arg(QString::fromStdString(connection.status().state), focused ? "Focused" : "Inactive");
    painter.fillRect(QRect(8, 8, 420, 28 + int(frame.lines.size()) * 18), QColor(0, 0, 0, 180));
    painter.drawText(16, 27, status);
    int y = 47;
    if (frame.context == context)
      for (const auto& line : frame.lines) {
        painter.drawText(16, y, QString::fromStdString(line.text));
        y += 18;
      }
    if (frame.expires_at && expiry != frame.expires_at) {
      expiry = frame.expires_at;
      scheduler.post_at(*expiry, [this] {
        expiry.reset();
        view.update();
      });
    }
  }
};
OpenAxisController::OpenAxisController(QGLView& v) : QObject(&v), impl(std::make_unique<Impl>(v))
{
  qApp->installEventFilter(this);
  auto diagnostics = new QAction(tr("OpenAxis Diagnostics"), this);
  diagnostics->setCheckable(true);

  auto navigation = new QAction(tr("OpenAxis Navigation"), this);
  navigation->setCheckable(true);
  navigation->setChecked(true);
  connect(navigation, &QAction::toggled, this, [this](bool checked) {
    impl->enabled = checked;
    impl->refresh();
    if (checked) impl->connection.start();
    else impl->connection.stop();
    impl->view.update();
  });
  // The viewport is constructed before the main window's menus.
  QTimer::singleShot(0, this, [this, diagnostics, navigation] {
    if (auto menu = impl->view.window()->findChild<QMenu *>("menu_View")) {
      menu->addSeparator();
      menu->addAction(navigation);
      menu->addAction(diagnostics);
    }
  });
  connect(diagnostics, &QAction::toggled, this, [this](bool checked) {
    impl->visible = checked;
    impl->refresh();
    impl->view.update();
  });
}
OpenAxisController::~OpenAxisController()
{
  qApp->removeEventFilter(this);
}
void OpenAxisController::refresh()
{
  impl->refresh();
}
void OpenAxisController::draw()
{
  impl->draw();
}
bool OpenAxisController::eventFilter(QObject *, QEvent *event)
{
  switch (event->type()) {
  case QEvent::WindowActivate:
  case QEvent::WindowDeactivate:
  case QEvent::Show:
  case QEvent::Hide:
  case QEvent::Resize:           QTimer::singleShot(0, this, [this] { impl->refresh(); }); break;
  default:                       break;
  }
  return false;
}
