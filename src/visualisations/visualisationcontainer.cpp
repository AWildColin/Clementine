/* This file is part of Clementine.

   Clementine is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Clementine is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Clementine.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "projectmvisualisation.h"
#include "visualisationcontainer.h"
#include "visualisationoverlay.h"
#include "engines/gstengine.h"
#include "ui/iconloader.h"

#include <QHBoxLayout>
#include <QSettings>
#include <QGLWidget>
#include <QtDebug>
#include <QGraphicsProxyWidget>
#include <QMenu>
#include <QSignalMapper>

#include <QLabel>

const char* VisualisationContainer::kSettingsGroup = "Visualisations";
const int VisualisationContainer::kDefaultWidth = 828;
const int VisualisationContainer::kDefaultHeight = 512;
const int VisualisationContainer::kDefaultFps = 35;

VisualisationContainer::VisualisationContainer(QWidget *parent)
  : QGraphicsView(parent),
    engine_(NULL),
    vis_(new ProjectMVisualisation(this)),
    overlay_(new VisualisationOverlay),
    menu_(new QMenu(this)),
    fps_(kDefaultFps)
{
  setWindowTitle(tr("Clementine Visualisation"));

  // Set up the graphics view
  setScene(vis_);
  setViewport(new QGLWidget(QGLFormat(QGL::SampleBuffers)));
  setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setFrameStyle(QFrame::NoFrame);

  // Add the overlay
  overlay_proxy_ = scene()->addWidget(overlay_);
  connect(overlay_, SIGNAL(OpacityChanged(qreal)), SLOT(ChangeOverlayOpacity(qreal)));
  connect(overlay_, SIGNAL(ShowPopupMenu(QPoint)), SLOT(ShowPopupMenu(QPoint)));
  ChangeOverlayOpacity(0.0);

  // Load settings
  QSettings s;
  s.beginGroup(kSettingsGroup);
  if (!restoreGeometry(s.value("geometry").toByteArray())) {
    resize(kDefaultWidth, kDefaultHeight);
  }
  fps_ = s.value("fps", kDefaultFps).toInt();

  SizeChanged();

  // Settings menu
  menu_->addAction(IconLoader::Load("view-fullscreen"), tr("Toggle fullscreen"),
                   this, SLOT(ToggleFullscreen()));

  QMenu* fps_menu = menu_->addMenu(tr("Framerate"));
  QSignalMapper* fps_mapper = new QSignalMapper(this);
  QActionGroup* fps_group = new QActionGroup(this);
  AddMenuItem(tr("Low (15 fps)"), 15, fps_, fps_group, fps_mapper);
  AddMenuItem(tr("Medium (25 fps)"), 25, fps_, fps_group, fps_mapper);
  AddMenuItem(tr("High (35 fps)"), 35, fps_, fps_group, fps_mapper);
  AddMenuItem(tr("Super high (60 fps)"), 60, fps_, fps_group, fps_mapper);
  fps_menu->addActions(fps_group->actions());
  connect(fps_mapper, SIGNAL(mapped(int)), SLOT(SetFps(int)));

  menu_->addSeparator();
  menu_->addAction(IconLoader::Load("application-exit"), tr("Close visualisation"),
                   this, SLOT(hide()));
}

void VisualisationContainer::AddMenuItem(const QString &name, int value, int def,
                                         QActionGroup* group, QSignalMapper *mapper) {
  QAction* action = group->addAction(name);
  action->setCheckable(true);
  action->setChecked(value == def);
  mapper->setMapping(action, value);
  connect(action, SIGNAL(triggered()), mapper, SLOT(map()));
}

void VisualisationContainer::SetEngine(GstEngine* engine) {
  engine_ = engine;
  if (isVisible())
    engine_->AddBufferConsumer(vis_);
}

void VisualisationContainer::showEvent(QShowEvent* e) {
  QGraphicsView::showEvent(e);
  update_timer_.start(1000 / fps_, this);

  if (engine_)
    engine_->AddBufferConsumer(vis_);
}

void VisualisationContainer::hideEvent(QHideEvent* e) {
  QGraphicsView::hideEvent(e);
  update_timer_.stop();

  if (engine_)
    engine_->RemoveBufferConsumer(vis_);
}

void VisualisationContainer::resizeEvent(QResizeEvent* e) {
  QGraphicsView::resizeEvent(e);
  SizeChanged();
}

void VisualisationContainer::SizeChanged() {
  // Save the geometry
  QSettings s;
  s.beginGroup(kSettingsGroup);
  s.setValue("geometry", saveGeometry());

  // Resize the scene
  if (scene())
    scene()->setSceneRect(QRect(QPoint(0, 0), size()));

  // Resize the overlay
  overlay_->resize(size());
}

void VisualisationContainer::timerEvent(QTimerEvent* e) {
  QGraphicsView::timerEvent(e);
  if (e->timerId() == update_timer_.timerId())
    scene()->update();
}

void VisualisationContainer::SetActions(QAction *previous, QAction *play_pause,
                                        QAction *stop, QAction *next) {
  overlay_->SetActions(previous, play_pause, stop, next);
}

void VisualisationContainer::SongMetadataChanged(const Song &metadata) {
  overlay_->SetSongTitle(QString("%1 - %2").arg(metadata.artist(), metadata.title()));
}

void VisualisationContainer::Stopped() {
  overlay_->SetSongTitle(tr("Clementine"));
}

void VisualisationContainer::ChangeOverlayOpacity(qreal value) {
  overlay_proxy_->setOpacity(value);

  // Hide the cursor if the overlay is hidden
  if (value < 0.5)
    setCursor(Qt::BlankCursor);
  else
    unsetCursor();
}

void VisualisationContainer::enterEvent(QEvent* e) {
  QGraphicsView::enterEvent(e);
  overlay_->SetVisible(true);
}

void VisualisationContainer::leaveEvent(QEvent* e) {
  QGraphicsView::leaveEvent(e);
  overlay_->SetVisible(false);
}

void VisualisationContainer::mouseMoveEvent(QMouseEvent* e) {
  QGraphicsView::mouseMoveEvent(e);
  overlay_->SetVisible(true);
}

void VisualisationContainer::mouseDoubleClickEvent(QMouseEvent* e) {
  QGraphicsView::mouseDoubleClickEvent(e);
  ToggleFullscreen();
}

void VisualisationContainer::contextMenuEvent(QContextMenuEvent *event) {
  QGraphicsView::contextMenuEvent(event);
  ShowPopupMenu(event->pos());
}

void VisualisationContainer::ToggleFullscreen() {
  setWindowState(windowState() ^ Qt::WindowFullScreen);
}

void VisualisationContainer::SetFps(int fps) {
  fps_ = fps;

  // Save settings
  QSettings s;
  s.beginGroup(kSettingsGroup);
  s.setValue("fps", fps_);

  update_timer_.stop();
  update_timer_.start(1000 / fps_, this);
}

void VisualisationContainer::ShowPopupMenu(const QPoint &pos) {
  menu_->popup(mapToGlobal(pos));
}
