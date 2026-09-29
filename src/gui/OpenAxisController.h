#pragma once
#include <QObject>
#include <memory>
class QGLView;
// Owned by the viewport; all scene access happens on the Qt event loop.
class OpenAxisController : public QObject {
public:
  explicit OpenAxisController(QGLView &view);
  ~OpenAxisController() override;
  void refresh();
  void draw();
private:
  bool eventFilter(QObject *, QEvent *) override;
  struct Impl;
  std::unique_ptr<Impl> impl;
};
