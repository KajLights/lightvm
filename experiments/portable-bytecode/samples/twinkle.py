from lightvm_poc import parameter, rgb, hash01, shade
seed = parameter(1)
density = parameter(0.15)
lifetime = parameter(1.5)
@shade
def pixel():
    phase = (time * density / lifetime + hash01(index, seed)) % 1.0
    if phase < density:
        remaining = 1.0 - phase / density
        brightness = remaining * remaining * remaining
        return rgb(int(255.0 * brightness), int(220.0 * brightness), int(160.0 * brightness))
    return rgb(0, 0, 0)
