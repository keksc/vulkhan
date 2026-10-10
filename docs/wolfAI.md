# Wolf pack AI

This is a CPU state machine and navigation controller, separate from rendering.
It is game behavior with explicit goals and safety rules, not a biological
simulation or a learned model.

## Ownership

- `WolfPacks` owns behavior, perception, routes, pack coordination, locomotion,
  stamina, and fixed-step simulation. It has no Vulkan dependencies.
- `WolfNavigation` queries the terrain surface and exclusions and plans paths.
- The main loop advances AI after terrain streaming and sends an immutable
  wolf snapshot to `WolfSys`.
- `WolfSys` loads the GLB, caches animation poses, uploads instances, culls,
  chooses mesh/fur LODs, and draws the main and reflection views. It owns no AI
  instance and never advances behavior.

## Behavior

| State | Purpose | Transition |
| --- | --- | --- |
| Travel | Visit persistent territory destinations in order | Rest on arrival/exhaustion; regroup if followers lag |
| Rest | Stop and recover stamina while the group settles | Resume the route or select the next territory destination |
| Alert | Stop and face a nearby visible player | Resume after losing sight; flee if approached closely or watched continuously for 3 seconds |
| Flee | Escape away from the last perceived threat, keeping the pack together | Stop only after a minimum escape period, sufficient distance and lost contact |
| Regroup | Let separated followers catch up | Resume travel once formation recovers |

Sight considers terrain occlusion and proximity to **any** member. Running
nearby can be heard even without a clear sight line. Threat confidence,
separate enter/exit distances, and state dwell times prevent rapid switching.
Large camera teleports do not count as running footsteps. The spectator
preview camera is excluded from threat perception.

The only random sampling is population placement and individual size/run
phase. Navigation goals do not change randomly every few seconds. Packs use
four persistent local territory destinations. Arrived sites are skipped during
route selection. If the full circuit is inaccessible or outside streamed
terrain, the next site is adapted to a reachable local point 8–32 m away;
a route back to the wolf's current position cannot masquerade as a patrol.
These are movement goals;
there are no prey actors or hunting/combat targets wired into this system.
The current observer is the local player, not a server-authoritative list of
all network players.

## Routes and locomotion

A* uses a 49x49 grid with 4 m cells around the start and destination, capped
at 768 expanded nodes and 32,768 height queries per search. All route attempts
share a 65,536-query budget per frame. Real-time queries use only streamed
terrain; absent chunks suspend the pack until ground is available, rather
than running procedural erosion on the game thread. Traversal checks body clearance, terrain
slope and intermediate samples, and diagonals cannot cut blocked corners.
Route smoothing retains only clear segments. Failure stays a failure;
unreachable goals never produce a straight path through water or cliffs.
New streamed exclusions trigger a detour to the same destination before
fallback territory selection. Decisions run approximately at 5 Hz and are
staggered across packs; movement remains at 30 Hz with interpolated output.

Followers have stable pair slots along the leader's distance-sampled trail,
with speed feed-forward and slot-error correction. They follow bends rather
than pursuing the leader's current point. Narrow terrain reduces lateral
spacing to single file. Separated or stuck followers can plan a bounded
recovery path back to formation. The leader pauses to regroup outside a flee
response. Failed long routes are followed by direct local-route retries,
so they cannot consume every retry's search budget before short routes run.
Escape also accepts shorter, forward/sideways routes on loaded ground.

Acceleration, braking and angular speed are bounded. Predictive obstacle
probes steer smoothly with a persistent avoidance side instead of instant
90-degree turns. Close-range separation and terrain-safe contact correction
prevent bodies overlapping when routes converge. Stopped wolves blend back
to the mesh's standing bind pose instead of freezing a lifted running paw.
Moving poses still use the shared 48-frame run cache and travelled-distance
phase. Independent foot IK and additional authored walk/idle clips are not
implemented.

## Validation and diagnostics

```sh
c++ -std=c++23 -O2 -DGLM_ENABLE_EXPERIMENTAL tests/wolfAI.cpp \
  src/vkh/systems/wolf/wolfPacks.cpp \
  src/vkh/systems/wolf/wolfNavigation.cpp -o /tmp/wolf-ai-tests
/tmp/wolf-ai-tests
VULKHAN_WOLF_DEBUG=1 ./build/vulkhan
```

Tests cover persistent destinations, patrol/rest cycles, formation spread,
body spacing, turn/acceleration bounds, terrain-aware detours, unreachable
paths, shared escape behavior, alert hysteresis, occluded perception and
spectator exclusion. Existing pack tests cover deterministic updates,
spawning, invalid timesteps, terrain grounding and complete hill circuits.

Debug output reports each pack's state, stamina, threat confidence, route
size and route-plan count alongside rendering visibility counts. CPU timings
from the test fixture use synthetic terrain and are not a game FPS benchmark.
Actual visual quality and performance still need an in-game check.
