#pragma once
#include <QWidget>
#include <QPainter>
#include <algorithm>
#include <openaxis/diagnostics.hpp>

namespace OpenAxisUI {
// Raster QWidget overlays keep QPainter's OpenGL engine out of the native
// OpenCSG framebuffer. QPainter on QOpenGLWidget changes depth/stencil state.
inline QColor tone(const std::string& name)
{
  const auto& palette = openaxis::diagnostic_colors();
  auto it = palette.find(name);
  if (it == palette.end()) return Qt::white;
  const auto& rgb = it->second;
  return QColor(rgb[0], rgb[1], rgb[2]);
}
class ScreenOverlay : public QWidget
{
public:
  std::vector<openaxis::DiagnosticMarker> markers;
  explicit ScreenOverlay(QWidget *parent) : QWidget(parent)
  {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
  }

protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter painter(this);
    painter.setClipRect(rect());
    painter.setRenderHint(QPainter::Antialiasing);
    {
      for (const auto& marker : markers) {
        const QPointF point(marker.point[0], marker.point[1]);
        if (!rect().contains(point.toPoint())) continue;
        auto color = tone(marker.tone);
        color.setAlphaF(0.65);
        painter.setPen(QPen(color, 2));
        painter.drawLine(point + QPointF(-9, 0), point + QPointF(9, 0));
        painter.drawLine(point + QPointF(0, -9), point + QPointF(0, 9));
        const auto labels = QString::fromStdString(marker.label).split('\n');
        int label_width = 0;
        for (const auto& label : labels)
          label_width = std::max(label_width, painter.fontMetrics().horizontalAdvance(label));
        const double left =
          std::clamp(point.x() + 12, 4., std::max(4., double(width() - label_width - 8)));
        const double top = std::clamp(point.y() - 8 - painter.fontMetrics().ascent(), 4.,
                                      std::max(4., double(height() - labels.size() * 15 - 8)));
        painter.fillRect(QRectF(left - 3, top - 2, label_width + 6, labels.size() * 15 + 4),
                         QColor(0, 0, 0, 210));
        painter.setPen(tone(marker.tone));
        for (int i = 0; i < labels.size(); ++i)
          painter.drawText(QPointF(left, top + painter.fontMetrics().ascent() + i * 15), labels[i]);
      }
    }
  }
};

}  // namespace OpenAxisUI
