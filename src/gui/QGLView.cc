/*
 *  OpenSCAD (www.openscad.org)
 *  Copyright (C) 2009-2011 Clifford Wolf <clifford@clifford.at> and
 *                          Marius Kintel <marius@kintel.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  As a special exception, you have permission to link this program
 *  with the CGAL library and distribute executables, as long as you
 *  follow the requirements of the GNU GPL in regard to all of the
 *  software in the executable aside from CGAL.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "gui/QGLView.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <QtCore/qpoint.h>

#include "core/Selection.h"
#include "geometry/linalg.h"
#include "gui/qtgettext.h"
#include "gui/Preferences.h"
#include "glview/Renderer.h"
#include "utils/degree_trig.h"
#include "utils/scope_guard.hpp"
#if defined(USE_GLEW) || defined(OPENCSG_GLEW)
#include "glview/glew-utils.h"
#endif

#include <QImage>
#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QWidget>
#include <iostream>
#include <QApplication>
#include <QWheelEvent>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QErrorMessage>
#ifdef USE_GLAD
#include <QOpenGLContext>
#endif
#include "gui/OpenCSGWarningDialog.h"

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#ifdef ENABLE_OPENCSG
#include <opencsg.h>
#endif

#include "gui/qt-obsolete.h"
#include "gui/Measurement.h"

namespace {

QSurfaceFormat compatibleWidgetFormat()
{
  auto format = QSurfaceFormat::defaultFormat();
  format.setRenderableType(QSurfaceFormat::OpenGL);
  format.setProfile(QSurfaceFormat::CompatibilityProfile);
  if (format.depthBufferSize() < 24) format.setDepthBufferSize(24);
  if (format.stencilBufferSize() < 8) format.setStencilBufferSize(8);
  return format;
}

}  // namespace

QGLView::QGLView(QWidget *parent) : QOpenGLWidget(parent)
{
  setFormat(compatibleWidgetFormat());
  init();
}

QGLView::~QGLView()
{
  // Just to make sure we can call GL functions in the supertype destructor
  makeCurrent();
}

void QGLView::init()
{
  resetView();

  this->mouse_drag_active = false;
  this->statusLabel = nullptr;

  setMouseTracking(true);
  // Clicking the view gives it the keyboard, for typing gizmo values.
  setFocusPolicy(Qt::ClickFocus);
}

void QGLView::resetView()
{
  cam.resetView();
}

void QGLView::viewAll()
{
  if (auto renderer = this->getRenderer()) {
    auto bbox = renderer->getBoundingBox();
    cam.autocenter = true;
    cam.viewAll(renderer->getBoundingBox());
  }
}

void QGLView::initializeGL()
{
#if defined(USE_GLEW) || defined(OPENCSG_GLEW)
  // Since OpenCSG requires glew, we need to initialize it.
  // ..in a separate compilation unit to avoid duplicate symbols with x.
  initializeGlew();
#endif
#ifdef USE_GLAD
  // We could ask for gladLoadGLES2UserPtr() here if we want to use GLES2+
  const auto version = gladLoadGLUserPtr(
    [](void *ctx, const char *name) -> GLADapiproc {
      return reinterpret_cast<QOpenGLContext *>(ctx)->getProcAddress(name);
    },
    this->context());
  if (version == 0) {
    std::cerr << "Unable to init GLAD" << std::endl;
    return;
  }
  PRINTDB("GLAD: Loaded OpenGL %d.%d", GLAD_VERSION_MAJOR(version) % GLAD_VERSION_MINOR(version));
#endif  // ifdef USE_GLAD

  PRINTD(gl_dump());

  GLView::initializeGL();

  this->selector = std::make_unique<MouseSelector>(this);

  emit initialized();
}

std::string QGLView::getRendererInfo() const
{
  std::ostringstream info;
  info << gl_dump();
  // Don't translate as translated text in the Library Info dialog is not wanted
  info << "\nQt graphics widget: QOpenGLWidget";
  auto qsf = this->format();
  auto rbits = qsf.redBufferSize();
  auto gbits = qsf.greenBufferSize();
  auto bbits = qsf.blueBufferSize();
  auto abits = qsf.alphaBufferSize();
  auto dbits = qsf.depthBufferSize();
  auto sbits = qsf.stencilBufferSize();
  info << boost::format("\nQSurfaceFormat: RGBA(%d%d%d%d), depth(%d), stencil(%d)\n\n") % rbits % gbits %
            bbits % abits % dbits % sbits;
  info << gl_extensions_dump();
  return info.str();
}

#ifdef ENABLE_OPENCSG
void QGLView::display_opencsg_warning()
{
  if (GlobalPreferences::inst()->getValue("advanced/opencsg_show_warning").toBool()) {
    QTimer::singleShot(0, this, &QGLView::display_opencsg_warning_dialog);
  }
}

void QGLView::display_opencsg_warning_dialog()
{
  auto dialog = new OpenCSGWarningDialog(this);

  QString message =
    _("Warning: Missing OpenGL capabilities for OpenCSG - OpenCSG has been disabled.\n\n");
  message +=
    _("It is highly recommended to use OpenSCAD on a system with "
      "OpenGL 2.0 or later.\n"
      "Your renderer information is as follows:\n");
#if defined(USE_GLEW) || defined(OPENCSG_GLEW)
  QString rendererinfo(_("GLEW version %1\n%2 (%3)\nOpenGL version %4\n"));
  message +=
    rendererinfo.arg((const char *)glewGetString(GLEW_VERSION), (const char *)glGetString(GL_RENDERER),
                     (const char *)glGetString(GL_VENDOR), (const char *)glGetString(GL_VERSION));
#endif
#ifdef USE_GLAD
  QString rendererinfo(_("GLAD version %1\n%2 (%3)\nOpenGL version %4\n"));
  message +=
    rendererinfo.arg(GLAD_GENERATOR_VERSION, (const char *)glGetString(GL_RENDERER),
                     (const char *)glGetString(GL_VENDOR), (const char *)glGetString(GL_VERSION));
#endif
  dialog->setText(message);
  dialog->exec();
}
#endif  // ifdef ENABLE_OPENCSG

void QGLView::resizeGL(int w, int h)
{
  GLView::resizeGL(w, h);
  emit resized();
}

void QGLView::paintGL()
{
  GLView::paintGL();
  if (gizmo.visible && gizmo.active != TransformGizmo::None) drawGizmoReadout();

  if (statusLabel) {
    auto status = QString("%1 (%2x%3)")
                    .arg(QString::fromStdString(cam.statusText()))
                    .arg(size().rwidth())
                    .arg(size().rheight());
    statusLabel->setText(status);
  }
}

void QGLView::mousePressEvent(QMouseEvent *event)
{
  if (gizmo_input && !gizmo_dragging) gizmoCancel();
  if (event->button() == Qt::LeftButton && measure_state == Measurement::MEASURE_IDLE &&
      gizmoStartDrag(event->position())) {
    return;
  }

  if (!mouse_drag_active) {
    mouse_drag_moved = false;
  }

  mouse_drag_active = true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  last_mouse = event->globalPosition();
#else
  last_mouse = event->globalPos();
#endif
}

/*
 * Voodoo warning...
 *
 * This function selects the widget's OpenGL context (via this->makeCurrent()).
 * Because it's changing the OpenGL context, it seems polite to save and restore it.
 * That resolution seems correct, independent of the mysteries below.
 *
 * Let's call the widget's context W, and the alternate context that we are called with A.
 *
 * It's important that A is selected when we return (as it is when we enter), because
 * if it isn't then sometimes the subsequent mouseReleaseEvent is called with W, when it
 * is normally called with A.  When that happens, the object-selection magic in selectObject
 * messes up W, and rendering is forever after broken in that window.
 *
 * However, as hygienic as saving-and-restoring seems, the picture is still unsatisfying.
 *
 * Open questions:
 * - Why are these mouse event functions called with A, rather than being called with W?
 *   It's unsurprising that the selection magic needs its own GL context, but it seems like
 *   it should be the one that needs to explicitly select it, not this function.
 * - Where did A come from?
 * - Why does a subsequent mouseReleaseEvent call get called with W?
 * - Why does it only sometimes get called with W, and sometimes (correctly) with A?
 * - Why do later mouseReleaseEvent calls revert to being (correctly) called with A?
 * - Why does this only happen with right clicks?  With left clicks, this function
 *   changes the context, but it's OK again on the following mouseReleaseEvent.
 * - Why does this only happen when you click on empty space, and not when you click
 *   on the model?  Double clicks on the model are not detected as double clicks.
 *   Perhaps this is because the first click pops a menu and the second click is
 *   on the menu, not this widget.
 *
 * getGLContext() and setGLContext() are in a separate file, QGLView2.cc, so that this
 * file doesn't need a full declaration of QOpenGLContext.  <QOpenGLContext> is
 * incompatible with GLEW and causes compilation warnings.
 *
 * For future attention:
 * - This function should probably only react to left double clicks.  Right double clicks
 *   should probably be ignored.
 */
void QGLView::mouseDoubleClickEvent(QMouseEvent *event)
{
  QOpenGLContext *oldContext = getGLContext();
  this->makeCurrent();
  auto guard = sg::make_scope_guard([this, oldContext] {
    this->doneCurrent();
    setGLContext(oldContext);
  });

  setupCamera();

  int viewport[4];
  GLdouble modelview[16];
  GLdouble projection[16];

  glGetIntegerv(GL_VIEWPORT, viewport);
  glGetDoublev(GL_MODELVIEW_MATRIX, modelview);
  glGetDoublev(GL_PROJECTION_MATRIX, projection);

  const double dpi = this->getDPI();
  const double x = event->pos().x() * dpi;
  const double y = viewport[3] - event->pos().y() * dpi;
  GLfloat z = 0;

  glGetError();  // clear error state so we don't pick up previous errors
  glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &z);
  if (const auto glError = glGetError(); glError != GL_NO_ERROR) {
    if (statusLabel) {
      auto status = QString("Center View: OpenGL Error reading Pixel: %s")
                      .arg(QString::fromLocal8Bit((const char *)gluErrorString(glError)));
      statusLabel->setText(status);
    }
    return;
  }

  if (z == 1) {
    return;  // outside object
  }

  GLdouble px, py, pz;

  auto success = gluUnProject(x, y, z, modelview, projection, viewport, &px, &py, &pz);

  if (success == GL_TRUE) {
    cam.object_trans -= Vector3d(px, py, pz);
    update();
    emit cameraChanged();
  }
}

void QGLView::normalizeAngle(GLdouble& angle)
{
  while (angle < 0) angle += 360;
  while (angle > 360) angle -= 360;
}

void QGLView::mouseMoveEvent(QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  auto this_mouse = event->globalPosition();
#else
  auto this_mouse = event->globalPos();
#endif
  if (gizmo.active != TransformGizmo::None) {
    gizmo_last_pos = event->position();
    if (gizmo_dragging) gizmoUpdateDrag(event->position());
    else update();  // the readout follows the mouse while typing
    return;
  }
  if (gizmo.visible && !mouse_drag_active) {
    const int hover = gizmoHitHandle(event->position());
    if (hover != gizmo.hover) {
      gizmo.hover = hover;
      update();
    }
  }
  if (measure_state != Measurement::MEASURE_IDLE) {
    QPoint pt = event->pos();
    this->shown_obj = findObject(pt.x(), pt.y());
    update();
  }
  double dx = (this_mouse.x() - last_mouse.x()) * 0.7;
  double dy = (this_mouse.y() - last_mouse.y()) * 0.7;
  if (mouse_drag_active) {
    mouse_drag_moved = true;

    bool multipleButtonsPressed = false;
    int buttonIndex = -1;
    if (event->buttons() & Qt::LeftButton) {
      buttonIndex = 0;
    }
    if (event->buttons() & Qt::MiddleButton) {
      if (buttonIndex != -1) {
        multipleButtonsPressed = true;
      } else {
        buttonIndex = 1;
      }
    }
    if (event->buttons() & Qt::RightButton) {
      if (buttonIndex != -1) {
        multipleButtonsPressed = true;
      } else {
        buttonIndex = 2;
      }
    }
    int modifierIndex = 0;
    if (QApplication::keyboardModifiers() & Qt::ShiftModifier) {
      modifierIndex = 1;
    }
    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
      if (modifierIndex == 1) {
        modifierIndex = 3;  // Ctrl + Shift
      } else {
        modifierIndex = 2;
      }
    }

    if (buttonIndex != -1 && !multipleButtonsPressed) {
      float *selectedMouseActions =
        &this->mouseActions[MouseConfig::ACTION_DIMENSION * (buttonIndex + modifierIndex * 3)];

      // Rotation angles from mouse movement
      // First 6 elements to selectedMouseActions are interpreted as a row-major 3x2 matrix, which is
      // right-multiplied by (dx, dy)^T to produce the rotation angle increments.
      double rx = selectedMouseActions[0] * dx + selectedMouseActions[1] * dy;
      double ry = selectedMouseActions[2] * dx + selectedMouseActions[3] * dy;
      double rz = selectedMouseActions[4] * dx + selectedMouseActions[5] * dy;
      if (!(rx == 0.0 && ry == 0.0 && rz == 0.0)) {
        rotate(rx, ry, rz, true);
        normalizeAngle(cam.object_rot.x());
        normalizeAngle(cam.object_rot.y());
        normalizeAngle(cam.object_rot.z());
      }

      // Panning from mouse movement
      // Elements 6..12 of selectedMouseActions are interpreted as another row-major 3x2 matrix, which is
      // right-multiplied by (dx, dy)^T, and then scaled by the zoom, to produce the translation
      // increments.
      double mx = selectedMouseActions[6 + 0] * (dx / QWidget::width()) +
                  selectedMouseActions[6 + 1] * (dy / QWidget::height());
      double my = selectedMouseActions[6 + 2] * (dx / QWidget::width()) +
                  selectedMouseActions[6 + 3] * (dy / QWidget::height());
      double mz = selectedMouseActions[6 + 4] * (dx / QWidget::width()) +
                  selectedMouseActions[6 + 5] * (dy / QWidget::height());
      if (!(mx == 0.0 && my == 0.0 && mz == 0.0)) {
        mx *= 3.0 * cam.zoomValue();
        my *= 3.0 * cam.zoomValue();
        mz *= 3.0 * cam.zoomValue();
      }
      translate(mx, my, mz, true);

      // Zoom from mouse movement
      // Final 2 elements of selectedMouseActions are interpreted as a 2-dimensional vector. The inner
      // product of this is taken with (dx, dy)^T to produce the zoom increment.
      double dZoom = selectedMouseActions[12] * dx + selectedMouseActions[13] * dy;
      if (dZoom != 0.0) {
        dZoom *= 12.0;
        zoom(dZoom, true);
      }
    }
  }
  last_mouse = this_mouse;
}

void QGLView::mouseReleaseEvent(QMouseEvent *event)
{
  if (gizmo_dragging) {
    gizmo_dragging = false;
    if (gizmo_input) return;  // wait for Enter / Esc
    if (gizmo.hasEdit()) {
      gizmoCommit();
    } else {
      // A press on the selection without moving is a plain click.
      gizmoCancel();
      emit doLeftClick(event->pos());
    }
    return;
  }

  mouse_drag_active = false;
  releaseMouse();

  if (!mouse_drag_moved) {
    if (event->button() == Qt::RightButton) {
      QPoint point = event->pos();
      emit doRightClick(point);
    }
    if (event->button() == Qt::LeftButton) {
      QPoint point = event->pos();
      emit doLeftClick(point);
    }
  }
  mouse_drag_moved = false;
}

const QImage& QGLView::grabFrame()
{
  // Force reading from front buffer. Some configurations will read from the back buffer here.
  glReadBuffer(GL_FRONT);
  this->frame = grabFramebuffer();
  return this->frame;
}

bool QGLView::save(const char *filename) const
{
  return this->frame.save(filename, "PNG");
}

void QGLView::wheelEvent(QWheelEvent *event)
{
  const auto pos = Q_WHEEL_EVENT_POSITION(event);
  const int v = event->angleDelta().y();
  if (QApplication::keyboardModifiers() & Qt::ShiftModifier) {
    zoomFov(v);
  } else if (this->mouseCentricZoom) {
    zoomCursor(pos.x(), pos.y(), v);
  } else {
    zoom(v, true);
  }
}

void QGLView::ZoomIn()
{
  zoom(120, true);
}

void QGLView::ZoomOut()
{
  zoom(-120, true);
}

void QGLView::zoom(double v, bool relative)
{
  this->cam.zoom(v, relative);
  update();
  emit cameraChanged();
}

void QGLView::zoomFov(double v)
{
  this->cam.setVpf(this->cam.fovValue() * pow(0.9, v / 120.0));
  update();
  emit cameraChanged();
}

void QGLView::zoomCursor(int x, int y, int zoom)
{
  const auto old_dist = cam.zoomValue();
  this->cam.zoom(zoom, true);
  const auto dist = cam.zoomValue();
  const auto ratio = old_dist / dist - 1.0;
  // screen coordinates from -1 to 1
  const auto screen_x = 2.0 * (x + 0.5) / this->cam.pixel_width - 1.0;
  const auto screen_y = 1.0 - 2.0 * (y + 0.5) / this->cam.pixel_height;
  const auto height = dist * tan_degrees(cam.fov / 2);
  const auto mx = ratio * screen_x * (aspectratio * height);
  const auto mz = ratio * screen_y * height;
  translate(-mx, 0, -mz, true);
}

void QGLView::setOrthoMode(bool enabled)
{
  if (enabled) this->cam.setProjection(Camera::ProjectionType::ORTHOGONAL);
  else this->cam.setProjection(Camera::ProjectionType::PERSPECTIVE);
}

void QGLView::translate(double x, double y, double z, bool relative, bool viewPortRelative)
{
  Matrix3d aax, aay, aaz;
  aax = angle_axis_degrees(-cam.object_rot.x(), Vector3d::UnitX());
  aay = angle_axis_degrees(-cam.object_rot.y(), Vector3d::UnitY());
  aaz = angle_axis_degrees(-cam.object_rot.z(), Vector3d::UnitZ());
  Matrix3d tm3 = aaz * aay * aax;

  Matrix4d tm = Matrix4d::Identity();
  if (viewPortRelative) {
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 3; ++j) {
        tm(j, i) = tm3(j, i);
      }
    }
  }

  Matrix4d vec;
  // clang-format off
  vec << 0, 0, 0, x,
         0, 0, 0, y,
         0, 0, 0, z,
         0, 0, 0, 1;
  // clang-format on
  tm = tm * vec;
  double f = relative ? 1 : 0;
  cam.object_trans.x() = f * cam.object_trans.x() + tm(0, 3);
  cam.object_trans.y() = f * cam.object_trans.y() + tm(1, 3);
  cam.object_trans.z() = f * cam.object_trans.z() + tm(2, 3);
  update();
  emit cameraChanged();
}

void QGLView::rotate(double x, double y, double z, bool relative)
{
  double f = relative ? 1 : 0;
  cam.object_rot.x() = f * cam.object_rot.x() + x;
  cam.object_rot.y() = f * cam.object_rot.y() + y;
  cam.object_rot.z() = f * cam.object_rot.z() + z;
  normalizeAngle(cam.object_rot.x());
  normalizeAngle(cam.object_rot.y());
  normalizeAngle(cam.object_rot.z());
  update();
  emit cameraChanged();
}

void QGLView::rotate2(double x, double y, double z)
{
  // This vector describes the rotation.
  // The direction of the vector is the angle around which to rotate, and
  // the length of the vector is the angle by which to rotate
  Vector3d rot = Vector3d(-x, -y, -z);

  // get current rotation matrix
  Matrix3d aax, aay, aaz, rmx;
  aax = angle_axis_degrees(-cam.object_rot.x(), Vector3d::UnitX());
  aay = angle_axis_degrees(-cam.object_rot.y(), Vector3d::UnitY());
  aaz = angle_axis_degrees(-cam.object_rot.z(), Vector3d::UnitZ());
  rmx = aaz * (aay * aax);

  // rotate
  rmx = rmx * angle_axis_degrees(rot.norm(), rot.normalized());

  // back to euler
  // see: http://staff.city.ac.uk/~sbbh653/publications/euler.pdf
  double theta, psi, phi;
  if (abs(rmx(2, 0)) != 1) {
    theta = -asin_degrees(rmx(2, 0));
    psi = atan2_degrees(rmx(2, 1) / cos_degrees(theta), rmx(2, 2) / cos_degrees(theta));
    phi = atan2_degrees(rmx(1, 0) / cos_degrees(theta), rmx(0, 0) / cos_degrees(theta));
  } else {
    phi = 0;
    if (rmx(2, 0) == -1) {
      theta = 90;
      psi = phi + atan2_degrees(rmx(0, 1), rmx(0, 2));
    } else {
      theta = -90;
      psi = -phi + atan2_degrees(-rmx(0, 1), -rmx(0, 2));
    }
  }

  cam.object_rot.x() = -psi;
  cam.object_rot.y() = -theta;
  cam.object_rot.z() = -phi;

  normalizeAngle(cam.object_rot.x());
  normalizeAngle(cam.object_rot.y());
  normalizeAngle(cam.object_rot.z());

  update();
  emit cameraChanged();
}

std::vector<SelectedObject> QGLView::findObject(int mouse_x, int mouse_y)
{
  int viewport[4] = {0, 0, 0, 0};
  double posXF, posYF, posZF;
  double posXN, posYN, posZN;
  viewport[2] = size().rwidth();
  viewport[3] = size().rheight();

  GLdouble winX = mouse_x;
  GLdouble winY = viewport[3] - mouse_y;

  gluUnProject(winX, winY, 1, this->modelview, this->projection, viewport, &posXF, &posYF, &posZF);
  gluUnProject(winX, winY, -1, this->modelview, this->projection, viewport, &posXN, &posYN, &posZN);
  Vector3d far_pt(posXF, posYF, posZF);
  Vector3d near_pt(posXN, posYN, posZN);

  Vector3d testpt(0, 0, 0);
  std::vector<SelectedObject> result;
  auto renderer = this->getRenderer();
  if (renderer == nullptr) return result;
  result = renderer->findModelObject(near_pt, far_pt, mouse_x, mouse_y, cam.zoomValue() / 300);
  return result;
}

void QGLView::selectPoint(int mouse_x, int mouse_y)
{
  std::vector<SelectedObject> obj = findObject(mouse_x, mouse_y);
  if (obj.size() == 1) {
    this->selected_obj.push_back(obj[0]);
    update();
  }
}

QPointF QGLView::projectToScreen(const Vector3d& p) const
{
  const Eigen::Map<const Eigen::Matrix4d> model(this->modelview);
  const Eigen::Map<const Eigen::Matrix4d> proj(this->projection);
  const Eigen::Vector4d clip = proj * model * Eigen::Vector4d(p.x(), p.y(), p.z(), 1.0);
  if (std::fabs(clip.w()) < 1e-12) return {-1e9, -1e9};
  const double nx = clip.x() / clip.w(), ny = clip.y() / clip.w();
  return {(nx + 1.0) * 0.5 * width(), (1.0 - ny) * 0.5 * height()};
}

void QGLView::screenRay(const QPointF& pos, Vector3d& nearPt, Vector3d& farPt) const
{
  const Eigen::Map<const Eigen::Matrix4d> model(this->modelview);
  const Eigen::Map<const Eigen::Matrix4d> proj(this->projection);
  const Eigen::Matrix4d inv = (proj * model).inverse();
  const double nx = 2.0 * pos.x() / width() - 1.0;
  const double ny = 1.0 - 2.0 * pos.y() / height();
  const Eigen::Vector4d n = inv * Eigen::Vector4d(nx, ny, -1.0, 1.0);
  const Eigen::Vector4d f = inv * Eigen::Vector4d(nx, ny, 1.0, 1.0);
  nearPt = n.head<3>() / n.w();
  farPt = f.head<3>() / f.w();
}

bool QGLView::rayHitsPlaneZ(const QPointF& pos, double z, Vector3d& hit) const
{
  Vector3d nearPt, farPt;
  screenRay(pos, nearPt, farPt);
  const Vector3d dir = farPt - nearPt;
  // Looking (almost) edge-on at the plane: no stable intersection.
  if (std::fabs(dir.z()) < 1e-6 * dir.norm()) return false;
  const double t = (z - nearPt.z()) / dir.z();
  hit = nearPt + t * dir;
  return true;
}

bool QGLView::axisFacesViewer(int axis) const
{
  // Eye space looks down -Z: a positive z component points at the viewer.
  const Eigen::Map<const Eigen::Matrix4d> model(this->modelview);
  return (model.topLeftCorner<3, 3>() * Vector3d::Unit(axis)).z() > 0.0;
}

namespace {

double distanceToSegment(const QPointF& p, const QPointF& a, const QPointF& b)
{
  const QPointF ab = b - a;
  const double len2 = QPointF::dotProduct(ab, ab);
  const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0) : 0.0;
  const QPointF d = p - (a + t * ab);
  return std::sqrt(QPointF::dotProduct(d, d));
}

QString formatValue(double v, int decimals)
{
  QString s = QString::number(v, 'f', decimals);
  if (s.contains('.')) {
    while (s.endsWith('0')) s.chop(1);
    if (s.endsWith('.')) s.chop(1);
  }
  return s == "-0" ? "0" : s;
}

}  // namespace

int QGLView::gizmoHitHandle(const QPointF& pos) const
{
  using G = TransformGizmo;
  if (!gizmo.visible) return G::None;
  const Vector3d c = gizmoOrigin();
  const QPointF sc = projectToScreen(c);
  int best = G::None;
  double bestDist = 10.0;  // pixels

  for (int axis = 0; axis < 3; ++axis) {
    // Size handles win ties: they are small.
    Vector3d face = c;
    face[axis] = gizmo.bbox.max()[axis];
    const QPointF d = pos - projectToScreen(face);
    const double sizeDist = std::sqrt(QPointF::dotProduct(d, d)) - 3.0;
    if (sizeDist < bestDist) {
      bestDist = sizeDist;
      best = G::SizeX + axis;
    }

    Vector3d tip = c;
    tip[axis] += gizmoHandleLength();
    const QPointF st = projectToScreen(tip);
    const QPointF seg = st - sc;
    if (QPointF::dotProduct(seg, seg) >= 25.0) {  // not pointing at the viewer
      // Grab the outer 70% of the arrow, so the center stays a plane drag.
      const double moveDist = distanceToSegment(pos, sc + 0.3 * seg, st);
      if (moveDist < bestDist) {
        bestDist = moveDist;
        best = G::MoveX + axis;
      }
    }

    const Vector3d u = Vector3d::Unit((axis + 1) % 3), v = Vector3d::Unit((axis + 2) % 3);
    QPointF prev = projectToScreen(c + gizmoRingRadius() * u);
    for (int i = 1; i <= 48; ++i) {
      const double a = i * 2.0 * M_PI / 48.0;
      const QPointF next = projectToScreen(c + gizmoRingRadius() * (std::cos(a) * u + std::sin(a) * v));
      const double ringDist = distanceToSegment(pos, prev, next) + 1.0;
      if (ringDist < bestDist) {
        bestDist = ringDist;
        best = G::RotateX + axis;
      }
      prev = next;
    }
  }
  return best;
}

void QGLView::gizmoBeginEdit(int handle, const QPointF& pos)
{
  gizmo.active = handle;
  gizmo.resetEdit();
  gizmo_input = false;
  gizmo_input_text[0].clear();
  gizmo_input_text[1].clear();
  gizmo_input_field = 0;
  gizmo_press_pos = pos;
  gizmo_last_pos = pos;
  if (TransformGizmo::isRotate(handle)) {
    const QPointF d = pos - projectToScreen(gizmoOrigin());
    gizmo_rot_last = std::atan2(-d.y(), d.x()) * 180.0 / M_PI;
    gizmo_rot_accum = 0.0;
  }
  update();
}

bool QGLView::gizmoStartDrag(const QPointF& pos)
{
  using G = TransformGizmo;
  if (!gizmo.visible) return false;

  int handle = gizmoHitHandle(pos);
  if (handle == G::None && !gizmoSelectionIndices.empty()) {
    const int index = pickObject(pos.toPoint());
    if (gizmoSelectionIndices.count(index)) {
      gizmo_plane_z = gizmo.bbox.min().z();
      if (rayHitsPlaneZ(pos, gizmo_plane_z, gizmo_plane_start)) handle = G::MovePlane;
    }
  }
  if (handle == G::None) return false;

  gizmoBeginEdit(handle, pos);
  gizmo_dragging = true;
  return true;
}

void QGLView::gizmoUpdateDrag(const QPointF& pos)
{
  using G = TransformGizmo;
  if (gizmo_input) {
    update();  // typed values win over the mouse
    return;
  }
  const bool fine = QApplication::keyboardModifiers() & Qt::ControlModifier;
  const double step = G::isRotate(gizmo.active) ? (fine ? 1.0 : 15.0) : (fine ? 0.1 : 1.0);
  auto snap = [step](double v) { return std::round(v / step) * step; };
  const int axis = G::axisOf(gizmo.active);

  // World distance moved along the handle axis, measured on the handle as
  // drawn at press time.
  auto alongAxis = [&]() {
    const Vector3d o = gizmoOrigin();
    Vector3d tip = o;
    tip[axis] += gizmoHandleLength();
    const QPointF so = projectToScreen(o), seg = projectToScreen(tip) - so;
    const double len2 = QPointF::dotProduct(seg, seg);
    return len2 < 1.0 ? 0.0 : QPointF::dotProduct(pos - gizmo_press_pos, seg) / len2 * gizmoHandleLength();
  };

  if (gizmo.active == G::MovePlane) {
    Vector3d hit;
    if (!rayHitsPlaneZ(pos, gizmo_plane_z, hit)) return;
    gizmo.offset = Vector3d(snap(hit.x() - gizmo_plane_start.x()), snap(hit.y() - gizmo_plane_start.y()), 0);
  } else if (G::isMove(gizmo.active)) {
    gizmo.offset = Vector3d::Zero();
    gizmo.offset[axis] = snap(alongAxis());
  } else if (G::isRotate(gizmo.active)) {
    const QPointF d = pos - projectToScreen(gizmoOrigin());
    const double raw = std::atan2(-d.y(), d.x()) * 180.0 / M_PI;
    double delta = raw - gizmo_rot_last;
    while (delta > 180.0) delta -= 360.0;
    while (delta < -180.0) delta += 360.0;
    gizmo_rot_accum += delta;
    gizmo_rot_last = raw;
    // Counter-clockwise on screen is positive when the axis faces the viewer.
    gizmo.angle = snap(axisFacesViewer(axis) ? gizmo_rot_accum : -gizmo_rot_accum);
  } else if (G::isSize(gizmo.active)) {
    const Vector3d old = gizmo.bboxSize();
    const double length = std::max(step, snap(old[axis] + alongAxis()));
    if (QApplication::keyboardModifiers() & Qt::ShiftModifier && old[axis] > 0) {
      gizmo.size = old * (length / old[axis]);  // uniform
    } else {
      gizmo.size = old;
      gizmo.size[axis] = length;
    }
  }
  update();
}

// Typed values replace the mouse values of the edit in progress.
void QGLView::gizmoApplyInput()
{
  using G = TransformGizmo;
  auto parse = [](QString text, bool *percent) {
    text = text.trimmed().replace(',', '.');
    *percent = text.endsWith('%');
    if (*percent) text.chop(1);
    bool ok = false;
    const double v = text.toDouble(&ok);
    return ok ? std::optional<double>(v) : std::nullopt;
  };
  bool percent = false;
  const auto v0 = parse(gizmo_input_text[0], &percent);
  const int axis = G::axisOf(gizmo.active);

  if (gizmo.active == G::MovePlane) {
    bool unused = false;
    const auto v1 = parse(gizmo_input_text[1], &unused);
    gizmo.offset = Vector3d(v0.value_or(0.0), v1.value_or(0.0), 0.0);
  } else if (G::isMove(gizmo.active)) {
    gizmo.offset = Vector3d::Zero();
    gizmo.offset[axis] = v0.value_or(0.0);
  } else if (G::isRotate(gizmo.active)) {
    gizmo.angle = v0.value_or(0.0);
  } else if (G::isSize(gizmo.active)) {
    const Vector3d old = gizmo.bboxSize();
    gizmo.size = old;
    if (v0) {
      const double length = percent ? old[axis] * *v0 / 100.0 : *v0;
      if (length > 0) gizmo.size[axis] = length;
    }
  }
  update();
}

void QGLView::gizmoCommit()
{
  using G = TransformGizmo;
  const int handle = gizmo.active;
  gizmo_dragging = false;
  gizmo_input = false;
  if (!gizmo.hasEdit()) {
    gizmoCancel();
    return;
  }
  // The ghost stays visible until the re-rendered model replaces it.
  if (G::isMove(handle)) {
    emit gizmoCommitted(handle, gizmo.offset.x(), gizmo.offset.y(), gizmo.offset.z());
  } else if (G::isRotate(handle)) {
    emit gizmoCommitted(handle, gizmo.angle, 0.0, 0.0);
  } else if (G::isSize(handle)) {
    const Vector3d old = gizmo.bboxSize();
    Vector3d f = Vector3d::Ones();
    for (int i = 0; i < 3; ++i) {
      if (old[i] > 1e-12) f[i] = gizmo.size[i] / old[i];
    }
    emit gizmoCommitted(handle, f.x(), f.y(), f.z());
  }
}

void QGLView::gizmoCancel()
{
  gizmo.active = TransformGizmo::None;
  gizmo.resetEdit();
  gizmo_dragging = false;
  gizmo_input = false;
  update();
}

void QGLView::keyPressEvent(QKeyEvent *event)
{
  using G = TransformGizmo;
  const QString text = event->text();
  const bool valueChar = !text.isEmpty() && QString("0123456789.,-%").contains(text[0]);

  // Typing while hovering a handle starts an edit on it.
  if (gizmo.active == G::None && gizmo.visible && gizmo.hover != G::None && valueChar) {
    gizmoBeginEdit(gizmo.hover, gizmo_last_pos.isNull() ? QPointF(width() / 2.0, height() / 2.0)
                                                         : gizmo_last_pos);
    if (gizmo.active == G::MovePlane) gizmo_plane_z = gizmo.bbox.min().z();
  }
  if (gizmo.active == G::None) {
    QOpenGLWidget::keyPressEvent(event);
    return;
  }

  switch (event->key()) {
  case Qt::Key_Escape: gizmoCancel(); break;
  case Qt::Key_Return:
  case Qt::Key_Enter:
    if (gizmo_input) gizmoApplyInput();
    if (!gizmo_dragging) gizmoCommit();
    break;
  case Qt::Key_Tab:
  case Qt::Key_Backtab:
    if (gizmo.active == G::MovePlane) {
      gizmo_input = true;
      gizmo_input_field ^= 1;
      update();
    }
    break;
  case Qt::Key_Backspace:
    if (gizmo_input) {
      gizmo_input_text[gizmo_input_field].chop(1);
      gizmoApplyInput();
    }
    break;
  default:
    if (!valueChar) {
      QOpenGLWidget::keyPressEvent(event);
      return;
    }
    if (!gizmo_input) {
      gizmo_input = true;
      gizmo_input_text[0].clear();
      gizmo_input_text[1].clear();
    }
    gizmo_input_text[gizmo_input_field] += text;
    gizmoApplyInput();
    break;
  }
  event->accept();
}

bool QGLView::focusNextPrevChild(bool next)
{
  // Tab switches the X/Y field of a typed plane move.
  if (gizmo.active != TransformGizmo::None) return false;
  return QOpenGLWidget::focusNextPrevChild(next);
}

// Value box next to the mouse: what the edit does, and the typed value.
void QGLView::drawGizmoReadout()
{
  using G = TransformGizmo;
  static const char *axisNames[] = {"X", "Y", "Z"};
  const int axis = G::axisOf(gizmo.active);

  struct Field {
    QString label;
    QString value;
    QString unit;
  };
  QString title;
  std::vector<Field> fields;
  auto typed = [this](int field, const QString& value) {
    if (!gizmo_input) return value;
    return gizmo_input_text[field] + (gizmo_input_field == field ? "|" : "");
  };
  if (gizmo.active == G::MovePlane) {
    title = "Move";
    fields.push_back({"X", typed(0, formatValue(gizmo.offset.x(), 2)), "mm"});
    fields.push_back({"Y", typed(1, formatValue(gizmo.offset.y(), 2)), "mm"});
  } else if (G::isMove(gizmo.active)) {
    title = "Move";
    fields.push_back({axisNames[axis], typed(0, formatValue(gizmo.offset[axis], 2)), "mm"});
  } else if (G::isRotate(gizmo.active)) {
    title = "Rotate";
    fields.push_back({axisNames[axis], typed(0, formatValue(gizmo.angle, 2)), QString::fromUtf8("\u00b0")});
  } else if (G::isSize(gizmo.active)) {
    const double old = gizmo.bboxSize()[axis];
    title = "Size";
    fields.push_back({axisNames[axis], typed(0, formatValue(gizmo.size[axis], 2)), "mm"});
    if (old > 0) title += QString("  (%1%)").arg(formatValue(gizmo.size[axis] / old * 100.0, 1));
  }
  const QString hint = gizmo.active == G::MovePlane ? "type values - Tab: next - Enter: apply - Esc: cancel"
                                                    : "type a value - Enter: apply - Esc: cancel";

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  QFont font = painter.font();
  font.setBold(true);
  const QFontMetrics fm(font);
  QFont small = font;
  small.setBold(false);
  small.setPointSizeF(font.pointSizeF() * 0.85);
  const QFontMetrics fms(small);

  const int pad = 6, gap = 10, lineH = fm.height();
  std::vector<int> fieldWidths;
  int fieldsWidth = 0;
  for (const auto& f : fields) {
    const int w = fm.horizontalAdvance(f.label + " " + f.value + " " + f.unit) + 2 * pad;
    fieldWidths.push_back(w);
    fieldsWidth += w + gap;
  }
  const int width = std::max({fm.horizontalAdvance(title), fieldsWidth - gap, fms.horizontalAdvance(hint)}) + 2 * pad;
  const int height = lineH * 2 + fms.height() + 4 * pad;

  QPointF origin = gizmo_last_pos + QPointF(18, 18);
  origin.setX(std::min(origin.x(), this->width() - width - 4.0));
  origin.setY(std::min(origin.y(), this->height() - height - 4.0));
  const QRectF box(origin, QSizeF(width, height));

  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(25, 25, 28, 225));
  painter.drawRoundedRect(box, 6, 6);

  painter.setFont(font);
  painter.setPen(QColor(235, 235, 235));
  painter.drawText(QPointF(box.left() + pad, box.top() + pad + fm.ascent()), title);

  double x = box.left() + pad;
  const double y = box.top() + 2 * pad + lineH;
  for (size_t i = 0; i < fields.size(); ++i) {
    const QRectF fieldBox(x, y, fieldWidths[i], lineH + pad / 2);
    const bool current = gizmo_input && static_cast<int>(i) == gizmo_input_field;
    painter.setPen(Qt::NoPen);
    painter.setBrush(current ? QColor(255, 210, 80) : QColor(60, 60, 66));
    painter.drawRoundedRect(fieldBox, 4, 4);
    painter.setPen(current ? QColor(20, 20, 20) : QColor(245, 245, 245));
    painter.drawText(fieldBox, Qt::AlignCenter, fields[i].label + " " + fields[i].value + " " + fields[i].unit);
    x += fieldWidths[i] + gap;
  }

  painter.setFont(small);
  painter.setPen(QColor(170, 170, 170));
  painter.drawText(QPointF(box.left() + pad, box.bottom() - pad - fms.descent()), hint);
}

int QGLView::pickObject(QPoint position)
{
  if (!isValid()) return -1;

  if (this->getRenderer()) {
    this->makeCurrent();
    auto guard = sg::make_scope_guard([this]() { this->doneCurrent(); });

    // Update the selector with the right image size
    this->selector->reset(this);

    return this->selector->select(this->getRenderer(), position.x(), position.y());
  }
  return -1;
}
