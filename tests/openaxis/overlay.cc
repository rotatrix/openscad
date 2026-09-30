#include "gui/OpenAxisOverlay.h"
#include <QApplication>
#include <QImage>
#include <QOpenGLContext>
#include <iostream>
#include <string>

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

  // Text rows are painted in the viewport, flow into further columns and
  // report rows that do not fit instead of dropping them silently.
  const auto painted = [&](int x0, int y0, int x1, int y1) {
    image.fill(Qt::transparent);
    overlay.render(&image, QPoint(), QRegion(), QWidget::DrawChildren);
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x)
        if (qAlpha(image.pixel(x, y))) return true;
    return false;
  };
  // 900 px wide: 420 px columns at x=8 and x=436; the second is the last.
  overlay.resize(900, 120);
  image = QImage(overlay.size(), QImage::Format_ARGB32_Premultiplied);
  overlay.status = "OpenAxis: ready | Focused";
  overlay.lines.push_back({"pick.cursor hit", name});
  if (!painted(8, 8, 60, 30)) return 7;
  if (painted(300, 0, 900, 120)) return 8;  // Backdrop covers only the text.
  for (int i = 0; i < 40; ++i) overlay.lines.push_back({"row " + std::to_string(i), name});
  if (!painted(436, 8, 480, 30)) return 9;  // Rows flow into a second column.
  if (painted(870, 0, 900, 120)) return 11;  // No third column.
  overlay.status.clear();
  overlay.lines.clear();
  if (painted(0, 0, overlay.width(), overlay.height())) return 10;
  std::cout << "Raster overlay, text rows, multiline/edge markers and cleanup passed\n";
  return 0;
}
