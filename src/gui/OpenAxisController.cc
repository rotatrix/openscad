#include "gui/QGLView.h"
#include "gui/OpenAxisController.h"
#include "gui/OpenAxisCamera.h"
#include <QApplication>
#include <QAction>
#include <QCursor>
#include <QPainter>
#include <QMenu>
#include <QTimer>
#include <QDockWidget>
#include <QMainWindow>
#include <QTextEdit>
#include <QScrollBar>
#include <QPointer>
#include <QOpenGLFramebufferObject>
#include <QOpenGLContext>
#include "utils/scope_guard.hpp"
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
  QPointer<QDockWidget> diagnostic_panel;
  QPointer<QTextEdit> diagnostic_text;
  QString last_text;

  static QColor tone(const std::string& name)
  {
    const auto& palette = openaxis::diagnostic_colors();
    auto it = palette.find(name);
    if (it == palette.end()) return Qt::white;
    const auto& rgb = it->second;
    return QColor(rgb[0], rgb[1], rgb[2]);
  }

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
    delete diagnostic_panel;
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
      // QOpenGLWidget may use multisampling, from which depth cannot be read
      // directly. Render geometry alone into a single-sample depth/stencil FBO.
      auto *previous = QOpenGLContext::currentContext();
      auto *surface = previous ? previous->surface() : nullptr;
      view.makeCurrent();
      auto restore_context = sg::make_scope_guard([&] {
        view.doneCurrent();
        if (previous && surface) previous->makeCurrent(surface);
        view.update();
      });
      GLint framebuffer = 0, matrix_mode = 0, program = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
      glGetIntegerv(GL_CURRENT_PROGRAM, &program);
      glGetIntegerv(GL_MATRIX_MODE, &matrix_mode);
      glPushAttrib(GL_ALL_ATTRIB_BITS);
      glMatrixMode(GL_PROJECTION);
      glPushMatrix();
      glMatrixMode(GL_MODELVIEW);
      glPushMatrix();
      auto restore_gl = sg::make_scope_guard([&] {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glUseProgram(program);
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        glMatrixMode(matrix_mode);
        glPopAttrib();
      });
      QOpenGLFramebufferObjectFormat format;
      format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
      format.setSamples(0);
      const QSize pixels(qRound(view.width() * scale), qRound(view.height() * scale));
      QOpenGLFramebufferObject target(pixels, format);
      if (!target.isValid() || !target.bind()) return nullptr;
      glUseProgram(0);
      glViewport(0, 0, pixels.width(), pixels.height());
      glDisable(GL_SCISSOR_TEST);
      glEnable(GL_DEPTH_TEST);
      glDepthMask(GL_TRUE);
      glClearDepth(1.0);
      const bool axes = view.showAxes(), crosshairs = view.showCrosshairs();
      auto selected = std::move(view.selected_obj);
      auto shown = std::move(view.shown_obj);
      auto restore_decorations = sg::make_scope_guard([&] {
        view.setShowAxes(axes);
        view.setShowCrosshairs(crosshairs);
        view.selected_obj = std::move(selected);
        view.shown_obj = std::move(shown);
      });
      view.setShowAxes(false);
      view.setShowCrosshairs(false);
      view.GLView::paintGL();
      // Renderers may change GL matrices. These are the native world-camera
      // matrices captured by setupCamera, independent of subsequent drawing.
      const GLint viewport[] = {0, 0, pixels.width(), pixels.height()};
      const int x = std::clamp(int(point.x() * scale), 0, pixels.width() - 1);
      const int y = std::clamp(pixels.height() - 1 - int(point.y() * scale), 0, pixels.height() - 1);
      GLfloat depth = 1;
      glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
      GLdouble wx, wy, wz;
      if (std::isfinite(depth) && depth >= 0 && depth < 1 &&
          gluUnProject(x + 0.5, y + 0.5, depth, view.modelview, view.projection, viewport, &wx, &wy,
                       &wz) &&
          std::isfinite(wx) && std::isfinite(wy) && std::isfinite(wz))
        result["point"] = {wx, wy, wz};
      return result;
    }
    // OpenSCAD has no persistent geometry selection. Unknown facts stay null.
    return nullptr;
  }
  void draw()
  {
    if (!available() || (!visible && !pivot)) return;
    const auto frame = diagnostics.presentation();
    // Draw world evidence with the current native camera, including mouse
    // movement. Neither diagnostics nor pivot graphics write scene depth.
    GLint matrix_mode = 0, program = 0;
    glGetIntegerv(GL_MATRIX_MODE, &matrix_mode);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    view.setupCamera();
    glLoadMatrixd(view.modelview);
    glUseProgram(0);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    const double scale = view.devicePixelRatioF();
    if (visible && frame.context == context) {
      glDisable(GL_DEPTH_TEST);
      for (const auto& segment : frame.segments) {
        const auto color = tone(segment.tone);
        glColor4d(color.redF(), color.greenF(), color.blueF(), segment.opacity);
        glLineWidth(float(segment.width * scale));
        glBegin(GL_LINES);
        glVertex3d(segment.start.x, segment.start.y, segment.start.z);
        glVertex3d(segment.end.x, segment.end.y, segment.end.z);
        glEnd();
      }
    }
    if (pivot) {
      GLdouble x, y, z;
      GLint viewport[4];
      glGetIntegerv(GL_VIEWPORT, viewport);
      if (gluProject(pivot->x, pivot->y, pivot->z, view.modelview, view.projection, viewport, &x, &y,
                     &z) &&
          z >= 0 && z <= 1 && x >= viewport[0] && x < viewport[0] + viewport[2] && y >= viewport[1] &&
          y < viewport[1] + viewport[3]) {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, viewport[2], 0, viewport[3], -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glEnable(GL_DEPTH_TEST);
        // Separate fill and annulus avoid double blending occluded fragments.
        for (int pass = 0; pass != 2; ++pass) {
          glDepthFunc(pass == 0 ? GL_GREATER : GL_LEQUAL);
          const double alpha = pass == 0 ? 0.23 : 1.0;
          glColor4d(0, 1, 0, alpha);
          glBegin(GL_TRIANGLE_FAN);
          glVertex3d(x - viewport[0], y - viewport[1], 1 - 2 * z);
          for (int i = 0; i <= 40; ++i) {
            const double a = i * 6.283185307179586 / 40;
            glVertex3d(x - viewport[0] + 4 * scale * std::cos(a),
                       y - viewport[1] + 4 * scale * std::sin(a), 1 - 2 * z);
          }
          glEnd();
          glColor4d(0, 0, 0, alpha);
          glBegin(GL_QUAD_STRIP);
          for (int i = 0; i <= 40; ++i) {
            const double a = i * 6.283185307179586 / 40;
            for (double radius : {4.0, 5.5})
              glVertex3d(x - viewport[0] + radius * scale * std::cos(a),
                         y - viewport[1] + radius * scale * std::sin(a), 1 - 2 * z);
          }
          glEnd();
        }
      }
    }
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(matrix_mode);
    glUseProgram(program);
    glPopAttrib();
    if (!visible) return;
    QPainter painter(&view);
    painter.setClipRect(view.rect());
    painter.setRenderHint(QPainter::Antialiasing);
    if (frame.context == context) {
      for (const auto& marker : frame.markers) {
        const QPointF point(marker.point[0], marker.point[1]);
        if (!view.rect().contains(point.toPoint())) continue;
        auto color = tone(marker.tone);
        color.setAlphaF(0.65);
        painter.setPen(QPen(color, 2));
        painter.drawLine(point + QPointF(-9, 0), point + QPointF(9, 0));
        painter.drawLine(point + QPointF(0, -9), point + QPointF(0, 9));
        const auto labels = QString::fromStdString(marker.label).split('\n');
        int width = 0;
        for (const auto& label : labels)
          width = std::max(width, painter.fontMetrics().horizontalAdvance(label));
        const double left =
          std::clamp(point.x() + 12, 4., std::max(4., double(view.width() - width - 8)));
        const double top = std::clamp(point.y() - 8 - painter.fontMetrics().ascent(), 4.,
                                      std::max(4., double(view.height() - labels.size() * 15 - 8)));
        painter.fillRect(QRectF(left - 3, top - 2, width + 6, labels.size() * 15 + 4),
                         QColor(0, 0, 0, 210));
        painter.setPen(tone(marker.tone));
        for (int i = 0; i < labels.size(); ++i)
          painter.drawText(QPointF(left, top + painter.fontMetrics().ascent() + i * 15), labels[i]);
      }
    }
    if (diagnostic_text) {
      QString html = QString("<p>OpenAxis: %1 | %2</p>")
                       .arg(QString::fromStdString(connection.status().state).toHtmlEscaped(),
                            focused ? "Focused" : "Inactive");
      if (frame.context == context)
        for (const auto& line : frame.lines)
          html += QString("<p style='color:%1; margin:2px 0'>%2</p>")
                    .arg(tone(line.tone).name(), QString::fromStdString(line.text).toHtmlEscaped());
      if (html != last_text) {
        const int scroll = diagnostic_text->verticalScrollBar()->value();
        diagnostic_text->setHtml(html);
        diagnostic_text->verticalScrollBar()->setValue(scroll);
        last_text = html;
      }
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
    if (auto window = qobject_cast<QMainWindow *>(impl->view.window())) {
      impl->diagnostic_panel = new QDockWidget(tr("OpenAxis Diagnostics"), window);
      impl->diagnostic_panel->setObjectName("OpenAxisDiagnosticsDock");
      impl->diagnostic_text = new QTextEdit(impl->diagnostic_panel);
      impl->diagnostic_text->setReadOnly(true);
      impl->diagnostic_text->setFocusPolicy(Qt::NoFocus);
      impl->diagnostic_text->setStyleSheet("QTextEdit { background: #181818; color: white; }");
      impl->diagnostic_panel->setWidget(impl->diagnostic_text);
      window->addDockWidget(Qt::RightDockWidgetArea, impl->diagnostic_panel);
      impl->diagnostic_panel->hide();
      connect(impl->diagnostic_panel, &QDockWidget::visibilityChanged, diagnostics,
              &QAction::setChecked);
    }
    if (auto menu = impl->view.window()->findChild<QMenu *>("menu_View")) {
      menu->addSeparator();
      menu->addAction(navigation);
      menu->addAction(diagnostics);
    }
  });
  connect(diagnostics, &QAction::toggled, this, [this](bool checked) {
    impl->visible = checked;
    if (impl->diagnostic_panel) impl->diagnostic_panel->setVisible(checked);
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
