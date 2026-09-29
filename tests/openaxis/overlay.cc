#include "gui/OpenAxisOverlay.h"
#include <QApplication>
#include <QImage>
#include <QOpenGLContext>
#include <iostream>

int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  OpenAxisUI::ScreenOverlay overlay(nullptr);
  overlay.resize(200, 120);
  const auto& palette = openaxis::diagnostic_colors();
  if (palette.empty()) return 1;
  const auto name = palette.begin()->first;
  overlay.markers.push_back({"pick.cursor\npick.viewport_center", {100, 60}, name});
  overlay.markers.push_back({"edge", {198, 118}, name});
  QImage image(overlay.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  overlay.render(&image, QPoint(), QRegion(), QWidget::DrawChildren);
  // The screen renderer must work without touching an OpenGL context. It must
  // retain the crosshair sample position and render a multiline label nearby.
  if (QOpenGLContext::currentContext() != nullptr) return 2;
  if (!overlay.testAttribute(Qt::WA_TransparentForMouseEvents)) return 3;
  if (qAlpha(image.pixel(100, 60)) == 0) return 4;
  int pixels = 0;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x)
      if (qAlpha(image.pixel(x, y))) ++pixels;
  if (pixels < 100) return 5;
  overlay.markers.clear();
  image.fill(Qt::transparent);
  overlay.render(&image, QPoint(), QRegion(), QWidget::DrawChildren);
  if (qAlpha(image.pixel(100, 60)) != 0) return 6;
  std::cout << "Raster overlay, multiline/edge markers and cleanup passed\n";
  return 0;
}
