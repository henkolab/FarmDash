# FarmRun — Multiplayer Farm-Themed Speedrun Obstacle Course

A C++/Qt 6 game where players race through farm-themed obstacle courses over
the local network. Fastest time wins. Features OpenGL rendering, scrolling
camera, A*-pathfinding enemies, UDP LAN discovery, TCP gameplay sync, and a
persistent leaderboard.

## Requirements

- Qt 6.2+ with modules: Core, Gui, Widgets, Network, OpenGL, OpenGLWidgets, Multimedia
- CMake 3.19+
- A C++17 compiler (MSVC 2019+, GCC 9+, Clang 10+)

## Build

### Qt Creator (easiest)
1. Open Qt Creator -> File -> Open File or Project -> select `CMakeLists.txt`.
2. Pick a Qt 6 kit, configure, and press the green Run arrow.

### Command line
```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/<compiler>
cmake --build build -j
./build/FarmRun
```

## Controls

| Key | Action |
|-----|--------|
| WASD / Arrows | Move |
| Space         | Jump (clear low obstacles) |
| Mouse / J     | Shoot in facing direction |
| Tab (hold)    | Peek leaderboard while running |
| Esc           | Pause / back to menu |

## Multiplayer

- **Host**: Menu -> Multiplayer -> Host. Broadcasts presence on UDP 45454.
- **Discover/Join**: Menu -> Multiplayer -> Discover. Lists hosts on the LAN.
- All clients receive a synchronized START countdown.
- Gameplay state is serialized and broadcast via the host's TCP server (Qt
  `QDataStream`); clients send input deltas. Position interpolation is applied
  client-side.

## Rubric coverage

- **Menus**: Single-player, MP host/join/discover, leaderboard view.
- **Rendering**: QOpenGLWidget with both fixed-screen HUD and a scrolling
  world camera that follows the player.
- **Course**: grass / mud / water / fence terrains; gates, hay bales, mud pits,
  pressure plates; targets to shoot, keys to collect, friendly farm animals
  to avoid harming.
- **Enemies**: Chickens (small/fast), Geese (medium/aggressive), Bulls (big/
  strong/charge), Crows (flying), Scarecrows (slow/strong), Foxes (fast/weak).
- **Networking**: UDP broadcast discovery, TCP gameplay channel, synchronized
  countdown, per-frame state delta serialization.
- **Pathfinding**: A* on a coarse grid; recomputed when the player or an
  obstacle moves enough to invalidate the cached path.
- **Balance**: Carrot speed-boost, milk shield, egg projectile pickups; player
  collisions push but don't damage; randomized enemy spawns each course.
- **Awesomeness**: Procedurally synthesized chicken cluck / cow moo / coin
  jingle SFX so no asset files are required.

## Project layout

```
src/
  main.cpp               app entry
  MainWindow.*           QStackedWidget shell switching menu <-> game
  MenuWidget.*           main menu, host/join/discover, leaderboard view
  GameWidget.*           QOpenGLWidget: rendering + input + game loop
  GameWorld.*            entities, course, physics, collisions
  Entities.*             Player + 6 enemy classes + Target/Key/Friendly/Pickup
  Pathfinding.*          A* with dynamic recompute
  NetworkManager.*       UDP discovery + TCP host/client
  Protocol.*             QDataStream message (de)serialization
  Leaderboard.*          JSON-backed persistent scores
  Audio.*                runtime-synthesized WAV SFX via QSoundEffect
```
