-- Drives the UI from gameplay WITHOUT touching a single widget.
--
-- The idea: a script writes DATA, and any element carrying a UI Binding component that names the same
-- key follows it. Nothing looks an element up, so renaming or restyling a widget can never break this
-- file — and the UI keeps working in the editor (an unset key falls back to the authored value).
--
-- Showcases the bridge:
--   ui.set( key, value )      -- number / string / bool, or ( key, r, g, b ) for a colour
--   ui.get( key ) / ui.has()  -- read it back
--   ui.send( msg )            -- raise a UI message yourself
--   OnUIMessage( msg )        -- hear every button action, pointer event and drop the canvas produced
--
-- Attach to any entity in the MainMenu scene (Details -> Add Component -> Script) and press Play:
-- the profile name, the server load bar and its caption start moving.

Properties = {
    PlayerName = "Nico_Bellic", -- written into player.name on start
    Latency    = 32.0,          -- ms, shown in the server caption
}

local t = 0.0

function OnStart()
    ui.set( "player.name", Properties.PlayerName )
    ui.set( "server.load", 0.85 )
    log( "[UIDataBridge] bound keys: player.name, server.load, server.caption" )
end

function OnUpdate( dt )
    t = t + dt

    -- A live server load: the bar and its caption read the SAME key, formatted differently.
    local load = 0.6 + 0.25 * math.sin( t * 0.8 )
    ui.set( "server.load", load )
    ui.set( "server.caption", string.format( "%d/1000    -    %d ms", math.floor( load * 1000 ),
                                             math.floor( Properties.Latency ) ) )
end

-- Every UI message lands here: button actions (including screen:Settings from a ShowScreen button),
-- pointer enter/exit, and drag-and-drop drops.
function OnUIMessage( msg )
    log( "[UIDataBridge] ui message: " .. msg )

    if msg == "screen:Settings" then
        ui.set( "player.name", "…in settings" )
    elseif msg == "screen:back" then
        ui.set( "player.name", Properties.PlayerName )
    end
end
