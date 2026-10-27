import { parse } from 'acorn';
import { SDK_IMPORTS, SourceError, type LoweredSource, type SourceLocation } from './source';

// ESTree nodes are consumed through an explicit allowlist. Nothing in the guest
// module is evaluated, imported, or handed to Function/eval.
type Node = any;
type Binding = { name: string; constant: boolean; node?: Node; kind?: string; countBound?: boolean; floatCounter?: boolean };
const aliases: Record<string, string> = {
  input: 'Input', ramp: 'Ramp', hash01: 'hash01', colorwheel: 'colorwheel', hueDegrees: 'hue_degrees', picoColorwheel: 'pico_colorwheel',
  fitStart: 'fit_start', fitEnd: 'fit_end', quadraticIn: 'quadratic_in', quadraticOut: 'quadratic_out', quadraticInOut: 'quadratic_in_out',
  cubicIn: 'cubic_in', cubicOut: 'cubic_out', cubicInOut: 'cubic_in_out', copyComposition: 'copy_composition',
  hasChannel: 'has_channel', setChannel: 'set_channel', setAlpha: 'set_alpha', multiplyAlpha: 'multiply_alpha',
  fixedBits: 'fixed_bits', fixedFromBits: 'fixed_from_bits', fixedSqrt: 'fixed_sqrt',
  sineIn: 'sine_in', sineOut: 'sine_out', sineInOut: 'sine_in_out', circularIn: 'circular_in', circularOut: 'circular_out', circularInOut: 'circular_in_out', exponentialIn: 'exponential_in', exponentialOut: 'exponential_out', exponentialInOut: 'exponential_in_out',
  copyFrom: 'copy_from',
  wrapIndex: 'wrap_index',
};
const calls = new Set('setting Input Ramp rgb hsv hash01 perlin2 perlin3 simplex2 simplex3 PixelArray Sprite SpriteBatch Transform ColorTransform red green blue colorwheel pico_colorwheel hue_degrees repeat pingpong clamp01 lerp fit_start fit_end linear smoothstep quadratic_in quadratic_out quadratic_in_out cubic_in cubic_out cubic_in_out sine_in sine_out sine_in_out circular_in circular_out circular_in_out exponential_in exponential_out exponential_in_out crossfade wipe fixed fixed_bits fixed_from_bits fixed_sqrt FixedArray floor ceil trunc'.split(' '));
calls.add('wrap_index');
calls.add('wrap');
const keywords = new Set('False None True and as assert async await break class continue def del elif else except finally for from global if import in is lambda nonlocal not or pass raise return try while with yield'.split(' '));
const methods = new Set('input fill clear draw copy copy_from transform point colorwheel gradient palette mix copy_composition has_channel set_channel set_alpha multiply_alpha spawn render remove'.split(' '));
// Equal-sign operands avoid losing a tiny f32 remainder when correcting a
// floored result. Only minimum i32/fixed values wrap under negation; that arm
// uses exact integer-backed correction. Multiplying by zero retains f32 sign.
const helperSource = `
def _js_remainder(a, b):
    zero = (a - a) + (b - b)
    if a == zero or ((a < zero) == (b < zero)):
        remainder = a % b
        return a * zero if remainder == zero else remainder
    opposite = -a
    if opposite < zero and a < zero:
        remainder = a % b
        return remainder - b if remainder != zero else remainder
    return (a * zero) - (opposite % b)
`;

export function lowerJavaScript(source: string): LoweredSource {
  if (new TextEncoder().encode(source).length > 65536) throw new SourceError('Keep source below 64 KiB.');
  let tree: Node;
  try { tree = parse(source, { ecmaVersion: 2022, sourceType: 'module', locations: true }); }
  catch (error: any) { throw new SourceError(error.message, { line: error.loc?.line ?? 1, column: (error.loc?.column ?? 0) + 1 }); }
  return new JavaScriptFrontend().lower(tree);
}

class JavaScriptFrontend {
  lines: string[] = []; locations: SourceLocation[] = []; depth = 0; serial = 0;
  names: Record<string, string> = {};
  scopes: Map<string, Binding>[] = [new Map()]; functions: { name: string; fn: Node; role?: string }[] = [];
  globals: { node: Node; binding: Binding }[] = []; loops: (Node | null)[] = []; expanding = new Set<string>();
  profile = 'animation'; roles = new Set<string>();
  fail(n: Node, message: string): never { throw new SourceError(message, this.location(n)); }
  location(n: Node): SourceLocation { return { line: n?.loc?.start.line ?? 1, column: (n?.loc?.start.column ?? 0) + 1 }; }
  emit(text: string, n?: Node) { for (const line of text.split('\n')) { this.lines.push('    '.repeat(this.depth) + line); this.locations.push(this.location(n)); } }
  binding(name: string) { for (const scope of [...this.scopes].reverse()) { const b = scope.get(name); if (b) return b; } }
  declare(n: Node, constant: boolean, kind?: string, name?: string): Binding {
    if (n?.type !== 'Identifier') this.fail(n, 'Use a simple name here; destructuring is not supported.');
    const scope = this.scopes.at(-1)!;
    if (scope.has(n.name)) this.fail(n, `The name “${n.name}” is already declared in this scope.`);
    const b = { name: name ?? `_js_v${++this.serial}`, constant, kind }; scope.set(n.name, b); this.names[b.name] = n.name; return b;
  }
  addFunction(name: string, fn: Node, role?: string) {
    if (fn.async || fn.generator) this.fail(fn, 'Callbacks run synchronously; async and generators are not supported.');
    if (role) { if (this.roles.has(role)) this.fail(fn, `Use one ${role} callback.`); this.roles.add(role); }
    this.functions.push({ name, fn, role });
  }
  lower(tree: Node): LoweredSource {
    // Discover module functions first, allowing ordinary JavaScript hoisting.
    for (const item of tree.body) {
      const n = item.type === 'ExportNamedDeclaration' ? item.declaration : item;
      if (n?.type === 'FunctionDeclaration') this.declare(n.id, true, 'function', `_js_fn${++this.serial}`);
      if (n?.type === 'VariableDeclaration') for (const d of n.declarations) {
        if (!d.init) this.fail(d, 'Initialize each variable when declaring it.');
        this.declare(d.id, true, ['ArrowFunctionExpression', 'FunctionExpression'].includes(d.init.type) ? 'function' : 'module');
      }
    }
    for (const item of tree.body) this.module(item);
    if (!this.roles.has('render')) this.fail(tree, 'Add export function render(state, runtime, pixels) { … } or animation({ render(...) { … } }).');
    this.emit(SDK_IMPORTS); this.emit(`profile("${this.profile}")`); this.emit(helperSource);
    // Sprite descriptions refer to helper callbacks, so helpers precede resources.
    for (const f of this.functions.filter(f => !f.role)) this.function(f);
    for (const { node, binding } of this.globals) this.emit(`${binding.name} = ${this.expr(node)}`, node);
    for (const f of this.functions.filter(f => f.role)) this.function(f);
    return { python: this.lines.join('\n') + '\n', locations: this.locations, names: this.names };
  }
  module(item: Node) {
    const exported = item.type === 'ExportNamedDeclaration';
    const n = exported ? item.declaration : item;
    if (!n) this.fail(item, 'Export callbacks or a profile declaration directly.');
    if (n.type === 'ImportDeclaration') {
      if (!['lightvm', 'lightvm/motion', 'lightvm/easing', 'lightvm/transitions'].includes(n.source.value)) this.fail(n, 'Only LightVM SDK imports are supported. SDK names are also available without imports.');
      for (const s of n.specifiers) {
        const imported = aliases[s.imported?.name] ?? s.imported?.name;
        if (s.type !== 'ImportSpecifier' || !calls.has(imported)) this.fail(s, 'Use a named LightVM SDK import.');
        this.declare(s.local, true, 'sdk', imported);
      }
    } else if (n.type === 'FunctionDeclaration') {
      const role = exported && ['init', 'render'].includes(n.id.name) ? n.id.name : undefined;
      this.addFunction(this.binding(n.id.name)!.name, n, role);
    } else if (n.type === 'VariableDeclaration') {
      if (n.kind !== 'const') this.fail(n, 'Use const at module scope. Put changing values on state in init().');
      for (const d of n.declarations) {
        const b = this.binding(d.id.name)!;
        if (exported && d.id.name === 'profile') {
          if (d.init.type !== 'Literal' || !['animation', 'transition'].includes(d.init.value)) this.fail(d, 'profile must be "animation" or "transition".');
          this.profile = d.init.value;
        } else if (b.kind === 'function') this.addFunction(b.name, d.init, exported && ['init', 'render'].includes(d.id.name) ? d.id.name : undefined);
        else if (['CallExpression', 'NewExpression'].includes(d.init.type) && ['Input', 'PixelArray', 'Sprite'].includes(this.callName(d.init.callee))) {
          b.kind = 'resource'; this.globals.push({ binding: b, node: d.init });
        } else {
          const pure = (v: Node): boolean => Boolean(v && (['Literal', 'Identifier'].includes(v.type) || v.type === 'UnaryExpression' && pure(v.argument) || v.type === 'BinaryExpression' && pure(v.left) && pure(v.right) || v.type === 'ArrayExpression' && v.elements.every(pure) ||
            v.type === 'CallExpression' && ['rgb', 'int', 'float', 'Number', 'fixed', 'fixed_bits', 'fixed_from_bits', 'fixed_sqrt'].includes(this.callName(v.callee)) && v.arguments.every(pure)));
          if (!pure(d.init)) this.fail(d.init, 'Module constants need literal values or arithmetic. Create settings and mutable resources in init().');
          b.node = d.init;
        }
      }
    } else if ((n.type === 'ExpressionStatement' && n.expression.type === 'CallExpression') || n.type === 'ExportDefaultDeclaration') {
      const call = n.type === 'ExportDefaultDeclaration' ? n.declaration : n.expression;
      if (call.type !== 'CallExpression' || !['animation', 'transition'].includes(call.callee.name) || call.arguments.length !== 1) this.fail(n, 'Use animation({ init, render }) or transition({ init, render }).');
      this.profile = call.callee.name;
      for (const prop of this.properties(call.arguments[0])) {
        const role = this.key(prop);
        if (!['init', 'render'].includes(role)) this.fail(prop, 'Program descriptions contain init and render callbacks.');
        let fn = prop.value;
        if (fn.type === 'Identifier') {
          const found = this.functions.find(f => f.name === this.binding(fn.name)?.name);
          if (!found) this.fail(fn, 'Declare the callback before this description.');
          if (this.roles.has(role)) this.fail(fn, `Use one ${role} callback.`);
          found.role = role; this.roles.add(role);
        } else if (['FunctionExpression', 'ArrowFunctionExpression'].includes(fn.type)) this.addFunction(`_js_entry_${role}`, fn, role);
        else this.fail(fn, 'Use a function for this callback.');
      }
    } else if (n.type !== 'EmptyStatement') this.fail(n, `“${n.type}” is not supported at module scope.`);
  }
  function({ name, fn, role }: { name: string; fn: Node; role?: string }) {
    this.scopes.push(new Map());
    const params = fn.params.map((p: Node, i: number) => {
      if (p.type === 'AssignmentPattern') {
        if (role) this.fail(p, 'Starting and frame functions receive their values automatically. Leave out default values here.');
        const initial = this.expr(p.right), binding = this.declare(p.left, false);
        return `${binding.name}=${initial}`;
      }
      return this.declare(p, false, role ? ['state', 'runtime', 'pixels'][i] : undefined).name;
    });
    if (role && params.length !== (role === 'init' ? 2 : 3)) this.fail(fn, `${role} takes ${role === 'init' ? '(state, runtime)' : '(state, runtime, pixels)'}.`);
    if (role) this.emit('@' + role, fn);
    this.emit(`def ${name}(${params.join(', ')}):`, fn); this.depth++;
    if (fn.body.type === 'BlockStatement') this.body(fn.body.body);
    else this.emit(`return ${this.expr(fn.body)}`, fn.body);
    this.depth--; this.scopes.pop();
  }
  body(nodes: Node[]) {
    // Predeclare lexical names so reads before initialization cannot accidentally
    // resolve to an outer variable (JavaScript's temporal dead zone).
    for (const n of nodes) if (n.type === 'VariableDeclaration') for (const d of n.declarations) this.declare(d.id, n.kind === 'const', 'pending');
    if (!nodes.length) this.emit('pass'); else for (const n of nodes) this.statement(n);
  }
  block(n: Node) { this.scopes.push(new Map()); this.body(n.type === 'BlockStatement' ? n.body : [n]); this.scopes.pop(); }
  statement(n: Node) {
    switch (n.type) {
      case 'EmptyStatement': this.emit('pass', n); break;
      case 'BlockStatement': this.block(n); break;
      case 'VariableDeclaration':
        if (n.kind === 'var') this.fail(n, 'Use let or const so each variable has a clear block scope.');
        for (const d of n.declarations) {
          const b = this.scopes.at(-1)!.get(d.id.name) ?? this.declare(d.id, n.kind === 'const', 'pending');
          if (!d.init) {
            if (n.kind !== 'let') this.fail(d, 'Give a constant its value when you declare it.');
            b.kind = 'uninitialized'; this.emit('pass', d); continue;
          }
          const value = this.expr(d.init); b.countBound = n.kind === 'const' && this.safeCountBound(d.init);
          b.kind = undefined; this.emit(`${b.name} = ${value}`, d);
        } break;
      case 'ExpressionStatement': this.expressionStatement(n.expression); break;
      case 'IfStatement':
        this.emit(`if ${this.expr(n.test)}:`, n.test); this.depth++; this.block(n.consequent); this.depth--;
        if (n.alternate) { this.emit('else:', n.alternate); this.depth++; this.block(n.alternate); this.depth--; } break;
      case 'WhileStatement':
        this.emit(`while ${this.expr(n.test)}:`, n); this.depth++; this.loops.push(null); this.block(n.body); this.loops.pop(); this.depth--; break;
      case 'ForStatement':
        if (this.rangeLoop(n)) break;
        this.scopes.push(new Map());
        if (n.init) n.init.type === 'VariableDeclaration' ? this.statement(n.init) : this.expressionStatement(n.init);
        this.emit(`while ${n.test ? this.expr(n.test) : 'True'}:`, n); this.depth++; this.loops.push(n.update);
        this.block(n.body); if (n.update) this.expressionStatement(n.update); this.loops.pop(); this.depth--; this.scopes.pop(); break;
      case 'ForOfStatement': {
        const declaration = n.left.type === 'VariableDeclaration';
        if (n.await || declaration && (n.left.declarations.length !== 1 || n.left.kind === 'var') || !declaration && n.left.type !== 'Identifier') this.fail(n, 'Use for (const value of array), or a variable declared with let.');
        const bind = () => declaration ? this.declare(n.left.declarations[0].id, n.left.kind === 'const') : { name: this.target(n.left) };
        if (n.right.type === 'CallExpression' && n.right.callee.type === 'Identifier' && n.right.callee.name === 'range' && !this.binding('range')) {
          if (n.right.arguments.length < 1 || n.right.arguments.length > 3 || n.right.arguments.some((a: Node) => a.type === 'SpreadElement')) this.fail(n.right, 'Use range(stop), range(start, stop), or range(start, stop, step).');
          const args = n.right.arguments.map((a: Node) => `int(${this.expr(a)})`).join(', ');
          this.scopes.push(new Map()); const b = bind();
          this.emit(`for ${b.name} in range(${args}):`, n); this.depth++; this.loops.push(null);
          this.block(n.body); this.loops.pop(); this.depth--; this.scopes.pop(); break;
        }
        const array = this.expr(n.right); this.scopes.push(new Map()); const b = bind(), i = `_js_index${++this.serial}`;
        this.emit(`for ${i} in range(len(${array})):`, n); this.depth++; this.emit(`${b.name} = ${array}[${i}]`, n);
        this.loops.push(null); this.block(n.body); this.loops.pop(); this.depth--; this.scopes.pop(); break;
      }
      case 'ContinueStatement':
        if (n.label || !this.loops.length) this.fail(n, 'continue needs an enclosing loop and no label.');
        if (this.loops.at(-1)) this.expressionStatement(this.loops.at(-1)); this.emit('continue', n); break;
      case 'BreakStatement':
        if (n.label || !this.loops.length) this.fail(n, 'break needs an enclosing loop and no label.');
        this.emit('break', n); break;
      case 'ReturnStatement': this.emit(n.argument ? `return ${this.expr(n.argument)}` : 'return', n); break;
      default: this.fail(n, `${n.type} is not supported. Try a helper function, if, for or while.`);
    }
  }
  safeCountBound(b: Node, integerBound = false, allowWideInteger = false): boolean {
    if (b?.type === 'Identifier') {
      const binding = this.binding(b.name);
      return Boolean(binding?.constant && (binding.countBound || binding.node && this.safeCountBound(binding.node, integerBound, allowWideInteger)));
    }
    return b?.type === 'Literal' && Number.isInteger(b.value) && b.value >= 0 && b.value <= (integerBound && allowWideInteger ? 2147483647 : 16777216) ||
      b?.type === 'CallExpression' && this.callName(b.callee) === 'int' && b.arguments.length === 1 && this.safeCountBound(b.arguments[0], true, allowWideInteger) ||
      b?.type === 'MemberExpression' && !b.computed && (this.binding(b.object.name)?.kind === 'pixels' && b.property.name === 'count' || this.binding(b.object.name)?.kind === 'runtime' && b.property.name === 'pixelCount');
  }
  rangeLoop(n: Node): boolean {
    // Whole-valued counters up to 2^24 have exact f32 increments. Keep their
    // observable number type while using a private integer cursor in the VM.
    const d = n.init?.type === 'VariableDeclaration' && n.init.kind === 'let' && n.init.declarations.length === 1 ? n.init.declarations[0] : undefined;
    const integerZero = d?.init?.type === 'CallExpression' && this.callName(d.init.callee) === 'int' && d.init.arguments.length === 1 && d.init.arguments[0].type === 'Literal' && d.init.arguments[0].value === 0;
    const numberZero = d?.init?.type === 'Literal' && d.init.value === 0;
    const update = n.update?.type === 'UpdateExpression' && n.update.operator === '++' && n.update.argument.name === d?.id.name;
    if (!(integerZero || numberZero) || !update || n.test?.type !== 'BinaryExpression' || n.test.operator !== '<' || n.test.left.type !== 'Identifier' || n.test.left.name !== d.id.name) return false;
    if (!this.safeCountBound(n.test.right, false, integerZero)) return false;
    const changesCounter = (v: any): boolean => v && typeof v === 'object' && (
      (v.type === 'AssignmentExpression' && v.left?.name === d.id.name) ||
      (v.type === 'UpdateExpression' && v.argument?.name === d.id.name) ||
      (v.type === 'VariableDeclarator' && v.id?.name === d.id.name) ||
      Object.values(v).some(child => Array.isArray(child) ? child.some(changesCounter) : changesCounter(child)));
    if (changesCounter(n.body)) return false;
    const bound = `int(${this.expr(n.test.right, true)})`;
    this.scopes.push(new Map()); const b = this.declare(d.id, false);
    b.floatCounter = numberZero;
    this.emit(`for ${b.name} in range(${bound}):`, n); this.depth++; this.loops.push(null);
    this.block(n.body); this.loops.pop(); this.depth--; this.scopes.pop(); return true;
  }
  expressionStatement(n: Node) {
    if (n.type === 'AssignmentExpression') {
      const target = this.target(n.left), right = this.expr(n.right);
      if (n.operator === '=') this.emit(`${target} = ${right}`, n);
      else if (['+=', '-=', '*=', '/='].includes(n.operator)) this.emit(`${target} ${n.operator} ${right}`, n);
      else this.fail(n, 'Use =, +=, -=, *= or /=.');
    } else if (n.type === 'UpdateExpression') this.emit(`${this.target(n.argument)} ${n.operator === '++' ? '+=' : '-='} 1`, n);
    else if (n.type === 'CallExpression') {
      if (n.callee.name === 'assert' && n.arguments.length === 1) this.emit(`assert ${this.expr(n.arguments[0])}`, n);
      else this.emit(this.expr(n), n);
    } else this.fail(n, 'Use assignments or function calls as statements. Updates inside expressions are not supported.');
  }
  target(n: Node): string {
    if (n.type === 'Identifier') {
      const b = this.binding(n.name); if (!b) this.fail(n, `Declare “${n.name}” with let first.`);
      if (b.kind === 'pending') this.fail(n, `Initialize “${n.name}” before assigning to it.`);
      if (b.constant) this.fail(n, `“${n.name}” is const. Use let for a value that changes.`); return b.name;
    }
    if (n.type === 'MemberExpression') return this.expr(n, true);
    return this.fail(n, 'Assign to a variable, state field or pixel/array element.');
  }
  callName(n: Node): string {
    if (n.type !== 'Identifier') return '';
    return this.binding(n.name)?.name ?? aliases[n.name] ?? n.name;
  }
  properties(n: Node): Node[] {
    if (n?.type !== 'ObjectExpression') this.fail(n, 'Use a literal options object here.');
    const keys = new Set<string>();
    for (const p of n.properties) {
      if (p.type !== 'Property' || p.computed || p.kind !== 'init') this.fail(p, 'Options need plain named properties; spreads and getters are not supported.');
      const key = this.key(p); if (keys.has(key)) this.fail(p, `Duplicate option “${key}”.`); keys.add(key);
    }
    return n.properties;
  }
  key(p: Node): string { const key = p.key.name ?? p.key.value; if (typeof key !== 'string' || !/^[A-Za-z_][A-Za-z0-9_]*$/.test(key)) this.fail(p, 'Use a simple property name.'); return key; }
  expr(n: Node, integer = false): string {
    if (!n) this.fail(n, 'An expression is missing.');
    switch (n.type) {
      case 'Literal':
        if (typeof n.value === 'number' && Number.isFinite(n.value)) {
          if (integer) { if (!Number.isInteger(n.value)) this.fail(n, 'Use a whole number here.'); return String(n.value); }
          const text = String(n.value); return /[.eE]/.test(text) ? text : text + '.0';
        }
        if (typeof n.value === 'boolean') return n.value ? 'True' : 'False';
        if (typeof n.value === 'string') return JSON.stringify(n.value);
        if (n.value === null && !n.regex) return 'None';
        return this.fail(n, 'Use a finite number, boolean or SDK string literal.');
      case 'Identifier': {
        const b = this.binding(n.name);
        if (!b) this.fail(n, `Unknown name “${n.name}”. Declare it with let or const.`);
        if (b.kind === 'pending') this.fail(n, `Initialize “${n.name}” before reading it.`);
        if (b.node) {
          if (this.expanding.has(n.name)) this.fail(n, 'Constants cannot refer to themselves.');
          this.expanding.add(n.name); const result = this.expr(b.node, integer); this.expanding.delete(n.name); return result;
        }
        return b.floatCounter && !integer ? `float(${b.name})` : b.name;
      }
      case 'ArrayExpression': return `[${n.elements.map((v: Node) => this.expr(v, integer)).join(', ')}]`;
      case 'MemberExpression': {
        if (n.optional) this.fail(n, 'Optional chaining is not supported.');
        if (n.computed) {
          const index = n.property.type === 'Identifier' && this.binding(n.property.name)?.floatCounter ? this.expr(n.property, true) : this.expr(n.property);
          return `${this.expr(n.object)}[int(${index})]`;
        }
        if (n.object.name === 'Math' && !this.binding('Math')) {
          if (n.property.name === 'PI') return 'math.pi';
          if (n.property.name === 'E') return '2.718281828459045';
          this.fail(n, 'Use Math.PI, Math.E or a supported Math function.');
        }
        const property = n.property.name;
        if (keywords.has(property)) this.fail(n, `Choose another field name instead of “${property}”.`);
        const object = this.expr(n.object), kind = this.binding(n.object.name)?.kind;
        if (kind === 'state') return `${object}.${property}`;
        if (property === 'length') return integer ? `len(${object})` : `float(len(${object}))`;
        if (['pixelCount', 'count'].includes(property)) {
          const result = `${object}.${property === 'pixelCount' ? 'pixel_count' : property}`;
          return integer ? result : `float(${result})`;
        }
        return `${object}.${property === 'pixelCount' ? 'pixel_count' : property}`;
      }
      case 'UnaryExpression':
        if (!['+', '-', '!', '~'].includes(n.operator)) this.fail(n, 'Use numeric + / -, boolean ! or i32 ~.');
        return n.operator === '+' ? this.expr(n.argument, integer) : `(${n.operator === '!' ? 'not ' : n.operator}${this.expr(n.argument, integer)})`;
      case 'BinaryExpression': {
        const ops: Record<string, string> = { '+': '+', '-': '-', '*': '*', '/': '/', '**': '**', '<': '<', '<=': '<=', '>': '>', '>=': '>=', '===': '==', '!==': '!=', '&': '&', '|': '|', '^': '^', '<<': '<<', '>>': '>>' };
        if (n.operator === '%') return `_js_remainder(${this.expr(n.left)}, ${this.expr(n.right)})`;
        if (!ops[n.operator]) this.fail(n, 'Use arithmetic, comparisons, === / !== or signed i32 bitwise operations. Coercing equality and >>> are not supported.');
        return `(${this.expr(n.left, integer)} ${ops[n.operator]} ${this.expr(n.right, integer)})`;
      }
      case 'LogicalExpression': {
        if (!['&&', '||'].includes(n.operator)) this.fail(n, 'Nullish coalescing is not supported.');
        const a = this.expr(n.left), b = this.expr(n.right);
        return `(${a} ${n.operator === '&&' ? 'and' : 'or'} ${b})`;
      }
      case 'ConditionalExpression': return `(${this.expr(n.consequent)} if (not not ${this.expr(n.test)}) else ${this.expr(n.alternate)})`;
      case 'CallExpression': case 'NewExpression': return this.call(n, integer);
      default: return this.fail(n, `${n.type} is not supported here. Use scalar values and LightVM resources.`);
    }
  }
  call(n: Node, integer: boolean): string {
    if (n.optional || n.arguments.some((a: Node) => a.type === 'SpreadElement')) this.fail(n, 'Optional calls and spread arguments are not supported.');
    let name = this.callName(n.callee), member = n.callee.type === 'MemberExpression';
    const args: Node[] = [...n.arguments];
    if (member) {
      if (n.callee.computed) this.fail(n, 'Call methods by name.');
      const obj = n.callee.object.name, prop = n.callee.property.name;
      if (obj === 'Math' && !this.binding(obj)) {
        const names: Record<string, string> = { sin: 'math.sin', cos: 'math.cos', sqrt: 'math.sqrt', log: 'math.log', exp: 'math.exp', pow: 'math.pow', random: 'random.random', min: 'min', max: 'max', trunc: 'trunc', floor: 'floor', ceil: 'ceil', abs: 'math.fabs', round: 'round' };
        name = names[prop]; if (!name) this.fail(n, `Math.${prop} is not supported. Use a supported numeric function.`);
        if (name === 'round' && args.length === 1) return `floor((${this.expr(args[0])}) + 0.5)`;
        if (['min', 'max'].includes(name) && args.length >= 2) return args.map(a => this.expr(a, integer)).reduce((a, b) => `${name}(${a}, ${b})`);
      } else if (obj === 'random' && !this.binding(obj)) {
        const names: Record<string, string> = { seed: 'seed', integer: 'randint', range: 'randrange', random: 'random' };
        if (!names[prop]) this.fail(n, 'Use random.seed, random.integer, random.range or random.random.');
        const result = `random.${names[prop]}(${args.map(a => `int(${this.expr(a)})`).join(', ')})`;
        return ['integer', 'range'].includes(prop) ? `float(${result})` : result;
      } else {
        const method = prop === 'input' ? prop : aliases[prop] ?? prop;
        if (!methods.has(method)) this.fail(n, `Unknown LightVM method “${prop}”.`);
        name = `${this.expr(n.callee.object)}.${method}`;
      }
    }
    const typed: Record<string, string> = { Int32Array: 'i', Uint32Array: 'I', Float32Array: 'f', Uint8Array: 'B', Uint16Array: 'H' };
    if (typed[name]) {
      if (args.length < 1 || args.length > 2 || args.length === 2 && args[0].type === 'ArrayExpression') this.fail(n, 'Typed arrays take a length with optional { fill }, or a literal array.');
      const options = args.length === 2 ? this.properties(args[1]) : [];
      if (options.some(p => this.key(p) !== 'fill')) this.fail(n, 'Typed array options support only fill.');
      const fill = options.length ? this.expr(options[0].value) : name === 'Float32Array' ? '0.0' : '0';
      const data = args[0].type === 'ArrayExpression' ? this.expr(args[0]) : `[${fill}] * ${this.expr(args[0], true)}`;
      return `array("${typed[name]}", ${data})`;
    }
    if (name === 'Number') name = 'float';
    if (name === 'fixed') {
      if (n.type === 'NewExpression') this.fail(n, 'Use fixed(value) for a number or new FixedArray(length) for storage.');
      if (args.length !== 1) this.fail(n, 'fixed takes one number.');
      const literal = (v: Node): string | undefined => v.type === 'Literal' && typeof v.value === 'number' && Number.isFinite(v.value) ? v.raw :
        v.type === 'UnaryExpression' && ['+', '-'].includes(v.operator) && literal(v.argument) !== undefined ? v.operator + literal(v.argument) : undefined;
      // Keep every decimal digit: Acorn's binary64 value can erase which side
      // of a Q16.16 halfway boundary the original source token occupied.
      return `fixed(${literal(args[0]) ?? this.expr(args[0])})`;
    }
    if (name === 'int' || name === 'float') {
      if (args.length !== 1) this.fail(n, `${name} takes one number.`);
      // Fold literal conversions so declaration expressions remain static.
      if (name === 'int' && args[0].type === 'UnaryExpression' && ['+', '-'].includes(args[0].operator) && args[0].argument.type === 'Literal' && Number.isInteger(args[0].argument.value)) {
        return String((args[0].operator === '-' ? -1 : 1) * args[0].argument.value);
      }
      if (args[0].type === 'Literal' && typeof args[0].value === 'number' && (name !== 'int' || Number.isInteger(args[0].value))) return this.expr(args[0], name === 'int');
      return `${name}(${this.expr(args[0])})`;
    }
    if (!member && !calls.has(name) && this.binding(n.callee.name)?.kind !== 'function') this.fail(n, `Unknown function “${n.callee.name}”.`);
    if (n.type === 'NewExpression' && !['PixelArray', 'FixedArray', 'Sprite', 'SpriteBatch', 'Transform', 'ColorTransform', 'Input', 'Ramp'].includes(name)) this.fail(n, 'new is only supported for LightVM resources and typed arrays.');
    const options = args.at(-1)?.type === 'ObjectExpression' ? this.properties(args.pop()) : [];
    const intPositions: Record<string, number[]> = { wrap_index: [0, 1], PixelArray: [0], FixedArray: [0], Sprite: [0], SpriteBatch: [1], rgb: [0, 1, 2], hash01: [0, 1], perlin2: [2, 3, 4], perlin3: [3, 4, 5, 6], simplex2: [2], simplex3: [3], pico_colorwheel: [0] };
    const integerSetting = name === 'setting' && (args[0]?.callee?.name === 'int' ||
      args[0]?.type === 'MemberExpression' && ['pixelCount', 'count'].includes(args[0].property?.name));
    const positional = args.map((a, i) => {
      const result = this.expr(a, (intPositions[name]?.includes(i) ?? false) || integerSetting);
      return ['hash01', 'wrap_index'].includes(name) || ['perlin2', 'perlin3', 'simplex2', 'simplex3'].includes(name) && intPositions[name].includes(i) ? `int(${result})` : result;
    });
    for (const p of options) {
      const key = { min: 'minimum', max: 'maximum' }[this.key(p)] ?? this.key(p);
      if (keywords.has(key) && !['init', 'render'].includes(key)) this.fail(p, `Unknown option “${key}”.`);
      const asInt = ['width', 'capacity'].includes(key) || integerSetting && ['minimum', 'maximum'].includes(key);
      let value = this.expr(p.value, asInt);
      if (['start', 'stop', 'index'].includes(key)) value = `int(${value})`;
      positional.push(`${key}=${value}`);
    }
    return `${name}(${positional.join(', ')})`;
  }
}
