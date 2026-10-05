#include "gui/VisualEdit.h"

#include <catch2/catch_all.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "core/LocalScope.h"
#include "core/ModuleInstantiation.h"
#include "core/SourceFile.h"
#include "openscad.h"

namespace {

// Parses `source` and returns its top-level statement number `index`.
std::shared_ptr<SourceFile> parseSource(const std::string& source)
{
  SourceFile *file = nullptr;
  REQUIRE(parse(file, source, "test.scad", "test.scad", false));
  REQUIRE(file != nullptr);
  return std::shared_ptr<SourceFile>(file);
}

std::string applyEdits(std::string source, const std::vector<VisualEdit::TextEdit>& edits)
{
  // Byte offset of the start of each line.
  std::vector<size_t> lineStart{0};
  for (size_t i = 0; i < source.size(); ++i) {
    if (source[i] == '\n') lineStart.push_back(i + 1);
  }
  for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
    const size_t begin = lineStart[it->firstLine - 1] + it->firstColumn - 1;
    const size_t end = lineStart[it->lastLine - 1] + it->lastColumn - 1;
    source.replace(begin, end - begin, it->text);
  }
  return source;
}

const ModuleInstantiation& statement(const std::shared_ptr<SourceFile>& file, size_t index)
{
  REQUIRE(file->scope->moduleInstantiations.size() > index);
  return *file->scope->moduleInstantiations[index];
}

}  // namespace

TEST_CASE("VisualEdit formats numbers compactly", "[visualedit]")
{
  CHECK(VisualEdit::formatNumber(10.0) == "10");
  CHECK(VisualEdit::formatNumber(2.5) == "2.5");
  CHECK(VisualEdit::formatNumber(-0.25) == "-0.25");
  CHECK(VisualEdit::formatNumber(1e-9) == "0");
  CHECK(VisualEdit::formatNumber(0.1 + 0.2) == "0.3");
}

TEST_CASE("VisualEdit translate", "[visualedit]")
{
  SECTION("literal vector components are rewritten")
  {
    const std::string src = "translate([0, 5, -2])\n  cube(10); // keep\n";
    auto file = parseSource(src);
    CHECK(applyEdits(src, VisualEdit::planTranslate(statement(file, 0), Vector3d(12, 0, 2))) ==
          "translate([12, 5, 0])\n  cube(10); // keep\n");
  }
  SECTION("expressions get an offset")
  {
    const std::string src = "wall = 3;\ntranslate([wall, 30, 0]) cylinder(h = 15, r = 6);\n";
    auto file = parseSource(src);
    CHECK(applyEdits(src, VisualEdit::planTranslate(statement(file, 0), Vector3d(5, -4, 0))) ==
          "wall = 3;\ntranslate([wall + 5, 26, 0]) cylinder(h = 15, r = 6);\n");
  }
  SECTION("2D vectors gain a z component")
  {
    const std::string src = "translate([1, 2]) sphere(1);";
    auto file = parseSource(src);
    CHECK(applyEdits(src, VisualEdit::planTranslate(statement(file, 0), Vector3d(0, 0, 3))) ==
          "translate([1, 2, 3]) sphere(1);");
  }
  SECTION("other statements are wrapped")
  {
    const std::string src = "  sphere(r = 8);";
    auto file = parseSource(src);
    CHECK(applyEdits(src, VisualEdit::planTranslate(statement(file, 0), Vector3d(1, 2, 3))) ==
          "  translate([1, 2, 3]) sphere(r = 8);");
  }
}

TEST_CASE("VisualEdit rotate", "[visualedit]")
{
  SECTION("first rotation wraps the statement about the pivot")
  {
    const std::string src = "cube(10);";
    auto file = parseSource(src);
    CHECK(applyEdits(src, VisualEdit::planRotate(statement(file, 0), Vector3d(5, 5, 5), Vector3d::UnitZ(), 30)) ==
          "translate([5, 5, 5]) rotate([0, 0, 30]) translate([-5, -5, -5]) cube(10);");
  }
  SECTION("further rotations only update the angles")
  {
    const std::string src = "translate([5, 5, 5]) rotate([0, 0, 30]) translate([-5, -5, -5]) cube(10);";
    auto file = parseSource(src);
    CHECK(applyEdits(src, VisualEdit::planRotate(statement(file, 0), Vector3d(9, 9, 9), Vector3d::UnitZ(), 15)) ==
          "translate([5, 5, 5]) rotate([0, 0, 45]) translate([-5, -5, -5]) cube(10);");
    CHECK(applyEdits(src, VisualEdit::planRotate(statement(file, 0), Vector3d(9, 9, 9), Vector3d::UnitX(), 90)) ==
          "translate([5, 5, 5]) rotate([90, -30, 0]) translate([-5, -5, -5]) cube(10);");
  }
}

TEST_CASE("VisualEdit scale", "[visualedit]")
{
  SECTION("cube at its corner changes size")
  {
    const std::string src = "translate([2, 3, 0]) cube([20, 20, 10]);";
    auto file = parseSource(src);
    CHECK(applyEdits(src, VisualEdit::planScale(statement(file, 0), Vector3d(2, 3, 0), Vector3d(1.25, 1, 1))) ==
          "translate([2, 3, 0]) cube([25, 20, 10]);");
  }
  SECTION("scalar cube stays scalar when scaled uniformly")
  {
    const std::string src = "cube(10);";
    auto file = parseSource(src);
    CHECK(applyEdits(src, VisualEdit::planScale(statement(file, 0), Vector3d::Zero(), Vector3d(2, 2, 2))) ==
          "cube(20);");
    CHECK(applyEdits(src, VisualEdit::planScale(statement(file, 0), Vector3d::Zero(), Vector3d(1, 1, 0.5))) ==
          "cube([10, 10, 5]);");
  }
  SECTION("cylinder height along Z")
  {
    const std::string src = "translate([0, 0, 1]) cylinder(h = 15, r = 6);";
    auto file = parseSource(src);
    CHECK(applyEdits(src, VisualEdit::planScale(statement(file, 0), Vector3d(-6, -6, 1), Vector3d(1, 1, 2))) ==
          "translate([0, 0, 1]) cylinder(h = 30, r = 6);");
  }
  SECTION("centered cube and other shapes are wrapped, then updated")
  {
    const std::string src = "cube(10, center = true);";
    auto file = parseSource(src);
    const std::string wrapped =
      applyEdits(src, VisualEdit::planScale(statement(file, 0), Vector3d(-5, -5, -5), Vector3d(2, 1, 1)));
    CHECK(wrapped ==
          "translate([-5, -5, -5]) scale([2, 1, 1]) translate([5, 5, 5]) cube(10, center = true);");
    auto again = parseSource(wrapped);
    CHECK(applyEdits(wrapped, VisualEdit::planScale(statement(again, 0), Vector3d(-5, -5, -5), Vector3d(1, 3, 1))) ==
          "translate([-5, -5, -5]) scale([2, 3, 1]) translate([5, 5, 5]) cube(10, center = true);");
  }
}
