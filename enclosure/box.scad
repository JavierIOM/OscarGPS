// OscarGPS layered plywood box: 8 layers of 3.6 mm ply under the 3 mm acrylic lid.
// Interior copied from the printed base (gps_case_base.stl) in its coordinates: looking down
// on the box, x right, y away from you, origin at the inside corner of the cavity. The back
// wall (y = 78.2) has the USB opening.
//
//   part = "sheet"     all eight layers laid out flat, for export to SVG and LightBurn
//   part = "assembly"  the stack with the lid and dummy boards, for a visual check
//   part = "cutaway"   the same, front half removed
part = "assembly";

ply = 3.6;
lid_t = 3.0;
outer = [-2.2, -2.2, 76.2, 78.2];     // x0, y0, x1, y1, same as the acrylic lid
corner_r = 3;
cavity = [0, 0, 74, 76];

// Lid bolts: M3 x 12 button heads through the lid, L8 and L7 into nuts trapped in L7
bolts = [[3.5, 3.5], [70.5, 3.5], [3.5, 72.5], [70.5, 72.5]];
bolt_d = 3.4;
nut_af = 5.75;                         // M3 nut is 5.5 across flats
block = 8.5;                           // corner blocks in L6 to L8, above the battery

battery = [3.0, 5.25, 37.5, 70.75];    // 34.5 x 65.5 pocket in L2 and L3
board = [44.0, 7.5, 73.5, 66.0];       // FireBeetle 29 x 58 plus clearance
pad = 3;                               // corner pads the board rests on (L2)
usb = [51.3, 66.05];                   // USB opening x range, back wall, L2 to L4
slot = [17.55, 22.3];                  // small back-wall slot copied from the print, L6
lead = [37.5, 50.0, 44.0, 66.0];       // battery lead channel between the two bays, L3

$fn = 48;

module rect(b) translate([b[0], b[1]]) square([b[2] - b[0], b[3] - b[1]]);
module rrect(b, r) translate([b[0] + r, b[1] + r]) offset(r = r) square([b[2] - b[0] - 2 * r, b[3] - b[1] - 2 * r]);
module outline() rrect(outer, corner_r);
module back_gap(x0, x1, from_y) rect([x0, from_y, x1, outer[3] + 1]);
module holes() for (b = bolts) translate(b) circle(d = bolt_d);
module hexes() for (b = bolts) translate(b) circle(d = nut_af / cos(30), $fn = 6);
module cavity_less_blocks() difference() {
    rect(cavity);
    for (b = [[0, 0], [74 - block, 0], [0, 76 - block], [74 - block, 76 - block]])
        translate(b) square([block, block]);
}
module board_air() difference() {      // under the board, leaving a pad at each corner
    rect(board);
    for (c = [[board[0], board[1]], [board[2] - pad, board[1]], [board[0], board[3] - pad], [board[2] - pad, board[3] - pad]])
        translate(c) square([pad, pad]);
}

module layer(n) {
    if (n == 1) outline();
    if (n == 2) difference() { outline(); rect(battery); board_air(); back_gap(usb[0], usb[1], board[3] - 1); }
    if (n == 3) difference() { outline(); rect(battery); rect(board); rect(lead); back_gap(usb[0], usb[1], board[3] - 1); }
    if (n == 4) difference() { outline(); rect(cavity); back_gap(usb[0], usb[1], cavity[3] - 1); }
    if (n == 5) difference() { outline(); rect(cavity); }
    if (n == 6) difference() { outline(); cavity_less_blocks(); holes(); back_gap(slot[0], slot[1], cavity[3] - 1); }
    if (n == 7) difference() { outline(); cavity_less_blocks(); hexes(); }
    if (n == 8) difference() { outline(); cavity_less_blocks(); holes(); }
}

// ---- Lid (matches make_lid.py) and dummy parts, preview only ----
window = [37.57, 26.57, 70.43, 45.43];
screen_clips = [
    [[34.85, 18.3], [42.05, 18.3], [42.05, 21.0], [36.05, 21.0], [36.05, 27.0], [34.85, 27.0]],
    [[65.95, 18.3], [73.45, 18.3], [73.45, 27.0], [71.95, 27.0], [71.95, 21.0], [65.95, 21.0]],
    [[34.85, 49.0], [36.05, 49.0], [36.05, 57.5], [31.5, 57.5], [31.5, 55.5], [34.85, 55.5]],
    [[71.95, 49.0], [73.45, 49.0], [73.45, 57.5], [71.95, 57.5]]];
gps_clips = [
    [[3.5, 22.55], [12.0, 22.55], [12.0, 25.05], [6.0, 25.05], [6.0, 31.05], [3.5, 31.05]],
    [[26.0, 22.55], [33.2, 22.55], [33.2, 31.05], [32.0, 31.05], [32.0, 25.05], [26.0, 25.05]],
    [[3.5, 44.95], [6.0, 44.95], [6.0, 50.95], [12.0, 50.95], [12.0, 53.45], [3.5, 53.45]],
    [[32.0, 44.95], [33.2, 44.95], [33.2, 53.45], [26.0, 53.45], [26.0, 50.95], [32.0, 50.95]]];

top = 8 * ply;
module lid() {
    color("LightSkyBlue", 0.35) translate([0, 0, top]) linear_extrude(lid_t) difference() { outline(); holes(); rect(window); }
    color("LightSkyBlue", 0.6) {
        for (c = screen_clips) translate([0, 0, top - 3]) linear_extrude(3) polygon(c);
        for (c = gps_clips) translate([0, 0, top - 6]) linear_extrude(6) polygon(c);
    }
}
module dummies() {
    color("DimGray") translate([battery[0] + 0.25, battery[1] + 0.25, ply]) cube([34, 65, 10]);           // LiPo
    translate([board[0] + 0.25, board[1] + 0.25, 2 * ply]) {
        color("SeaGreen") cube([29, 58, 1.6]);                                                          // FireBeetle
        color("Silver") translate([5.5, 5, 1.6]) cube([18, 25.5, 3.1]);                                  // ESP32 module
        color("Silver") translate([10.5, 57, 1.6]) cube([8, 6, 3]);                                      // USB, overhanging
        color("Black") for (x = [0.3, 26.2]) translate([x, 6, 1.6]) cube([2.5, 40, 2.5]);                // header strips
        color("Black") translate([26.2, 40, 4.1]) cube([2.5, 12.7, 14]);                                 // jumper housings on 5 pins
    }
    color("Navy") translate([6.25, 25.3, top - 5.3]) { cube([25.4, 25.4, 1.6]); translate([7.7, 7.7, 1.6]) cube([10, 10, 3.7]); } // GPS, module up
    color("Black") translate([36.3, 21.2, top - 2.6]) { cube([35.4, 33.5, 1.2]); translate([0.15, 5.2, 1.2]) cube([35.1, 23.7, 1.4]); } // OLED, glass up
}
module stack() for (n = [1:8]) color(n % 2 ? "BurlyWood" : "Tan") translate([0, 0, (n - 1) * ply]) linear_extrude(ply) layer(n);

if (part == "sheet") {
    for (n = [1:8]) translate([((n - 1) % 4) * 84 + 2.2 + 2, floor((n - 1) / 4) * 86 + 2.2 + 2]) layer(n);
} else if (part == "assembly") {
    stack(); lid(); dummies();
} else if (part == "cutaway") {
    difference() { union() { stack(); lid(); dummies(); } translate([-10, -10, -1]) cube([100, 38, 50]); }
}
