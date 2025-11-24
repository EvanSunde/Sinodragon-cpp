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