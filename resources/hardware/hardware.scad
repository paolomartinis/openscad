// Metric hardware library: screws, bolts, nuts, washers and holes (M2-M12).
//
// Installed by OpenSCAD (Insert > Hardware) into the user library folder as
// openscad-hardware/hardware.scad. Use it with:
//     use <openscad-hardware/hardware.scad>
//     socket_head_screw("M3", l = 12);
//
// Conventions:
// - z = 0 is the surface the part sits on or is driven into.
// - Screws: head above z = 0 (countersunk heads end flush at z = 0), shank
//   down to z = -l. Set screws: top face at z = 0.
// - Nuts and washers sit on z = 0 and go up. Threaded rods go from 0 to l.
// - Holes are cutting tools: use them inside difference(). They start 1 mm
//   above z = 0 and go down to z = -l.
// - thread = true models a printable single-start right-hand thread; the
//   default is a plain cylinder at the nominal diameter (use metal screws).
//
// Dimensions follow ISO 4017 (hex bolt), 4762 (socket head), 10642
// (countersunk), 7380 (button head), 4026 (set screw), 4032 (hex nut),
// 10511 (nyloc nut), 7089 (washer), 273 medium (clearance holes).
// Heat-set insert holes use typical values: check your inserts.

// ---------------------------------------------------------------- data
//  0 name     1 d   2 pitch 3 hexS 4 hexK 5 nutM 6 nyloc 7 sockDk 8 sockKey
//  9 cskDk 10 cskK 11 cskKey 12 washD1 13 washD2 14 washH 15 clearance
// 16 counterbore 17 insertD 18 insertL 19 btnDk 20 btnK 21 btnKey 22 grubKey
HW_SIZES = [
  ["M2",   2,   0.4,  4,   1.4, 1.6,  3.0, 3.8,  1.5, 3.8,   1.2,  1.3, 2.2,  5,  0.3, 2.4,  4.4, 3.2,  4.0,  3.5,  1.3,  1.3, 0.9],
  ["M2.5", 2.5, 0.45, 5,   1.7, 2.0,  3.5, 4.5,  2,   4.7,   1.5,  1.5, 2.7,  6,  0.5, 2.9,  5.5, 3.5,  5.0,  4.7,  1.5,  1.5, 1.3],
  ["M3",   3,   0.5,  5.5, 2,   2.4,  4,   5.5,  2.5, 6.72,  1.86, 2,   3.2,  7,  0.5, 3.4,  6.5, 4.0,  5.7,  5.7,  1.65, 2,   1.5],
  ["M4",   4,   0.7,  7,   2.8, 3.2,  5,   7,    3,   8.96,  2.48, 2.5, 4.3,  9,  0.8, 4.5,  8,   5.6,  8.1,  7.6,  2.2,  2.5, 2],
  ["M5",   5,   0.8,  8,   3.5, 4.7,  5,   8.5,  4,   11.2,  3.1,  3,   5.3,  10, 1,   5.5,  10,  6.4,  9.5,  9.5,  2.75, 3,   2.5],
  ["M6",   6,   1,    10,  4,   5.2,  6,   10,   5,   13.44, 3.72, 4,   6.4,  12, 1.6, 6.6,  11,  8.0,  12.7, 10.5, 3.3,  4,   3],
  ["M8",   8,   1.25, 13,  5.3, 6.8,  8,   13,   6,   17.92, 4.96, 5,   8.4,  16, 1.6, 9,    15,  10,   13,   14,   4.4,  5,   4],
  ["M10",  10,  1.5,  16,  6.4, 8.4,  10,  16,   8,   22.4,  6.2,  6,   10.5, 20, 2,   11,   18,  12.5, 15,   17.5, 5.5,  6,   5],
  ["M12",  12,  1.75, 18,  7.5, 10.8, 12,  18,   10,  26.88, 7.44, 8,   13,   24, 2.5, 13.5, 20,  15,   18,   21,   6.6,  8,   6],
];

// Row of a size ("M3"); stops with an error for unknown sizes.
function hw_size(size) =
  let(i = search([size], HW_SIZES, 1, 0)[0])
  assert(i != [] && i != undef, str("hardware: unknown size ", size, " (M2..M12)"))
  HW_SIZES[i];

// Nominal diameter and coarse pitch, e.g. for custom parts.
function hw_d(size) = hw_size(size)[1];
function hw_pitch(size) = hw_size(size)[2];
function hw_clearance(size) = hw_size(size)[15];

// ---------------------------------------------------------------- helpers

module hw_hex_prism(s, h) {
  // s = width across flats, from z = 0 to z = h
  cylinder(h = h, d = s / cos(30), $fn = 6);
}

// Shank from z = -l to 0: plain or with a printable thread.
module hw_shank(d, l, pitch, thread, clearance = 0) {
  translate([0, 0, -l])
    if (thread) {
      e = pitch * 0.3;  // thread depth ~0.6 * pitch
      linear_extrude(height = l, twist = -360 * l / pitch, slices = ceil(l / pitch * 12))
        translate([e, 0]) circle(r = d / 2 - e + clearance, $fn = 32);
    } else {
      cylinder(h = l, d = d + 2 * clearance, $fn = 32);
    }
}

// ---------------------------------------------------------------- screws

// Hex head bolt, ISO 4017.
module hex_bolt(size = "M3", l = 10, thread = false) {
  s = hw_size(size);
  hw_hex_prism(s[3], s[4]);
  hw_shank(s[1], l, s[2], thread);
}

// Socket head cap screw, ISO 4762.
module socket_head_screw(size = "M3", l = 10, thread = false) {
  s = hw_size(size);
  difference() {
    union() {
      cylinder(h = s[1], d = s[7], $fn = 48);
      hw_shank(s[1], l, s[2], thread);
    }
    translate([0, 0, s[1] * 0.4]) hw_hex_prism(s[8], s[1]);
  }
}

// Countersunk socket screw, ISO 10642. l is the overall length.
module countersunk_screw(size = "M3", l = 10, thread = false) {
  s = hw_size(size);
  difference() {
    union() {
      translate([0, 0, -s[10]]) cylinder(h = s[10], d1 = s[1], d2 = s[9], $fn = 48);
      translate([0, 0, -s[10]]) hw_shank(s[1], l - s[10], s[2], thread);
    }
    translate([0, 0, -s[10] * 0.6]) hw_hex_prism(s[11], s[10]);
  }
}

// Button head socket screw, ISO 7380.
module button_head_screw(size = "M3", l = 10, thread = false) {
  s = hw_size(size);
  difference() {
    union() {
      intersection() {
        scale([1, 1, s[20] / (s[19] / 2)]) sphere(d = s[19], $fn = 48);
        cylinder(h = s[20], d = s[19], $fn = 48);
      }
      hw_shank(s[1], l, s[2], thread);
    }
    translate([0, 0, s[20] * 0.4]) hw_hex_prism(s[21], s[20]);
  }
}

// Set screw (grub screw) with hex socket and flat point, ISO 4026.
module set_screw(size = "M3", l = 6, thread = false) {
  s = hw_size(size);
  difference() {
    hw_shank(s[1], l, s[2], thread);
    translate([0, 0, -min(l * 0.6, s[22] * 1.5)]) hw_hex_prism(s[22], l);
  }
}

// Threaded rod / stud from z = 0 to z = l.
module threaded_rod(size = "M3", l = 20, thread = false) {
  s = hw_size(size);
  translate([0, 0, l]) hw_shank(s[1], l, s[2], thread);
}

// ---------------------------------------------------------------- nuts and washers

// Hex nut, ISO 4032.
module hex_nut(size = "M3") {
  s = hw_size(size);
  difference() {
    hw_hex_prism(s[3], s[5]);
    translate([0, 0, -1]) cylinder(h = s[5] + 2, d = s[1], $fn = 32);
  }
}

// Prevailing torque (nylon insert) nut, ISO 10511.
module nyloc_nut(size = "M3") {
  s = hw_size(size);
  difference() {
    union() {
      hw_hex_prism(s[3], s[5]);
      cylinder(h = s[6], d = s[3] * 0.9, $fn = 48);
    }
    translate([0, 0, -1]) cylinder(h = s[6] + 2, d = s[1], $fn = 32);
  }
}

// Plain washer, ISO 7089.
module washer(size = "M3") {
  s = hw_size(size);
  difference() {
    cylinder(h = s[14], d = s[13], $fn = 48);
    translate([0, 0, -1]) cylinder(h = s[14] + 2, d = s[12], $fn = 32);
  }
}

// ---------------------------------------------------------------- holes (cutting tools)

// Through hole with clearance, ISO 273 medium.
module clearance_hole(size = "M3", l = 10) {
  s = hw_size(size);
  translate([0, 0, -l]) cylinder(h = l + 1, d = s[15], $fn = 32);
}

// Clearance hole with a counterbore for a socket head screw.
module counterbore_hole(size = "M3", l = 10) {
  s = hw_size(size);
  clearance_hole(size, l);
  translate([0, 0, -(s[1] + 0.4)]) cylinder(h = s[1] + 1.4, d = s[16], $fn = 48);
}

// Clearance hole with a 90 degree countersink for a countersunk screw.
module countersink_hole(size = "M3", l = 10) {
  s = hw_size(size);
  clearance_hole(size, l);
  k = s[10] + 0.2;
  translate([0, 0, -k]) cylinder(h = k, d1 = s[15], d2 = s[9] + 0.4, $fn = 48);
  cylinder(h = 1, d = s[9] + 0.4, $fn = 48);
}

// Hex nut pocket at the surface (with printing clearance) and a clearance
// hole below it.
module nut_trap(size = "M3", l = 10) {
  s = hw_size(size);
  clearance_hole(size, l);
  translate([0, 0, -(s[5] + 0.4)]) hw_hex_prism(s[3] + 0.4, s[5] + 1.4);
}

// Pilot hole for a heat-set threaded insert (typical sizes).
module heat_insert_hole(size = "M3") {
  s = hw_size(size);
  translate([0, 0, -s[18]]) cylinder(h = s[18] + 1, d = s[17], $fn = 32);
}

// Printable internal thread (tapped hole) for a printed or metal screw.
module tapped_hole(size = "M3", l = 10, clearance = 0.2) {
  s = hw_size(size);
  translate([0, 0, 1]) hw_shank(s[1], l + 1, s[2], true, clearance);
}
