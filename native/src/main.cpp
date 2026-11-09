#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

#include "mainwindow.h"

static void applyDarkTheme(QApplication& app) {
  app.setStyle(QStyleFactory::create("Fusion"));
  QPalette p;
  const QColor window("#24272c"), base("#1b1d21"), button("#31353c"), text("#e8e8ea");
  p.setColor(QPalette::Window, window);
  p.setColor(QPalette::WindowText, text);
  p.setColor(QPalette::Base, base);
  p.setColor(QPalette::AlternateBase, window);
  p.setColor(QPalette::Text, text);
  p.setColor(QPalette::Button, button);
  p.setColor(QPalette::ButtonText, text);
  p.setColor(QPalette::ToolTipBase, button);
  p.setColor(QPalette::ToolTipText, text);
  p.setColor(QPalette::Highlight, QColor("#3d6fb4"));
  p.setColor(QPalette::HighlightedText, Qt::white);
  p.setColor(QPalette::PlaceholderText, QColor("#9aa0aa"));
  p.setColor(QPalette::Disabled, QPalette::Text, QColor("#6b717b"));
  p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#6b717b"));
  app.setPalette(p);
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  app.setApplicationName("album-lite");
  app.setOrganizationName("album-lite");
  applyDarkTheme(app);

  // album-lite project.album  → opens that project.
  // album-lite photo1.jpg folder/ … → imports on launch.
  QStringList args = app.arguments();
  args.removeFirst();
  album::ui::Options options;
  QStringList photos;
  for (const QString& a : std::as_const(args)) {
    if (options.project.isEmpty() && a.endsWith(".album", Qt::CaseInsensitive)) options.project = a;
    else photos << a;
  }

  album::ui::MainWindow window(std::move(options));
  window.show();
  if (!photos.isEmpty()) window.importPaths(photos);
  return app.exec();
}
