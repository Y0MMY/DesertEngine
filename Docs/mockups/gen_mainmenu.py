#!/usr/bin/env python3
# Generates a MainMenu.desce scene for DesertEngine.
# Format: { SceneName, Entities:[ {id, parent?, Tag, <ComponentKey>:{fields...}} ] }
# - components at entity top level (ExtraFields); deserialize is PARTIAL (only set fields).
# - vec = JSON array; enum = int index; UUID = uint64 number.
# - child order within a parent = order in Entities array = z-order (earlier = behind).
import json, os

entities = []
_uid = [1000]
def uid():
    _uid[0] += 1
    return _uid[0]

def E(tag, parent=None, **components):
    e = {"id": uid(), "Tag": tag}
    if parent is not None:
        e["parent"] = parent
    e.update(components)   # component keys spread at top level
    entities.append(e)
    return e["id"]

# ---- anchor helpers (design px @ 1920x1080, y-down) ----
def L(amin, amax, omin, omax, minsize=(0,0), clip=False):
    return {"AnchorMin": list(amin), "AnchorMax": list(amax),
            "OffsetMin": list(omin), "OffsetMax": list(omax),
            "CustomMinimumSize": list(minsize), "ClipContents": clip}
FILL = L((0,0),(1,1),(0,0),(0,0))

# ---------------- Canvas ----------------
canvas = E("UI Canvas",
           UICanvas={"ScaleMode":1, "ReferenceWidth":1920.0, "ReferenceHeight":1080.0,
                     "MatchWidthHeight":0.5, "Visible":True})

# ---------------- Background (drawn first = behind) ----------------
E("BG", canvas, UILayout=FILL,
  UIPanel={"Color":[1,1,1], "Opacity":1.0, "CornerRadius":0.0})   # drag a .gif onto Sprite in the editor
E("Scrim", canvas, UILayout=FILL,
  UIPanel={"Color":[0.02,0.027,0.05], "Opacity":0.35, "CornerRadius":0.0})

# ---------------- Top bar ----------------
top = E("TopBar", canvas, UILayout=L((0,0),(1,0),(0,0),(0,80)))
E("Logo", top, UILayout=L((0,0),(0,1),(40,0),(400,0)),
  UIText={"Text":"DESERT[color=#FF7A33]RP[/color]", "RichText":True, "FontSize":34.0,
          "Align":0, "VerticalAlign":1, "Color":[0.93,0.95,0.98]})
E("StatusDot", top, UILayout=L((1,0.5),(1,0.5),(-360,-8),(-344,8)),
  UIPanel={"Circle":True, "Color":[0.24,0.88,0.5], "Opacity":1.0,
           "Pulse":True, "PulseSpeed":2.5, "PulseMin":0.3})
E("StatusText", top, UILayout=L((1,0.5),(1,0.5),(-330,-16),(-40,16)),
  UIText={"Text":"ONLINE   -   Friends 12", "FontSize":16.0, "Align":0,
          "VerticalAlign":1, "Color":[0.62,0.69,0.78]})

# ---------------- Left navigation (vertical layout group) ----------------
nav = E("Nav", canvas,
        UILayout=L((0,0),(0,0),(48,150),(368,720)),
        UILayoutGroup={"Type":0, "Spacing":8.0, "StretchCross":True, "Padding":[0,0,0,0]})

NAV = [("PLAY",True,1),("CHARACTERS",False,1),("SERVERS",False,1),
       ("STORE",False,1),("SETTINGS",False,1),("QUIT",False,3)]
for label, selected, action in NAV:
    btn = {"NormalColor":[0.10,0.12,0.16], "HoverColor":[0.16,0.22,0.30],
           "PressedColor":[0.08,0.10,0.14], "Action":action}
    if selected:
        btn.update({"Selected":True, "SelectedColor":[0.85,0.42,0.18],
                    "SelectedAccent":[1.0,0.55,0.2]})
    b = E("Btn_"+label, nav, UILayout=L((0,0),(0,0),(0,0),(0,0),(0,60)), UIButton=btn)
    # Icon slot: an empty Image block (invisible until you drop a PNG onto its Sprite in the editor).
    E("Ico_"+label, b, UILayout=L((0,0),(0,1),(18,14),(58,-14)),
      UIImage={"Tint":[1.0,1.0,1.0], "Opacity":1.0})
    E("Lbl_"+label, b, UILayout=L((0,0),(1,1),(68,0),(-12,0)),
      UIText={"Text":label, "FontSize":20.0, "Align":0, "VerticalAlign":1,
              "Color":[0.90,0.93,0.98]})

# ---------------- Right column (cards) ----------------
side = E("Side", canvas,
         UILayout=L((1,0),(1,0),(-508,120),(-48,900)),
         UILayoutGroup={"Type":0, "Spacing":16.0, "StretchCross":True, "Padding":[0,0,0,0]})
GLASS = {"Color":[0.055,0.075,0.11], "Opacity":0.55, "CornerRadius":20.0,
         "Shadow":True, "ShadowColor":[0,0,0], "ShadowOffset":[0,6]}

# Profile card
prof = E("Card_Profile", side, UILayout=L((0,0),(0,0),(0,0),(0,0),(0,150)), UIPanel=dict(GLASS))
av = E("Avatar", prof, UILayout=L((0,0),(0,0),(20,20),(110,110)),
       UIPanel={"Circle":True, "Color":[0.16,0.20,0.28], "Opacity":1.0, "CornerRadius":0.0,
                "RingWidth":4.0, "RingColorA":[1.0,0.48,0.15], "RingColorB":[0.18,0.89,1.0]})
E("AvatarIcon", av, UILayout=L((0,0),(1,1),(18,18),(-18,-18)),
  UIImage={"Tint":[1.0,1.0,1.0], "Opacity":1.0})   # drop an avatar/portrait PNG here
E("Name", prof, UILayout=L((0,0),(1,0),(128,26),(-16,58)),
  UIText={"Text":"Nico_Bellic", "FontSize":24.0, "Align":0, "VerticalAlign":1, "Color":[0.93,0.96,1.0]})
E("Stats", prof, UILayout=L((0,0),(1,0),(128,64),(-16,92)),
  UIText={"Text":"Lv. 34    $ 1,248,500", "FontSize":15.0, "Align":0, "VerticalAlign":1, "Color":[0.62,0.69,0.78]})
E("XP", prof, UILayout=L((0,0),(1,0),(128,104),(-16,116)),
  UIProgressBar={"Value":0.68, "Background":[0.10,0.12,0.16], "Fill":[0.95,0.55,0.2], "CornerRadius":6.0})

# Server quick-connect card
srv = E("Card_Server", side, UILayout=L((0,0),(0,0),(0,0),(0,0),(0,190)), UIPanel=dict(GLASS))
E("SrvName", srv, UILayout=L((0,0),(1,0),(20,18),(-20,52)),
  UIText={"Text":"Los Santos RP", "FontSize":22.0, "Align":0, "VerticalAlign":1, "Color":[0.93,0.96,1.0]})
E("SrvStats", srv, UILayout=L((0,0),(1,0),(20,58),(-20,84)),
  UIText={"Text":"847/1000    -    32 ms", "FontSize":15.0, "Align":0, "VerticalAlign":1, "Color":[0.62,0.69,0.78]})
E("SrvBar", srv, UILayout=L((0,0),(1,0),(20,96),(-20,108)),
  UIProgressBar={"Value":0.85, "Background":[0.10,0.12,0.16], "Fill":[0.24,0.88,0.5], "CornerRadius":6.0})
conn = E("BtnConnect", srv, UILayout=L((0,0),(1,0),(20,124),(-20,168)),
         UIButton={"NormalColor":[0.95,0.50,0.20], "HoverColor":[1.0,0.60,0.28],
                   "PressedColor":[0.80,0.42,0.16], "Action":2, "OnClickMessage":""})
E("ConnectLbl", conn, UILayout=FILL,
  UIText={"Text":"CONNECT", "FontSize":18.0, "Align":1, "VerticalAlign":1, "Color":[0.10,0.06,0.03]})

# News card
news = E("Card_News", side, UILayout=L((0,0),(0,0),(0,0),(0,0),(0,150)), UIPanel=dict(GLASS))
NEWS = [("[color=#FF7A33]PATCH[/color]  0.9.1 - housing & car auction",16,44),
        ("[color=#2FE3FF]EVENT[/color]  Weekend races - 5,000,000$ prize",54,82),
        ("[color=#2FE3FF]CLAN[/color]  Vinewood Family recruiting",92,120)]
for i,(txt,y0,y1) in enumerate(NEWS):
    E("News%d"%i, news, UILayout=L((0,0),(1,0),(20,y0),(-20,y1)),
      UIText={"Text":txt, "RichText":True, "FontSize":15.0, "Align":0,
              "VerticalAlign":1, "Color":[0.70,0.77,0.86]})

# ---------------- Bottom bar ----------------
bot = E("BottomBar", canvas, UILayout=L((0,1),(1,1),(0,-52),(0,0)),
        UIPanel={"Color":[0.02,0.027,0.05], "Opacity":0.5, "CornerRadius":0.0})
E("Version", bot, UILayout=L((0,0),(0,1),(40,0),(320,0)),
  UIText={"Text":"DesertRP - build v0.9.1", "FontSize":14.0, "Align":0, "VerticalAlign":1, "Color":[0.62,0.69,0.78]})
E("Ticker", bot, UILayout=L((0,0),(1,1),(340,0),(-340,0)),
  UIText={"Text":"Double XP until Monday        Season 4 starts Aug 15        Join our Discord community",
          "Marquee":True, "MarqueeSpeed":60.0, "FontSize":15.0, "Align":0, "VerticalAlign":1, "Color":[0.70,0.77,0.86]})
E("Hotkeys", bot, UILayout=L((1,0),(1,1),(-320,0),(-40,0)),
  UIText={"Text":"Tab - navigate    Enter - select    Esc - back", "FontSize":13.0,
          "Align":2, "VerticalAlign":1, "Color":[0.55,0.62,0.73]})

scene = {"SceneName":"MainMenu", "Entities":entities}
out = "/Users/daniilsavcenko/Desktop/Programming/C++/DesertEngine/Editor/Resources/Assets/Scenes/MainMenu.desce"
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "w") as f:
    json.dump(scene, f, indent=1, ensure_ascii=False)
print("wrote", out, "with", len(entities), "entities")
