#pragma once
#include <string>

namespace Color {
    // Reset / style
    constexpr const char* RESET   = "\033[0m";
    constexpr const char* BOLD    = "\033[1m";
    constexpr const char* DIM     = "\033[2m";

    // Bright colors (most readable on dark terminals)
    constexpr const char* RED     = "\033[91m";
    constexpr const char* GREEN   = "\033[92m";
    constexpr const char* YELLOW  = "\033[93m";
    constexpr const char* BLUE    = "\033[94m";
    constexpr const char* MAGENTA = "\033[95m";
    constexpr const char* CYAN    = "\033[96m";
    constexpr const char* WHITE   = "\033[97m";

    // Standard (slightly dimmer, good for backgrounds/labels)
    constexpr const char* DRED    = "\033[31m";
    constexpr const char* DGREEN  = "\033[32m";
    constexpr const char* DYELLOW = "\033[33m";
    constexpr const char* DBLUE   = "\033[34m";
    constexpr const char* DMAGENTA= "\033[35m";
    constexpr const char* DCYAN   = "\033[36m";

    // 256-color extras - the basic 16 colors are all already spoken for above
    constexpr const char* ORANGE  = "\033[38;5;208m";

    // Semantic aliases - use these in game code for consistency
    constexpr const char* DAMAGE        = RED;      // player takes damage
    constexpr const char* PLAYER_ATTACK = GREEN;    // player deals damage
    constexpr const char* HEAL          = GREEN;
    constexpr const char* ARMOR_CLR     = BLUE;
    constexpr const char* ENERGY_CLR    = YELLOW;
    constexpr const char* POISON_CLR    = DGREEN;
    constexpr const char* BURN_CLR      = ORANGE;
    constexpr const char* STUN_CLR      = YELLOW;
    constexpr const char* WEAK_CLR      = BLUE;
    constexpr const char* STRENGTH_CLR  = DGREEN;
    constexpr const char* BOSS_CLR      = MAGENTA;
    constexpr const char* CARD_ATTACK   = RED;
    constexpr const char* CARD_DEFEND   = BLUE;
    constexpr const char* CARD_SPECIAL  = MAGENTA;
    constexpr const char* CARD_NAME     = WHITE;
    constexpr const char* SECTION       = CYAN;

    // Rarity tints for card names - 256-color pastels, distinct from the
    // basic 16 colors which are all spoken for elsewhere.
    constexpr const char* COMMON_TINT     = "\033[38;5;120m"; // pale green
    constexpr const char* RARE_TINT       = "\033[38;5;153m"; // pale sky blue
    constexpr const char* SUPER_RARE_TINT = "\033[38;5;218m"; // pale pink

    // Legendary is a step up from the pastel family on purpose - bold, saturated
    // neon gold, so Dodge (the only legendary card) visually stands out rather
    // than just being another pastel.
    constexpr const char* LEGENDARY_TINT = "\033[1m\033[38;5;220m"; // bold neon gold

    // Menu selection cursor - was bold cyan, which blends into a dark terminal.
    // Bold neon lime instead, so the highlighted row actually pops.
    constexpr const char* SELECT_CLR = "\033[1m\033[38;5;118m";
}

// Returns an ANSI color for an HP value relative to its maximum
inline const char* hpColor(int hp, int maxHp) {
    if (maxHp <= 0) return Color::WHITE;
    int pct = hp * 100 / maxHp;
    if (pct > 60) return Color::GREEN;
    if (pct > 30) return Color::YELLOW;
    return Color::RED;
}
