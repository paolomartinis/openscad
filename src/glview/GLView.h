#pragma once

/* GLView: A basic OpenGL rectangle for rendering images.

   This class is inherited by:

 * QGLview - for Qt GUI
 * OffscreenView - for offscreen rendering, in tests and from command-line
   (This class is also overridden by NULLGL.cc for special experiments)

   The view assumes either a Gimbal Camera (rotation,translation,distance)
   or Vector Camera (eye,center/target) is being used. See Camera.h. The
   cameras are not kept in sync.

   QGLView only uses GimbalCamera while OffscreenView can use either one.
   Some actions (showCrossHairs) only work properly on Gimbal Camera.

 */

#include <memory>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <string>
#include <vector>
#include "glview/Camera.h"
#include "glview/ShaderUtils.h"
#include "geometry/linalg.h"
#include "glview/ColorMap.h"
#include "glview/system-gl.h"
#include "core/Selection.h"
#include "glview/Renderer.h"

class GLView
{
public:
  GLView();
  virtual ~GLView();

  void setupShader();
  void teardownShader();

  void setRenderer(std::shared_ptr<Renderer> r);
  [[nodiscard]] Renderer *getRenderer() const { return this->renderer.get(); }

  void initializeGL();
  void resizeGL(int w, int h);
  virtual void paintGL();

  void setCamera(const Camera& cam);
  void setupCamera();

  void setColorScheme(const ColorScheme& cs);
  void setColorScheme(const std::string& cs);
  void updateColorScheme();

  [[nodiscard]] bool showAxes() const { return this->showaxes; }
  void setShowAxes(bool enabled) { this->showaxes = enabled; }
  [[nodiscard]] bool showScaleProportional() const { return this->showscale; }
  void setShowScaleProportional(bool enabled) { this->showscale = enabled; }
  [[nodiscard]] bool showEdges() const { return this->showedges; }
  void setShowEdges(bool enabled) { this->showedges = enabled; }
  [[nodiscard]] bool showCrosshairs() const { return this->showcrosshairs; }
  void setShowCrosshairs(bool enabled) { this->showcrosshairs = enabled; }

  virtual bool save(const char *filename) const = 0;
  [[nodiscard]] virtual std::string getRendererInfo() const = 0;
  virtual float getDPI() { return 1.0f; }

  std::unique_ptr<ShaderUtils::ShaderInfo> edge_shader;
  std::shared_ptr<Renderer> renderer;
  const ColorScheme *colorscheme;
  Camera cam;
  double far_far_away;
  double aspectratio;
  bool showaxes;
  bool showedges;
  bool showcrosshairs;
  bool showscale;
  GLdouble modelview[16];
  GLdouble projection[16];
  std::vector<SelectedObject> selected_obj;
  std::vector<SelectedObject> shown_obj;

  // Transform gizmo of the visual editor: selection bounds, move arrows,
  // rotation rings and size handles. While an edit is in progress only the
  // gizmo shows it (as a "ghost" box); the model is re-rendered once the edit
  // has been written to the source.
  struct TransformGizmo {
    enum Handle {
      None = -1,
      MoveX, MoveY, MoveZ, MovePlane,
      RotateX, RotateY, RotateZ,
      SizeX, SizeY, SizeZ
    };
    static bool isMove(int h) { return h >= MoveX && h <= MovePlane; }
    static bool isRotate(int h) { return h >= RotateX && h <= RotateZ; }
    static bool isSize(int h) { return h >= SizeX && h <= SizeZ; }
    // World axis 0..2 of a handle (MovePlane -> 3).
    static int axisOf(int h)
    {
      return isMove(h) ? h - MoveX : isRotate(h) ? h - RotateX : isSize(h) ? h - SizeX : -1;
    }

    bool visible = false;
    BoundingBox bbox;
    int hover = None;
    int active = None;
    Vector3d offset = Vector3d::Zero();  // move, world mm
    double angle = 0.0;                  // rotate, degrees about the handle axis
    Vector3d size = Vector3d::Zero();    // resize: target size, min corner fixed

    [[nodiscard]] Vector3d bboxSize() const { return bbox.isEmpty() ? Vector3d::Zero() : Vector3d(bbox.sizes()); }
    void resetEdit()
    {
      offset = Vector3d::Zero();
      angle = 0.0;
      size = bboxSize();
    }
    [[nodiscard]] bool hasEdit() const;
    // Where point p ends up with the edit in progress.
    [[nodiscard]] Vector3d transformPoint(const Vector3d& p) const;
  };
  TransformGizmo gizmo;

  // Where a part will be placed (Insert > Hardware): a ring on the face under
  // the mouse and its normal.
  struct PlacementMarker {
    bool visible = false;
    Vector3d point = Vector3d::Zero();
    Vector3d normal = Vector3d::UnitZ();
    double radius = 3.0;
  };
  PlacementMarker placement;
  [[nodiscard]] Vector3d gizmoOrigin() const { return gizmo.bbox.center(); }
  [[nodiscard]] double gizmoHandleLength() const { return cam.zoomValue() * 0.12; }
  [[nodiscard]] double gizmoRingRadius() const { return gizmoHandleLength() * 0.8; }

#ifdef ENABLE_OPENCSG
  bool is_opencsg_capable;
  bool has_shaders;
  void enable_opencsg_shaders();
  virtual void display_opencsg_warning() = 0;
  int opencsg_id;
#endif
  void showObject(const SelectedObject& pt, const Vector3d& eyedir);
  void showGizmo();
  void showPlacement();

private:
  void showCrosshairs(const Color4f& col);
  void showAxes(const Color4f& col);
  void showSmallaxes(const Color4f& col);
  void showScalemarkers(const Color4f& col);
  void decodeMarkerValue(double i, double l, int size_div_sm);
};
