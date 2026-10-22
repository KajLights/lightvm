import { blockExample } from './languages/blocks';
import type { Language } from './languages/source';
import jsRainbow from '../../examples/javascript/rainbow.js?raw';
import jsPulse from '../../examples/javascript/pulse.js?raw';
import jsWipe from '../../examples/javascript/wipe.js?raw';
import jsTrail from '../../examples/javascript/trail.js?raw';
import rainbow from '../../examples/animations/rainbow.py?raw';
import stars from '../../examples/animations/stars.py?raw';
import color from '../../examples/animations/color.py?raw';
import pattern from '../../examples/animations/pattern.py?raw';
import rainbowSprite from '../../examples/animations/rainbow_sprite.py?raw';
import sweep from '../../examples/animations/sweep.py?raw';
import eye from '../../examples/animations/eye.py?raw';
import crossfade from '../../examples/transitions/crossfade.py?raw';
import wipe from '../../examples/transitions/wipe.py?raw';

export interface Example { id: string; filename: string; label: string; source: string; description: string; language: Language }
const PYTHON_EXAMPLES = [
  { id: 'transition_crossfade', filename: 'crossfade.py', label: 'crossfade.py · transition', source: crossfade,
    description: 'A Python transition that blends the prepared A and B compositions at the supplied progress.' },
  { id: 'transition_wipe', filename: 'wipe.py', label: 'wipe.py · transition', source: wipe,
    description: 'Reveal B across A. Set reverse for the opposite end, or separator for a three-pixel peak with a configurable colour at the boundary.' },
  { id: 'rainbow', filename: 'rainbow.py', label: 'rainbow.py', source: rainbow,
    description: 'A colour cycle every 60 seconds. Use Live inputs to scrub phase, change spatial scale or adjust the timer.' },
  { id: 'eye', filename: 'eye.py', label: 'eye.py · live inputs', source: eye,
    description: 'An eye with live position, width, full RGB colour and local intensity. Follow light colour and bind other inputs to timers, music or sensors.' },
  { id: 'stars', filename: 'stars.py', label: 'stars.py', source: stars,
    description: 'Three-pixel stars appear and fade over a transparent background. Run alone or over another animation.' },
  { id: 'color', filename: 'color.py', label: 'color.py', source: color,
    description: 'A steady colour across the whole strip.' },
  { id: 'pattern', filename: 'pattern.py', label: 'pattern.py', source: pattern,
    description: 'A steady colour pattern, stretched to fit the strip by default.' },
  { id: 'rainbow_sprite', filename: 'rainbow_sprite.py', label: 'rainbow_sprite.py', source: rainbowSprite,
    description: 'A 15-pixel rainbow moves between the strip ends with smooth turns and transparent edges. Requires at least 15 pixels.' },
  { id: 'sweep', filename: 'sweep.py', label: 'sweep.py', source: sweep,
    description: 'Configurable eyes: loop, bounce, chase or inward wipe. Retain swept paint, or add timed trails with trail_seconds and samples. One eye can also run in its own composition layer.' },
];

export const EXAMPLES: Example[] = [
  ...PYTHON_EXAMPLES.map(example => ({ ...example, language: 'python' as const })),
  { id: 'js_rainbow', filename: 'rainbow.js', label: 'Rainbow · live inputs', source: jsRainbow, language: 'javascript', description: 'Start with a loop and a colour wheel. Try changing the timer duration or brightness.' },
  { id: 'js_pulse', filename: 'pulse.js', label: 'Breathe · helpers & callbacks', source: jsPulse, language: 'javascript', description: 'An arrow function and an animation object make a gentle breathing light.' },
  { id: 'js_trail', filename: 'trail.js', label: 'Comet · retained drawing', source: jsTrail, language: 'javascript', description: 'A PixelArray remembers a fading trail. Change its speed or colour.' },
  { id: 'js_wipe', filename: 'wipe.js', label: 'Wipe · transition', source: jsWipe, language: 'javascript', description: 'Reveal composition B with smooth motion. Try reversing the direction.' },
  ...(['rainbow', 'pulse', 'wipe', 'stars'] as const).map(kind => ({ id: 'blocks_' + kind, filename: kind + '.blocks.json', label: kind === 'rainbow' ? 'Rainbow · first loop' : kind === 'pulse' ? 'Breathe · motion' : kind === 'stars' ? 'Stars · create once, fade each frame' : 'Wipe · transition', source: blockExample(kind), language: 'blocks' as const, description: kind === 'wipe' ? 'Mix two compositions with the progress block. Try another transition block.' : kind === 'stars' ? 'Shared artwork, fixed geometry at creation, and a separate fading callback. Change brightness through the live input.' : 'Change a number, try another colour, or open a category to discover more blocks.' })),
];
export function defaultExample(language: Language) { return EXAMPLES.find(example => example.language === language && (language !== 'python' || example.id === 'rainbow'))!; }
export const DEFAULT_EXAMPLE = EXAMPLES.find(example => example.id === 'rainbow')!;
export function exampleForSource(source: string) {
  return EXAMPLES.find(example => example.source === source);
}
