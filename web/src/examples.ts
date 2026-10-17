import rainbow from '../../examples/animations/rainbow.py?raw';
import stars from '../../examples/animations/stars.py?raw';
import color from '../../examples/animations/color.py?raw';
import pattern from '../../examples/animations/pattern.py?raw';
import wipe from '../../examples/animations/wipe.py?raw';
import rainbowSprite from '../../examples/animations/rainbow_sprite.py?raw';

export const EXAMPLES = [
  { id: 'rainbow', filename: 'rainbow.py', label: 'rainbow.py', source: rainbow,
    description: 'Original picolights Rainbow: one colour cycle every 60 seconds. Lower duration in Initialization settings for faster motion.' },
  { id: 'stars', filename: 'stars.py', label: 'stars.py', source: stars,
    description: 'Three-pixel stars appear and fade over a transparent background. Run alone or over another animation.' },
  { id: 'color', filename: 'color.py', label: 'color.py', source: color,
    description: 'A steady colour across the whole strip.' },
  { id: 'pattern', filename: 'pattern.py', label: 'pattern.py', source: pattern,
    description: 'A steady colour pattern, stretched to fit the strip by default.' },
  { id: 'wipe', filename: 'wipe.py', label: 'wipe.py', source: wipe,
    description: 'Fills from both ends towards the centre, then clears and repeats. At the default speed, 200 pixels fill in about 2 seconds.' },
  { id: 'rainbow_sprite', filename: 'rainbow_sprite.py', label: 'rainbow_sprite.py', source: rainbowSprite,
    description: 'A 15-pixel rainbow moves between the strip ends with smooth turns and transparent edges. Requires at least 15 pixels.' },
];

export const DEFAULT_EXAMPLE = EXAMPLES[0];
export function exampleForSource(source: string) {
  return EXAMPLES.find(example => example.source === source);
}
