#pragma once

#include "glview/system-gl.h"
#include "core/Selection.h"
#include "gui/MouseSelector.h"

#include <memory>
#include <array>
#include <QImage>
#include <QMouseEvent>
#include <QPoint>
#include <QWheelEvent>
#include <QWidget>
#include <QtGlobal>
#include <QOpenGLWidget>
#include <QLabel>
#include <string>
#include <vector>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <functional>
#include <set>
#include "glview/GLView.h"
#include "../core/MouseConfig.h"

class QGLView : public QOpenGLWidget, public GLView
{
  Q_OBJECT
  Q_PROPERTY(bool showEdges READ showEdges WRITE setShowEdges);
  Q_PROPERTY(bool showAxes READ showAxes WRITE setShowAxes);
  Q_PROPERTY(bool showCrosshairs READ showCrosshairs WRITE setShowCrosshairs);
  Q_PROPERTY(bool orthoMode READ orthoMode WRITE setOrthoMode);
  Q_PROPERTY(double showScaleProportional READ showScaleProportional WRITE setShowScaleProportional);

public:
  QGLView(QWidget *parent = nullptr);
  ~QGLView() override;
#ifdef ENABLE_OPENCSG
  bool hasOpenCSGSupport() { return this->is_opencsg_capable; }
#endif
  // Properties
  bool orthoMode() const { return (this->cam.projection == Camera::ProjectionType::ORTHOGONAL); }
  void setOrthoMode(bool enabled);
  bool showScaleProportional() const { return this->showscale; }
  void setShowScaleProportional(bool enabled) { this->showscale = enabled; }
  std::string getRendererInfo() const override;
  float getDPI() override { return this->devicePixelRatio(); }

  const QImage& grabFrame();
  bool save(const char *filename) const override;
  void resetView();
  void viewAll();
  void selectPoint(int x, int y);
  std::vector<SelectedObject> findObject(int x, int y);
  int measure_state;

  int pickObject(QPoint position);
  // Drop the edit in progress (also used when the source could not be changed).
  void gizmoCancel();

  // Placement mode: the next click on a face (or on the ground) places a part.
  void startPlacement(const QString& label, double markerRadius);
  void stopPlacement();
  bool placementActive() const { return placement_active; }
  // Finds the visible surface along a world ray (origin, direction): point,
  // outward normal and the node index of the leaf hit. Set by MainWindow.
  std::function<bool(const Vector3d&, const Vector3d&, Vector3d&, Vector3d&, int&)> surfaceProvider;

  // Leaf node indices of the current visual-editor selection; pressing on
  // one of them starts a drag on the XY plane.
  std::set<int> gizmoSelectionIndices;

public slots:
  void ZoomIn();
  void ZoomOut();
  void setMouseCentricZoom(bool var) { this->mouseCentricZoom = var; }
  void setMouseActions(int mouseAction, std::array<float, MouseConfig::ACTION_DIMENSION> var)
  {
    // Load an array defining the behaviour for a single mouse action.
    for (int i = 0; i < MouseConfig::ACTION_DIMENSION; i++) {
      this->mouseActions[MouseConfig::ACTION_DIMENSION * mouseAction + i] = var[i];
    }
  }

public:
  QLabel *statusLabel;

  void zoom(double v, bool relative);
  void zoomFov(double v);
  void zoomCursor(int x, int y, int zoom);
  void rotate(double x, double y, double z, bool relative);
  void rotate2(double x, double y, double z);
  void translate(double x, double y, double z, bool relative, bool viewPortRelative = true);

private:
  void init();

  bool mouse_drag_active;
  bool mouse_drag_moved = true;
  bool mouseCentricZoom = true;
  // Information held for each mouse action is a 3x2 rotation matrix, a 3x2 translation matrix, and a
  // zoom 2-vector.
  float mouseActions[MouseConfig::MouseAction::NUM_MOUSE_ACTIONS * MouseConfig::ACTION_DIMENSION];
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  QPointF last_mouse;
#else
  QPoint last_mouse;
#endif
  QImage frame;  // Used by grabFrame() and save()

  void wheelEvent(QWheelEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;

  void initializeGL() override;
  void resizeGL(int w, int h) override;

  // Transform gizmo interaction (see GLView::TransformGizmo).
  QPointF projectToScreen(const Vector3d& p) const;
  void screenRay(const QPointF& pos, Vector3d& nearPt, Vector3d& farPt) const;
  bool rayHitsPlaneZ(const QPointF& pos, double z, Vector3d& hit) const;
  bool axisFacesViewer(int axis) const;
  int gizmoHitHandle(const QPointF& pos) const;
  bool gizmoStartDrag(const QPointF& pos);
  void gizmoBeginEdit(int handle, const QPointF& pos);
  void gizmoUpdateDrag(const QPointF& pos);
  void gizmoApplyInput();
  void gizmoCommit();
  void drawGizmoReadout();
  bool surfaceAt(const QPointF& pos, Vector3d& point, Vector3d& normal);
  void updatePlacement(const QPointF& pos);
  void drawPlacementReadout();
  bool placement_active = false;
  bool placement_on_surface = false;
  int placement_leaf = -1;
  QString placement_label;
  bool gizmo_dragging = false;           // mouse button held on a handle
  bool gizmo_input = false;              // values typed on the keyboard (opened with Tab)
  QString gizmo_input_text[3];           // one field, or X/Y/Z for a free move
  bool gizmo_input_fresh[3] = {};        // prefilled: the next key replaces the text
  int gizmo_input_field = 0;
  bool gizmo_swallow_release = false;    // committed with Enter while the button was held
  int gizmoInputFieldCount() const;
  void gizmoStartInput();
  QPointF gizmo_press_pos;
  QPointF gizmo_last_pos;
  Vector3d gizmo_plane_start;
  double gizmo_plane_z = 0.0;
  double gizmo_rot_last = 0.0;           // screen angle at the last mouse move
  double gizmo_rot_accum = 0.0;          // unwrapped screen angle since press

protected:
  void keyPressEvent(QKeyEvent *event) override;
  bool focusNextPrevChild(bool next) override;

private:

  void paintGL() override;
  void normalizeAngle(GLdouble& angle);

#ifdef ENABLE_OPENCSG
  void display_opencsg_warning() override;
  std::unique_ptr<MouseSelector> selector;
private slots:
  void display_opencsg_warning_dialog();
#endif

signals:
  void cameraChanged();
  void resized();
  void doRightClick(QPoint screen_coordinate);
  void doLeftClick(QPoint screen_coordinate);
  // A gizmo edit was confirmed (mouse release or Enter). For move handles
  // (a, b, c) is the world offset in mm, for rotate handles a is the angle in
  // degrees about the handle axis, for size handles (a, b, c) are the scale
  // factors along the world axes (min corner of the bounding box fixed).
  void gizmoCommitted(int handle, double a, double b, double c);
  // Placement click: point and outward normal of the face (or the ground).
  void placementChosen(bool onSurface, double px, double py, double pz, double nx, double ny, double nz,
                       int leafIndex);
  void placementCancelled();
  // "C" pressed with a selection.
  void colorRequested();
  void initialized();
};

/* These are defined in QLGView2.cc.  See the commentary there. */
// Can't include <QOpenGLContext>, as it will clash with glew. Forward declare.
class QOpenGLContext;
QOpenGLContext *getGLContext();
void setGLContext(QOpenGLContext *);
