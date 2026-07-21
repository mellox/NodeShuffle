// BUILD-TIME HEADER STUB ONLY — see README-STUB.md at the plugin root.
// Minimal module implementation so the stub DLL links. Never runs in a shipped game: this plugin is
// never packaged/deployed; at runtime the player's real KBFL provides the KBFL module.

#include "Modules/ModuleManager.h"

// IMPLEMENT_GAME_MODULE + a game-module impl, mirroring the real KBFL's module macro
// (KBFLModule.cpp: IMPLEMENT_GAME_MODULE(FKBFLModule, KBFL)).
IMPLEMENT_GAME_MODULE(FDefaultGameModuleImpl, KBFL);
