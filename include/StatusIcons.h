#ifndef STATUS_ICONS_H
#define STATUS_ICONS_H

#include <SDL.h>
#include <string>

// The symbols in the combat panel's status tags. Each kind of tag has an 8x8
// two-tone pattern, drawn in the tag's own colour, so a tag still reads for
// someone who cannot tell its colour from another's: Poison's green and
// Burn's orange can look the same, a droplet and a flame do not.
namespace StatusIcons {

// The symbol for a tag's words ("Poison 3/turn x2"), or -1 if it has none.
int forTag(const std::string& words);

// Draws symbol `id` with its top left at x, y, each of its pixels z by z
// screen pixels, in `colour` with its lighter parts a step towards white.
void draw(SDL_Renderer* r, int id, int x, int y, int z, SDL_Color colour);

} // namespace StatusIcons

#endif
