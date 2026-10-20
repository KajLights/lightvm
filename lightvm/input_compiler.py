"""Static live-input declarations. Guest declarations are parsed, never run."""
import ast
import math
import re
import struct

from .compiler_core import I, F, C, B


class InputCompiler:
    def input_setup(self):
        self.input_symbols = {}
        self.input_manifest = []

    def input_declaration(self, target, call, node):
        if not isinstance(target, ast.Name) or target.id in self.imports or target.id in self.input_symbols or target.id in self.helpers or target.id in self.artwork or target.id in self.sprite_definitions:
            self.fail(node, 'Input needs a unique module-level name')
        args = self.arguments(call, ('name', 'default', 'minimum', 'maximum', 'unit', 'driver', 'control'),
                              {'default': 0.0, 'minimum': None, 'maximum': None, 'unit': '', 'driver': None, 'control': None})
        try:
            name = ast.literal_eval(args['name'])
            unit = ast.literal_eval(args['unit'])
        except (ValueError, TypeError):
            self.fail(node, 'input name and unit must be literal strings')
        if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9_.-]{1,32}', name) or any(item['name'] == name for item in self.input_manifest):
            self.fail(node, 'input names must be unique and contain 1..32 letters, digits, dots, underscores or hyphens')
        if not isinstance(unit, str) or not re.fullmatch(r'[A-Za-z0-9_./%-]{0,16}', unit):
            self.fail(node, 'input unit must be a short literal label')
        if len(self.input_manifest) >= 8:
            self.fail(node, 'input limit exceeded (8)')
        value_node = args['default']
        if isinstance(value_node, ast.Call) and getattr(value_node.func, 'id', '') == 'rgb':
            value, typ = self.colour_literal(value_node), C
        else:
            try: value = ast.literal_eval(value_node)
            except (ValueError, TypeError): self.fail(node, 'input defaults must be scalar literals')
            typ = B if type(value) is bool else I if type(value) is int else F if type(value) is float else None
            if typ is None: self.fail(node, 'input defaults must be float, int, bool or rgb literals')
            if typ == I and not -(1 << 31) <= value < (1 << 31): self.fail(node, 'input default exceeds int32')
            if typ == F:
                try: value = struct.unpack('<f', struct.pack('<f', value))[0]
                except OverflowError: self.fail(node, 'input default exceeds binary32')
                if not math.isfinite(value): self.fail(node, 'input default must be finite binary32')
        item = {'name': name, 'type': {I: 'int', F: 'float', C: 'rgb8', B: 'bool'}[typ], 'default': value}
        if unit: item['unit'] = unit
        for key in ('minimum', 'maximum'):
            try: bound = ast.literal_eval(args[key])
            except (ValueError, TypeError): self.fail(node, 'input bounds must be numeric literals')
            if bound is None: continue
            if typ not in (I, F) or type(bound) not in (int, float) or (typ == I and type(bound) is not int) or not math.isfinite(bound):
                self.fail(node, 'input bounds must match their numeric type')
            if typ == I and not -(1 << 31) <= bound < (1 << 31): self.fail(node, 'input bound exceeds int32')
            item[key] = struct.unpack('<f', struct.pack('<f', bound))[0] if typ == F else bound
            if not math.isfinite(item[key]): self.fail(node, 'input bound must fit binary32')
        if value < item.get('minimum', value) or value > item.get('maximum', value):
            self.fail(node, 'input default is outside its bounds')
        try: control = ast.literal_eval(args['control'])
        except (ValueError, TypeError): self.fail(node, 'input control must be a literal')
        if control is not None:
            if not ((control == 'color' and typ == C) or
                    (control == 'brightness' and typ == F and item.get('minimum', 0) <= 0 and item.get('maximum', 1) >= 1)):
                self.fail(node, 'light control must match the input type and bounds')
            item['control'] = control
        driver = args['driver']
        if not (isinstance(driver, ast.Constant) and driver.value is None):
            if control is not None:
                self.fail(node, 'choose one default control or timer')
            if typ != F or not isinstance(driver, ast.Call) or self.imports.get(getattr(driver.func, 'id', '')) != 'Ramp':
                self.fail(node, 'default input drivers must be Ramp descriptions on float inputs')
            params = self.arguments(driver, ('duration', 'mode', 'easing'), {'mode': 'once', 'easing': 'linear'})
            try: params = {key: ast.literal_eval(value) for key, value in params.items()}
            except (ValueError, TypeError): self.fail(node, 'Ramp options must be literals')
            duration = params['duration']
            if type(duration) not in (int, float) or not math.isfinite(duration) or duration <= 0 or duration > 1e9 or params['mode'] not in ('once', 'repeat', 'pingpong') or params['easing'] not in ('linear', 'smoothstep'):
                self.fail(node, 'invalid Ramp duration, mode or easing')
            if item.get('minimum', 0.0) > 0.0 or item.get('maximum', 1.0) < 1.0:
                self.fail(node, 'default Ramp requires an input domain containing 0..1')
            item['driver'] = {'kind': 'ramp', **params}
        self.input_symbols[target.id] = (len(self.input_manifest), typ)
        self.input_manifest.append(item)

    def input_read(self, node):
        if not isinstance(node, ast.Call) or not isinstance(node.func, ast.Attribute) or self.capability(node.func.value) != 'runtime' or node.func.attr != 'input':
            return None
        if self.stage != 1 or (self.sprite_context and self.sprite_context[2] != 'render'):
            self.fail(node, 'live inputs are only readable in render callbacks')
        if len(node.args) != 1 or node.keywords or not isinstance(node.args[0], ast.Name) or node.args[0].id not in self.input_symbols:
            self.fail(node, 'runtime.input requires one statically declared Input')
        index, typ = self.input_symbols[node.args[0].id]
        return self.native('input_' + {I: 'int', F: 'float', C: 'color', B: 'bool'}[typ], [self.literal(index)], node)
