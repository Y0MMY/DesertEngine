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
