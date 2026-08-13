# Reference shots

Rendered by the editor itself, not captured off a screen:

```
scripts/MacOS/RunEditor.sh is the interactive path; these came from the shot mode —
Editor --project Desert.deproj --scene <s>.desce --shot out.png \
       --camera 0,200,0 --look 0,0.35,-1 --shot-frames 90
```

| file | what it shows |
|---|---|
| `before-zenith-empty.png` | Looking straight up at Coverage 0.50 — **empty sky**. The whole visible dome fell inside one feature of a weather field whose dominant scale was 30 km. |
| `before-horizon-moire.png` | The first attempt at a fix — shrinking the noise tiles — which fills the zenith and turns the horizon into a radial moire fan, because a 3 km field repeats forty times over 138 km. |
| `after-fairweather.png` | Fair Weather: scattered cumulus, flat bases, lit tops, blue between. |
| `after-partlycloudy.png` | Partly Cloudy. |
| `after-horizon-aerial.png` | The horizon with the fades measured at the cloud rather than at the shell entry, and their range derived from the layer geometry — the far field recedes into haze instead of ending on a white wall. |
| `after-storm.png` | Storm, for the other end of the range. |

The frame count matters: the clouds accumulate over about ten frames, so a shot
taken on frame one is a picture of the dither rather than of the sky.

## v3 — the Nubis-Cubed parity pass (REQUIREMENTS_CLOUDS.md §10)

| file | what it shows |
|---|---|
| `v3-partlycloudy-mid.png` | Same camera as `after-partlycloudy.png`: the hemispheric ambient (CLD-100/101) — lit faces white, bases luminous grey, no slate-blue cast. Lit:shadow ≈ 2.3:1 in linear (CLD-112). |
| `v3-partlycloudy-zenith.png` | Zenith: alligator detail erosion (CLD-110) — wispy fringes, no empty sky. |
| `v3-partlycloudy-horizon.png` | Horizon: aerial perspective intact, no moire, no shadow-map seam (CLD-103). |
| `v3-storm.png` | Storm: deck interiors stay readable (CLD-104 depth-modulated ambient extinction, 3 MS octaves). |
| `v3-sunset.png` | Sunset: warm direct light survives to the horizon (CLD-102), mauve-grey ambient tracks the sky. |
| `v3-default-scene.png` | The default sandbox scene after the field migration. |
