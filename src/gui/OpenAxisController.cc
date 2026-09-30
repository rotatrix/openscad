#include "gui/QGLView.h"
#include "gui/MainWindow.h"
#include "gui/OpenAxisController.h"
#include "gui/OpenAxisCamera.h"
#include "gui/OpenAxisOverlay.h"
#include "core/CSGNode.h"
#include "glview/ShaderUtils.h"
#include "glview/preview/ThrownTogetherRenderer.h"
#ifdef ENABLE_OPENCSG
#include "glview/preview/OpenCSGRenderer.h"
#include <opencsg.h>
#endif
#include "version.h"
#include <QApplication>
#include <QAction>
#include <QCursor>
#include <QMenu>
#include <QTimer>
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
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
using namespace OpenAxisCamera;
using OpenAxisUI::ScreenOverlay;
using OpenAxisUI::tone;
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
// One SDK session log per process, shared by the controllers of all windows.
int log_users = 0;
std::shared_ptr<openaxis::DiagnosticLog> session_log;
void acquire_log()
{
  if (log_users++ == 0)
    session_log = openaxis::DiagnosticLog::configure("openscad", {}, openscad_displayversionnumber);
}
void release_log()
{
  if (--log_users == 0 && session_log) {
    session_log->close();
    session_log.reset();
  }
}
openaxis::OpenAxisClientOptions client_options(Scheduler& scheduler)
{
  acquire_log();
  openaxis::OpenAxisClientOptions o;
  o.client_name = "OpenSCAD";
  o.scheduler = &scheduler;
  o.target = {{"pid", openaxis::current_process_id()}};
  return o;
}
openaxis::Value bounds_value(const BoundingBox& b)
{
  return {{"min", openaxis::vector_value(vec(b.min()))}, {"max", openaxis::vector_value(vec(b.max()))}};
}
// Rendered CSG products of a preview renderer, whose select pass writes leaf
// indices. Other renderers (full render) supply depth only.
struct PickProducts {
  std::array<std::shared_ptr<CSGProducts>, 3> products;
  bool throwntogether = false;
};
std::optional<PickProducts> pick_products(const Renderer *renderer)
{
#ifdef ENABLE_OPENCSG
  if (auto r = dynamic_cast<const OpenCSGRenderer *>(renderer)) return PickProducts{r->products(), false};
#endif
  if (auto r = dynamic_cast<const ThrownTogetherRenderer *>(renderer))
    return PickProducts{r->products(), true};
  return std::nullopt;
}
}  // namespace
struct OpenAxisController::Impl : openaxis::NavigationAdapter {
  QGLView& view;
  Scheduler scheduler;
  openaxis::OpenAxisClient client;
  openaxis::NavigationDiagnostics diagnostics;
  openaxis::NavigationSession session;
  openaxis::OpenAxisConnectionManager connection;
  bool focused = false, visible = false;
  std::string context;
  Eigen::Matrix3d last_rotation = Eigen::Matrix3d::Identity();
  Eigen::Vector3d last_translation = Eigen::Vector3d::Zero();
  double last_distance = 0, last_fov = 0;
  Camera::ProjectionType last_projection = Camera::ProjectionType::PERSPECTIVE;
  std::optional<openaxis::Vec3> pivot;
  std::optional<double> expiry;
  QPointer<ScreenOverlay> overlay;

  // Offscreen pick render: native select-shader leaf indices plus depth, for
  // the camera, scene and size it depicts. Reused until any of these changes.
  struct PickFrame {
    bool valid = false, ids = false;
    const Renderer *renderer = nullptr;
    unsigned long scene = 0;
    Eigen::Vector3d rotation, translation;
    double distance = 0, fov = 0;
    Camera::ProjectionType projection = Camera::ProjectionType::PERSPECTIVE;
    QSize pixels;
    GLdouble modelview[16], projection_matrix[16];
    std::optional<PickProducts> products;
  } pick;
  QOpenGLContext *pick_context = nullptr;
  std::unique_ptr<QOpenGLFramebufferObject> pick_target;
  std::optional<ShaderUtils::ShaderInfo> select_shader;

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
          o.compare_camera = OpenAxisCamera::compare;
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
    if (pick_context && view.context() == pick_context) {
      view.makeCurrent();
      release_pick_resources();
      view.doneCurrent();
    }
    delete overlay;
    release_log();
  }
  void release_pick_resources()
  {
    pick_target.reset();
    if (select_shader) {
      glDeleteProgram(select_shader->resource.shader_program);
      glDeleteShader(select_shader->resource.vertex_shader);
      glDeleteShader(select_shader->resource.fragment_shader);
      select_shader.reset();
    }
    pick.valid = false;
  }
  bool available() const
  {
    return view.isValid() && view.isVisible() && view.width() > 0 && view.height() > 0 &&
           view.getRenderer();
  }
  // The navigation target is this viewport's camera. Re-rendering (preview,
  // render, view mode, animation, reload) keeps it; resizing and DPI changes
  // replace the viewport geometry used by queries.
  std::string key() const
  {
    return std::to_string(reinterpret_cast<std::uintptr_t>(&view)) + "/" +
           std::to_string(view.width()) + "/" + std::to_string(view.height()) + "/" +
           std::to_string(view.devicePixelRatioF());
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
    QPoint cursor;
    bool inside;
    Capture(Impl& o, const openaxis::NavigationContext& c) : owner(o), context(c), initial(o.pose())
    {
      cursor = o.view.mapFromGlobal(QCursor::pos());
      inside = o.view.rect().contains(cursor) && QApplication::widgetAt(QCursor::pos()) == &o.view;
    }
    std::optional<openaxis::Pose> initial_observation() override { return initial; }
    openaxis::Value resolve(const std::string& name) override
    {
      if (!owner.is_current(context)) return nullptr;
      return name == "camera.pose" ? openaxis::pose_value(initial) : owner.fact(name, cursor, inside);
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
  // Stable identity of the document shown in this window: the active editor
  // tab. It does not change when the document is re-rendered.
  std::string document_id() const
  {
    const auto *window = qobject_cast<const MainWindow *>(view.window());
    const void *document = window && window->activeEditor ? static_cast<const void *>(window->activeEditor)
                                                          : static_cast<const void *>(view.window());
    return std::to_string(reinterpret_cast<std::uintptr_t>(document));
  }
  bool pick_matches(const QSize& pixels) const
  {
    const auto& c = view.cam;
    return pick.valid && pick.renderer == view.getRenderer() && pick.scene == view.sceneRevision &&
           pick.rotation == c.object_rot && pick.translation == c.object_trans &&
           pick.distance == c.zoomValue() && pick.fov == c.fov && pick.projection == c.projection &&
           pick.pixels == pixels;
  }
  // Renders the scene once with the native select shader: leaf indices in
  // colour and the displayed surface in depth (OpenCSG writes the CSG result
  // depth before the ID pass at GL_EQUAL). Decorations, lighting, edges,
  // pivots and diagnostics are not drawn. Requires the view's context.
  bool render_pick(const QSize& pixels)
  {
    auto *current = QOpenGLContext::currentContext();
    if (current != pick_context) {
      // Resources of a destroyed context went with it.
      pick_target.reset();
      select_shader.reset();
      pick.valid = false;
      pick_context = current;
    }
    if (pick_matches(pixels)) return true;
    auto *renderer = view.getRenderer();
    if (!renderer) return false;
    if (!pick_target || pick_target->size() != pixels) {
      QOpenGLFramebufferObjectFormat format;
      format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
      format.setSamples(0);
      pick_target = std::make_unique<QOpenGLFramebufferObject>(pixels, format);
      if (!pick_target->isValid()) {
        pick_target.reset();
        return false;
      }
    }
    auto products = pick_products(renderer);
    if (products && !select_shader) {
      const auto resource =
        ShaderUtils::compileShaderProgram(ShaderUtils::loadShaderSource("MouseSelector.vert"),
                                          ShaderUtils::loadShaderSource("MouseSelector.frag"));
      const GLint idcolor = resource.shader_program
                              ? glGetUniformLocation(resource.shader_program, "frag_idcolor")
                              : -1;
      if (idcolor >= 0)
        select_shader = ShaderUtils::ShaderInfo{
          resource, ShaderUtils::ShaderType::SELECT_RENDERING, {{"frag_idcolor", idcolor}}, {}};
    }
    const bool ids = products && select_shader;

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
    if (!pick_target->bind()) return false;
    glUseProgram(0);
    glViewport(0, 0, pixels.width(), pixels.height());
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DITHER);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    view.setupCamera();
    const auto& c = view.cam;
    glTranslated(c.object_trans.x(), c.object_trans.y(), c.object_trans.z());
#ifdef ENABLE_OPENCSG
    OpenCSG::setContext(view.opencsg_id);
#endif
    renderer->prepare(view.edge_shader.get());
    renderer->draw(false, ids ? &*select_shader : nullptr);
    pick.valid = true;
    pick.ids = ids;
    pick.products = ids ? products : std::optional<PickProducts>{};
    pick.renderer = renderer;
    pick.scene = view.sceneRevision;
    pick.rotation = c.object_rot;
    pick.translation = c.object_trans;
    pick.distance = c.zoomValue();
    pick.fov = c.fov;
    pick.projection = c.projection;
    pick.pixels = pixels;
    std::memcpy(pick.modelview, view.modelview, sizeof pick.modelview);
    std::memcpy(pick.projection_matrix, view.projection, sizeof pick.projection_matrix);
    return true;
  }
  // Bounds of the CSG product (visible body) containing the picked leaf. A
  // cavity surface carries the subtracted leaf's index, so the leaf's own
  // bounds would describe the cutter. A normalized leaf can occur in several
  // products; use those whose bounds contain the hit.
  std::optional<BoundingBox> body_bounds(int index, const Eigen::Vector3d& point) const
  {
    if (!pick.products || index <= 0) return std::nullopt;
    for (const auto& list : pick.products->products) {
      if (!list) continue;
      BoundingBox merged;
      bool found = false;
      for (const auto& product : list->products) {
        const auto has_leaf = [index](const std::vector<CSGChainObject>& chain) {
          return std::any_of(chain.begin(), chain.end(),
                             [index](const auto& o) { return o.leaf && o.leaf->index == index; });
        };
        if (!has_leaf(product.intersections) && !has_leaf(product.subtractions)) continue;
        const auto box = product.getBoundingBox(pick.products->throwntogether);
        if (box.isEmpty()) continue;
        // Depth-buffer precision: allow a small fraction of the body and view size.
        const double tolerance = 1e-3 * box.diagonal().norm() + 1e-5 * view.cam.zoomValue();
        if (box.exteriorDistance(point) > tolerance) continue;
        merged.extend(box);
        found = true;
      }
      if (found) return merged;
    }
    return std::nullopt;
  }
  openaxis::Value pick_at(const QPoint& point)
  {
    openaxis::Value result = {{"markerPosition", {point.x(), point.y()}}};
    auto *previous = QOpenGLContext::currentContext();
    auto *surface = previous ? previous->surface() : nullptr;
    view.makeCurrent();
    auto restore_context = sg::make_scope_guard([&] {
      view.doneCurrent();
      if (previous && surface) previous->makeCurrent(surface);
    });
    const double scale = view.devicePixelRatioF();
    const QSize pixels(qRound(view.width() * scale), qRound(view.height() * scale));
    if (!render_pick(pixels)) return nullptr;
    GLint framebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    auto restore_framebuffer = sg::make_scope_guard([&] { glBindFramebuffer(GL_FRAMEBUFFER, framebuffer); });
    if (!pick_target->bind()) return nullptr;
    const int x = std::clamp(int(point.x() * scale), 0, pixels.width() - 1);
    const int y = std::clamp(pixels.height() - 1 - int(point.y() * scale), 0, pixels.height() - 1);
    GLfloat depth = 1;
    GLubyte color[4] = {0, 0, 0, 0};
    GLint alignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, color);
    glPixelStorei(GL_PACK_ALIGNMENT, alignment);
    const GLint viewport[] = {0, 0, pixels.width(), pixels.height()};
    GLdouble wx, wy, wz;
    if (!std::isfinite(depth) || depth < 0 || depth >= 1 ||
        !gluUnProject(x + 0.5, y + 0.5, depth, pick.modelview, pick.projection_matrix, viewport, &wx,
                      &wy, &wz) ||
        !std::isfinite(wx) || !std::isfinite(wy) || !std::isfinite(wz))
      return result;  // A tested miss: marker only.
    result["point"] = {wx, wy, wz};
    if (pick.ids) {
      const int index = color[0] | (color[1] << 8) | (color[2] << 16);
      if (auto box = body_bounds(index, Eigen::Vector3d(wx, wy, wz))) result["bounds"] = bounds_value(*box);
    }
    return result;
  }
  openaxis::Value fact(const std::string& name, const QPoint& cursor, bool inside)
  {
    using openaxis::vector_value;
    if (name == "document.id") return document_id();
    if (name == "world.orientation")
      return {{"forward", {0, 1, 0}}, {"up", {0, 0, 1}}, {"handedness", "right"}};
    if (name == "camera.view_target") return vector_value(vec(-view.cam.object_trans));
    if (name == "viewport.aspect") return double(view.width()) / view.height();
    if (name == "viewport.cursor" && inside)
      return {{"x", 2. * cursor.x() / view.width() - 1}, {"y", 1 - 2. * cursor.y() / view.height()}};
    if (name == "model.bounds") {
      auto b = view.getRenderer()->getBoundingBox();
      if (b.isEmpty()) return nullptr;
      return bounds_value(b);
    }
    if (name == "pick.viewport_center") return pick_at(view.rect().center());
    if (name == "pick.cursor" && inside) return pick_at(cursor);
    // OpenSCAD has no persistent geometry selection. Unknown facts stay null.
    return nullptr;
  }
  void draw()
  {
    if (!available() || !visible) {
      if (overlay) overlay->hide();
    }
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
    if (overlay) {
      // Text rows and screen markers are painted in the viewport by a raster,
      // mouse-transparent child widget, outside the native GL framebuffer.
      overlay->setGeometry(view.rect());
      const bool current = frame.context == context;
      const QString status = QString("OpenAxis: %1 | %2")
                               .arg(QString::fromStdString(connection.status().state),
                                    focused ? "Focused" : "Inactive");
      if (overlay->revision != frame.revision || overlay->context != context || overlay->status != status) {
        overlay->revision = frame.revision;
        overlay->context = context;
        overlay->status = status;
        overlay->lines = current ? frame.lines : std::vector<openaxis::DiagnosticLine>{};
        overlay->markers = current ? frame.markers : std::vector<openaxis::DiagnosticMarker>{};
        overlay->update();
      }
      overlay->show();
    }
    if (frame.expires_at && expiry != frame.expires_at) {
      expiry = frame.expires_at;
      scheduler.post_at(*expiry, [this] {
        expiry.reset();
        // Expiry changes content without a revision; force an overlay update.
        if (overlay) overlay->revision = std::numeric_limits<std::uint64_t>::max();
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
  // The viewport is constructed before the main window's menus.
  QTimer::singleShot(0, this, [this, diagnostics] {
    impl->overlay = new ScreenOverlay(&impl->view);
    impl->overlay->hide();
    if (auto menu = impl->view.window()->findChild<QMenu *>("menu_View")) {
      menu->addSeparator();
      menu->addAction(diagnostics);
    }
  });
  connect(diagnostics, &QAction::toggled, this, [this](bool checked) {
    impl->visible = checked;
    if (!checked && impl->overlay) impl->overlay->hide();
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
