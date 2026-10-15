from lightvm import parameter, rgb, hash01, shade
seed = parameter(1, minimum=0, maximum=65520)
density = parameter(0.15, minimum=0.01, maximum=1.0)
lifetime = parameter(1.5, minimum=0.01, maximum=60.0)
@shade
def pixel():
    phase = (time * density / lifetime + hash01(index, seed)) % 1.0
    if phase < density:
        remaining = 1.0 - phase / density
        brightness = remaining * remaining * remaining
        return rgb(int(255.0 * brightness), int(220.0 * brightness), int(160.0 * brightness))
    return rgb(0, 0, 0)
