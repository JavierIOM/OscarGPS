# Acrylic lid

A laser-cut lid in 3 mm acrylic, replacing the 3D printed one. The screen and GPS sit in
acrylic clips glued to its underside. Everything is copied from the printed lid that fitted:
same outline, screw holes, window and clip positions.

| File | Cut from | What |
|------|----------|------|
| `oscargps-lid-acrylic.svg` | 3 mm acrylic | The lid and 12 support clips |
| `oscargps-glue-jig.svg` | 3 mm card, MDF or ply, **not acrylic** | A jig that puts the clips in exactly the right place |
| `make_lid.py` | | Writes both SVGs. Change a dimension there and rerun it |

The lid is 78.4 x 80.4 mm with 3 mm corners and four M3 holes on a 67 x 69 mm pitch.

## Layers

| Colour | Layer |
|--------|-------|
| Black | Cut |
| Blue | Screen window. Cut it on opaque acrylic. On clear or smoked acrylic, leave it off: the OLED shows straight through, and smoked hides the insides |
| Green | Score (the arrow on the jig) |

The clips are small and can drop through a honeycomb bed. Cut them on a pin bed or over a
sheet of card.

## Assembly

1. **Lid face down.** Turn the cut lid over left to right, like turning a page.
2. **Jig on top, arrow up.** Line up the outline and push M3 screws through two opposite
   corners to pin it. If you cut the window, the jig's window hole lines up with it. If it
   doesn't, the jig is upside down or turned round.
3. **Drop the clips in.** Four screen clips and four GPS corner brackets. Each fits its hole
   only one way round.
4. **Glue with capillary acrylic cement** (Tensol 12, Plastic Weld or similar) run along the
   foot of each clip. **Not superglue**: it fogs acrylic white. This is why the jig mustn't be
   acrylic too, or it gets welded to the lid.
5. **Lift the jig** once the cement has grabbed, then glue the second layer of GPS brackets on
   top of the first. Those are 6 mm tall in all, the screen clips 3 mm.
6. **Fit the boards.**
   - The OLED goes glass first into the screen clips, with its pin header at the open gap.
   - The GPS goes module first (antenna towards the lid) into the corner brackets. The
     sides are open for the STEMMA cables.
   - A dab of hot glue or a strip of tape on the back holds each one.
7. **Screw it on** with M3 button heads. The printed lid had counterbores for the heads;
   acrylic doesn't, so the heads sit on top.

The OLED glass rests directly against the lid (the printed lid had a 1.6 mm pocket for it,
which can't be cut).
