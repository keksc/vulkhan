# Menus and graphics settings

The UI uses black panels, white text and red hover/pressed accents. Menus use
a centred 800x600 pixel canvas: larger windows add margins, smaller windows
shrink the entire page uniformly. Glyph metrics update from framebuffer size
without recreating the font atlas; the crosshair remains 32x32 pixels. Mouse
hit testing uses logical window coordinates for high-DPI displays.

ESC opens the pause menu. Resume uses a stable chamfered polygon button with
shape-based hit testing; graphics, controls, drawing and networking have
separate, aligned controls. Buttons highlight on hover, show a pressed state,
and activate on release inside the button. Dragging off cancels the click.
ESC cancels key rebinding without deleting the previous binding.

Polygon vertices remain in local coordinates, including after updates and
resizing. Ear clipping supports concave shapes and either winding. UI images
and palette colours are sampled directly; the old animated water tint is
removed. HUD text and shape uploads use independent buffers for each fenced
frame slot. Capacity growth only replaces the current retired slot, avoiding
both in-flight writes and device-wide waits.

Graphics settings persist in settings.bin. Existing shorter files retain the
new defaults, and invalid graphics values are reset to valid presets.

| Option | Choices | Applied |
| --- | --- | --- |
| Antialiasing | Off, 2x, 4x, 8x MSAA, limited by device capabilities | Restart |
| Grass distance | Near (40 m), Medium (80 m), Far (120 m) | Immediately |
| Terrain shadows | Off, Low (8 steps), Medium (16), High (24) | Immediately |
| V-Sync | On: FIFO; Off: mailbox when available, FIFO fallback | Swapchain recreation |
| Fullscreen, FPS display | On / Off | Immediately |
| Sky resolutions | Existing independent presets | Rebake immediately |
| Sky bake cache | On / Off | Next launch |

The VULKHAN_PRESENT_MODE environment override still takes precedence over
V-Sync. Antialiasing Off renders directly into the single-sample scene colour
and depth inputs, including single-sample lake reflections; it never attempts
to resolve a single-sample attachment. Sample count changes require rebuilding
all dependent scene pipelines, so the menu explicitly marks them for restart.
Grass choices stay within the field's allocated capacity.

[Layout illustration](uiLayout.png) shows colours and placement at 800x600 per
page. It is not an in-game capture. The editing environment has no GPU device;
visual quality, resize behaviour and Vulkan runtime validation need checking
on the target machine.

CPU checks:

```sh
c++ -std=c++23 -O2 tests/uiLayout.cpp -o /tmp/ui-layout-test
/tmp/ui-layout-test
c++ -std=c++23 -O2 tests/uiGeometry.cpp -o /tmp/ui-geometry-test
/tmp/ui-geometry-test
c++ -std=c++23 -O2 tests/graphicsSettings.cpp -o /tmp/graphics-settings-test
XDG_CACHE_HOME=/tmp /tmp/graphics-settings-test
```

Manual checks: hover/click the polygon's cut-off corners, drag a pressed button
off its bounds, open each page, cancel and accept key rebindings, resize and
toggle fullscreen, switch grass/shadows, and restart separately with Off/2x/4x/
8x antialiasing. Inspect validation output and lake reflections at each mode.

Resize verification: compare 800x600, 1920x1080, 2560x720, 600x1200 and
400x300 windows. Check proportional polygons and text, centred menus, square
crosshair, editable text caret, hover/click alignment, minimization and restore,
and fullscreen transitions. Repeat on a high-DPI display.

Text input and canvas: click places the caret at the nearest character on the
clicked line. Home/End navigate within a line, Up/Down retain the column,
Delete removes the following character, and Ctrl+Backspace removes the previous
word. Empty inputs have a usable hit region and a finite caret. Only the focused
input receives typing; inactive inputs hide their carets. The font atlas currently
supports printable ASCII.

The drawing canvas uses the same proportional canvas as the menus. Rectangle
creation respects the selected colour; drags end even when released outside.
Reset clears selection and transient tools. File-picker key handling supports
Backspace, Tab completion and Enter. Truncated binary reads are rejected.

Additional CPU regression check:

```sh
c++ -std=c++23 -O2 tests/textEditing.cpp -o /tmp/text-editing-test
/tmp/text-editing-test
```

Manual checks: edit an empty input; click within multiline text; test navigation,
Ctrl+Backspace, Delete, copy/paste and focus switches; resize while editing.
On the canvas, create text/rectangles/lines/freehand strokes, drag and release
outside, reset, save/load, and use the file picker. GPU verification is pending.

Pause/resume camera input: gameplay look and movement are disabled while the
cursor is free or the window is unfocused. Recapturing the cursor and focus
transitions invalidate the mouse baseline, so the first captured sample absorbs
GLFW's coordinate change without rotating the camera. Subsequent mouse motion
works normally. Resume preserves the saved yaw/pitch without warping the cursor
to stale coordinates.

Sketchbook appearance: a full-window opaque dark backdrop hides the world around
the centred page. The page uses an opaque procedural paper material with fine
grain, fibers and edge wear; it does not sample a background image. Text and
default strokes use charcoal ink. Freehand strokes use tapered triangle ribbons
with subtle width variation, sampled every two framebuffer pixels. Check bright
scenes behind the canvas, window resizing, new text, coloured rectangles, and
freehand drawing/save/load. Runtime visual verification remains pending.

Additional live graphics controls:

| Option | Choices | Effect |
| --- | --- | --- |
| Field of view | 70, 90, 110, 120 degrees | Vertical camera FOV; default retains 110 degrees |
| Grass | On / Off | Show or hide grass draws |
| Post effects | On / Off | Bypass the smoke composite and light-shaft postprocessing chain |
| Light shafts | Off, Subtle, Normal, Strong | 0%, 50%, 100%, 150% intensity; requires post effects |

All four persist and apply without restarting. Light-shaft intensity changes
appearance, not sampling quality; use Post effects Off to bypass that GPU work.
Grass Off hides draws; terrain streaming and grass residency still run.
The graphics page now uses seven compact rows; the earlier layout illustration
shows the original arrangement. Verify new controls at 800x600 and narrow window
sizes, then restart to check persistence. No GPU is available in the editing
environment for runtime screenshots or frame-rate measurements.

FOV and light-shaft intensity now use the existing Slider widget. FOV ranges
from 70 to 120 degrees in one-degree steps; shafts range from 0 to 150 percent
in one-percent steps. Numeric labels update live, and settings save on release.
The slider has a grey track, red filled section and white thumb that becomes
red during hover/drag. Thumb dimensions preserve pixel proportions on resize.
Clicking the track sets its value; dragging outside clamps to the endpoints;
releasing outside ends the drag. AA, shadows and texture resolutions retain
preset buttons because their choices are discrete.
