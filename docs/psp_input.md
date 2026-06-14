# PSP Input

The PSP port presents the built-in controls as N64 controller 1 through the
existing `osCont*` interface. Controllers 2-4 remain disconnected.

## Default Mapping

| PSP control | N64 control |
| --- | --- |
| Analog nub | Control Stick |
| Cross | A |
| Circle | B |
| Square | Z |
| Triangle | C-Up |
| L trigger | L |
| R trigger | R |
| Start | Start |
| D-pad | N64 D-pad |
| Select + D-pad | C-buttons |
| Select + Start | Exit the game |

The platform layer reports raw N64-range stick values. The original
`Controller_AddDeadZone` path remains responsible for the game's deadzone and
final clamping, matching the existing controller lifecycle.

The initial implementation is intentionally single-player. A future
multiplayer input backend can continue to feed the same `OSContPad` array
without changing game code.
