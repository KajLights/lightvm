import rainbow from '../../examples/animations/rainbow.py?raw';
import stars from '../../examples/animations/stars.py?raw';
import color from '../../examples/animations/color.py?raw';
import pattern from '../../examples/animations/pattern.py?raw';
import rainbowSprite from '../../examples/animations/rainbow_sprite.py?raw';
import sweep from '../../examples/animations/sweep.py?raw';

export const EXAMPLES = [
  { id: 'rainbow', filename: 'rainbow.py', label: 'rainbow.py', source: rainbow,
    description: 'Original picolights Rainbow: one colour cycle every 60 seconds. Override duration in the preview settings for faster motion.' },
  { id: 'stars', filename: 'stars.py', label: 'stars.py', source: stars,
    description: 'Three-pixel stars appear and fade over a transparent background. Run alone or over another animation.' },
  { id: 'color', filename: 'color.py', label: 'color.py', source: color,
    description: 'A steady colour across the whole strip.' },
  { id: 'pattern', filename: 'pattern.py', label: 'pattern.py', source: pattern,
    description: 'A steady colour pattern, stretched to fit the strip by default.' },
  { id: 'rainbow_sprite', filename: 'rainbow_sprite.py', label: 'rainbow_sprite.py', source: rainbowSprite,
    description: 'A 15-pixel rainbow moves between the strip ends with smooth turns and transparent edges. Requires at least 15 pixels.' },
  { id: 'sweep', filename: 'sweep.py', label: 'sweep.py', source: sweep,
    description: 'Configurable eyes: loop, bounce, chase or inward wipe. Retain swept paint, or add timed trails with trail_seconds and samples. One eye can also run in its own scene layer.' },
];

export const DEFAULT_EXAMPLE = EXAMPLES[0];
export function exampleForSource(source: string) {
  return EXAMPLES.find(example => example.source === source);
}
