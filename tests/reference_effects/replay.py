from lightvm import Context, Pixels, Parameter

class Replay:
    """Execute only trusted test/examples; compilation itself never does this."""
    def __init__(self, source, count):
        namespace = {}
        exec(compile(source, '<trusted-reference>', 'exec'), namespace)
        self.parameters = {v.name: v for v in namespace.values() if isinstance(v, Parameter)}
        self.handlers = {v.lightvm_trigger: v for v in namespace.values() if callable(v) and hasattr(v, 'lightvm_trigger')}
        self.pixels = Pixels()
        self.pixels.bind(count)
        self.time = 0
        self.pixels.begin()
        if 0 in self.handlers:
            self.handlers[0](Context(0, 0, self.pixels))
        self.pixels.commit()

    def step(self, delta_ms):
        self.time += delta_ms * 1000
        self.pixels.begin()
        self.handlers[1](Context(self.time, delta_ms * 1000, self.pixels))
        self.pixels.commit()
        return b''.join(c.to_bytes(3, 'big') for c in self.pixels.committed)
