#pragma once

#include "Enemy.h"
#include <string>
#include <vector>
#include <unordered_map>

// True-color "pixel art" portraits per enemy archetype and boss, plus a few
// triggered animation beats (hit flash, attack lunge, death crumple).
//
// Rendered with 24-bit color half-blocks (U+2580 UPPER HALF BLOCK): each
// terminal cell's foreground paints the "top" pixel and its background paints
// the "bottom" pixel, packing two pixel-rows into one printed row - the same
// technique terminal image viewers (chafa, viu, etc.) use, not hand-drawn
// ASCII glyphs.
namespace EnemyArt {
    struct RGB { unsigned char r, g, b; };

    struct Art {
        std::vector<std::string> grid; // rows of palette-key characters, '.' = transparent
        std::unordered_map<char, RGB> palette; // overrides/additions on top of the shared palette
    };

    // Boss != NONE takes priority over the base EnemyType.
    const Art& get(EnemyType type, BossType boss = BossType::NONE);

    // Static portrait, full brightness.
    void print(const Art& art, int indent = 2);

    // Brief lunge toward the player (shifted left, no indent).
    void printLunge(const Art& art);

    // Brief bright flash (used when the enemy takes damage) - blown-out highlights.
    void printHitFlash(const Art& art);

    // Crumple/fade sequence (used when the enemy is defeated) - darkened palette.
    void printDeath(const Art& art);
}
