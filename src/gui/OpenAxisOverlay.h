#pragma once
#include <QWidget>
#include <QPainter>
#include <algorithm>
#include <cstdint>
#include <limits>
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
// Paints the diagnostic status, text rows and screen markers directly over
// the viewport. It ignores mouse input, so native navigation and cursor facts
// are unaffected where it draws.
class ScreenOverlay : public QWidget
{
public:
  QString status;
  std::vector<openaxis::DiagnosticLine> lines;
  std::vector<openaxis::DiagnosticMarker> markers;
  // Presentation last copied into the overlay, maintained by the controller.
  std::uint64_t revision = std::numeric_limits<std::uint64_t>::max();
  std::string context;
  static constexpr int margin = 8, gap = 8, max_column_width = 420;

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
    paintRows(painter);
    paintMarkers(painter);
  }

private:
  struct Row {
    QString text;
    QColor color;
    QRect bounds;
  };
  // Word-wrapped rows flow top to bottom, then into further columns. Rows are
  // never dropped silently: when no column remains, a final row says how many
  // are not shown.
  void paintRows(QPainter& painter)
  {
    if (status.isEmpty() && lines.empty()) return;
    std::vector<Row> rows;
    if (!status.isEmpty()) rows.push_back({status, Qt::white, {}});
    for (const auto& line : lines)
      rows.push_back({QString::fromStdString(line.text), tone(line.tone), {}});
    const int column_width = std::max(1, std::min(max_column_width, width() - 2 * margin));
    const int bottom = height() - margin;
    const auto measure = [&](const QString& text) {
      return painter.boundingRect(QRect(0, 0, column_width, std::numeric_limits<int>::max() / 2),
                                  Qt::TextWordWrap, text);
    };
    int x = margin, y = margin;
    std::size_t shown = 0;
    // Backdrops cover only the painted text, not the whole column width.
    std::vector<QRect> columns{QRect(x, y, 0, 0)};
    for (; shown < rows.size(); ++shown) {
      QRect bounds = measure(rows[shown].text);
      // Leave room for the "not shown" row in the last possible column.
      const bool last_column = x + 2 * column_width + gap > width() - margin;
      const int reserve = last_column && shown + 1 < rows.size() ? painter.fontMetrics().height() + 2 : 0;
      if (y + bounds.height() > bottom - reserve && y > margin) {
        if (last_column) break;
        x += column_width + gap;
        y = margin;
        columns.push_back(QRect(x, y, 0, 0));
      }
      rows[shown].bounds = QRect(x, y, column_width, bounds.height());
      columns.back().setBottom(y + bounds.height());
      columns.back().setWidth(std::max(columns.back().width(), bounds.width()));
      y += bounds.height() + 2;
    }
    for (const auto& column : columns)
      painter.fillRect(column.adjusted(-4, -3, 4, 3), QColor(0, 0, 0, 190));
    for (std::size_t i = 0; i < shown; ++i) {
      painter.setPen(rows[i].color);
      painter.drawText(rows[i].bounds, Qt::TextWordWrap, rows[i].text);
    }
    if (shown < rows.size()) {
      const QString text = QString("%1 more rows not shown; enlarge the viewport").arg(rows.size() - shown);
      const QRect more(x, y, std::min(column_width, painter.fontMetrics().horizontalAdvance(text) + 1),
                       painter.fontMetrics().height());
      painter.fillRect(more.adjusted(-4, -3, 4, 3), QColor(0, 0, 0, 190));
      painter.setPen(Qt::white);
      painter.drawText(more, Qt::AlignLeft, text);
    }
  }
  void paintMarkers(QPainter& painter)
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
};

}  // namespace OpenAxisUI
