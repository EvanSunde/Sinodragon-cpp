# Lighting Configuration Presets Information
This document describes the various effects and the configuration options available for each preset in your settings file.

## Effect Types and Options

### static_color

    - Description: A single solid color illuminated across the entire device.
    - Options:
        color (Hex color code): The desired color (e.g., #FFFF40 for bright yellow, #FF0000 for bright red, #95e000 for lime green).

### rainbow_wave

    - Description: A dynamic, smoothly moving wave of rainbow colors.
    - Options:
        - speed (Float): Controls how quickly the wave moves across the surface.
        - scale (Float): Determines the size or frequency of the color bands in the wave.
        - tint (Hex color code, optional): Overlays a specific color onto the rainbow wave.
        - tint_mix (Float, optional): How strongly the tint color is blended with the rainbow effect (0.0 to 1.0).

### star_matrix

    - Description: A dark background with randomly appearing, sparkling "stars" that fade in and out.
    - Options:
        - star (Hex color code): The color of the sparkling stars (e.g., #FFFFFF for white).
        - background (Hex color code): The background color (e.g., #101010 for dark gray/near black).
        - density (Float): How frequently stars spawn or how many are visible at once.
        - speed (Float): Controls the rate at which stars appear and fade away.

### liquid_plasma

    - Description: A smooth, flowing, organic liquid or plasma effect using a gradient of specified colors. This effect can be made reactive to input.
    - Options:
        - speed (Float): Speed of the overall motion.
        - scale (Float): Size of the plasma waves.
        - wave_complexity (Integer): The complexity or turbulence of the liquid motion.
        - mix_mode (String): How colors blend (e.g., linear).
        - colors (Comma-separated Hex color codes): The primary colors used in the gradient (e.g., #E0FFFF,#00BFFF,#1E90FF).
        - reactive (Boolean, optional): Set to true to enable a secondary effect triggered by user input.
        - reactive_color (Hex color code, optional): Color of the reactive ripple/splash.
        - reactive_history (Float, optional): How long the reactive effect persists.
        - reactive_decay (Float, optional): How quickly the reactive effect fades.
        - reactive_spread (Float, optional): How far the reactive effect spreads.
        - reactive_intensity (Float, optional): How strong the reactive effect is.

### reaction_diffusion

    - Description: A complex, algorithmic "Gray-Scott" cellular automaton simulation, creating organic, self-organizing patterns. This can be made reactive by "injecting" substance B.
    - Options:
        - width, height (Integers): The resolution of the simulation grid.
        - du, dv (Floats): Diffusion rates for the two substances.
        - feed (Float): The feed rate parameter for the Gray-Scott model.
        - kill (Float): The kill rate parameter for the Gray-Scott model.
        - steps (Integer): The number of simulation steps per frame.
        - zoom (Float): Controls the visual size of the pattern.
        - speed (Float): Overall speed of the pattern evolution.
        - color_a, color_b (Hex color codes): The two main colors that the pattern transitions between.
        - reactive (Boolean, optional): Set to true to enable injection of the color_b substance via input.
        - injection_amount (Float, optional): How much substance B is injected.
        - injection_radius (Float, optional): Radius of the injection area.
        - injection_decay (Float, optional): How quickly the injection fades.
        - injection_history (Float, optional): How long the injection effect persists.

### smoke

    - Description: A smooth, procedural noise-based effect that simulates flowing smoke or clouds. This can also be made reactive.
    - Options:
        - speed (Float): Speed of the smoke motion.
        - scale (Float): Visual size/scale of the smoke clouds.
        - octaves (Integer): Number of noise layers for detail.
        - persistence (Float): How much each layer contributes to the overall noise.
        - lacunarity (Float): Frequency multiplier for each layer.
        - drift_x, drift_y (Floats): Defines the primary direction and speed of the smoke flow.
        - contrast (Float): Adjusts the contrast between the high and low colors.
        - color_low, color_high (Hex color codes): The color range for the smoke effect.
        - reactive (Boolean, optional): Set to true to enable a secondary effect triggered by user input.
        - reactive_color (Hex color code, optional): Color of the reactive effect.
        - reactive_history (Float, optional): How long the reactive effect persists.
        - reactive_decay (Float, optional): How quickly the reactive effect fades.
        - reactive_spread (Float, optional): How far the reactive effect spreads.
        - reactive_intensity (Float, optional): How strong the reactive effect is.

### doom_fire

    - Description: A classic, pixelated simulation of fire, inspired by the original Doom game engine effect.
    - Options:
        - speed (Float): Overall speed and intensity of the fire simulation.
        - cooling (Float): How quickly the "heat" dissipates; lower values make the fire taller/hotter.
        - spark_chance (Float): Controls how often new sparks appear at the bottom.
        - spark_intensity (Float): Controls how bright the sparks are.

### reactive_ripple

    - Description: A subtle background that creates a ripple effect from the point of user input (e.g., key presses).
    - Options:
        - wave_speed (Float): How quickly the ripple expands.
        - decay_time (Float): How long it takes for the ripple to fade away.
        - thickness (Float): The visual width of the ripple ring.
        - history (Float): How long the input data is stored (affects how quickly multiple key presses stack).
        - intensity (Float): The brightness of the ripple effect.
        - color (Hex color code): The color of the ripples (e.g., #00AAFF for cyan/blue).
        - base_color (Hex color code): The background color when inactive (e.g., #000010 for very dark blue/black).
### space_colonization

    - Description: Branching structures that grow towards randomly scattered attractors, like roots or lightning. Keystrokes seed new roots.
    - Options:
        - attractors (Integer): How many growth targets are scattered across the board.
        - kill_dist (Float): Distance at which a branch consumes an attractor.
        - influence_dist (Float): How far an attractor can pull a branch towards it.
        - segment_len (Float): Length of one growth step.
        - growth_interval (Float): Seconds between growth ticks.
        - lifespan (Float): Seconds a node stays lit before it starts to fade.
        - fade_time (Float): Seconds to fade out after the lifespan expires.
        - thickness (Float): Base branch radius.
        - color_root, color_tip (Hex color codes): Gradient along distance from the root.
        - reactive (Boolean): Seed new roots from keystrokes. Default true.

### typing_heatmap

    - Description: Shows where you actually type. Each press adds heat to its key, heat bleeds into neighbours and decays over time, and the palette runs cold to hot. A layer rather than a game -- stack it over a dim base with blend = "screen".
    - Options:
        - half_life (Float): Seconds for a key's heat to fall to 1/e of its value.
        - gain (Float): Heat added per keypress.
        - spread (Float, 0.0 to 1.0): How much heat bleeds into physically adjacent keys.
        - ceiling (Float): Heat value that counts as fully hot.
        - palette (Comma-separated hex codes): Cold to hot. Defaults to blue through cyan and yellow to white.
        - color_cold (Hex color code): Colour for keys with no heat at all.

### system_meter

    - Description: Draws a value as a bar across a named run of keys.
    - Options:
        - metric (String): cpu, memory, load, battery, or any name fed by `sinoctl metric <name> <0..1>`.
        - bar_keys (Array of key labels): The keys the bar fills, in order. Without it the whole board is used and the layer's own zones/keys mask decides what shows.
        - smoothing (Float, 0.0 to 0.99): How much of the previous reading to keep. Higher is calmer.
        - invert (Boolean): Fill from full towards empty instead.
        - pulse_when_charging (Boolean): Battery only. Default true.
        - color_low, color_mid, color_high (Hex color codes): The gradient along the bar.
        - color_empty (Hex color code): Unlit cells.
    - Note: unlit cells are painted color_empty, so stacked meters need blend = "add" or each will erase the one below.

### status_light

    - Description: Shows a named state pushed in over the control socket, so a CI script or a git hook can drive the keyboard.
    - Options:
        - signal (String): The state name to watch, e.g. "build".
        - keys (Array of key labels): Which keys to light. Defaults to the whole board.
        - ok_timeout (Float): Seconds after which a settled "ok" fades out. 0 keeps it lit.
        - color_<state> (Hex color code): Colour for a state. Built in: ok, warn, fail, busy.
        - style_<state> (String): solid, pulse or sweep.
        - color_off (Hex color code): Shown when the state is unset or "off".

### Games: snake, tetris, pong, life

    - Description: Take the whole keyboard over while running. Start and stop them with `sinoctl game <name> start|stop`; declare one as a layer in its own profile to make it available. All of them need the keycodes CSV, which is how they read input.
    - Options:
        - snake: step_interval (Float) -- seconds per move.
        - tetris: step_interval (Float), background, palette (Comma-separated hex codes).
        - pong: two players by default -- left paddle W/S, right paddle Up/Down. ball_speed, paddle_height (Floats); win_score (Integer, default 7); left_up, left_down, right_up, right_down (key labels); opponent ("human" default, or "ai"/"cpu" for a computer right-hand player, which then uses ai_speed); color_left, color_right, color_ball, background (color_player/color_ai still accepted).
        - life: step_interval (Float), density (Float, 0.0 to 1.0 seeding density); color_alive, color_new, background.
