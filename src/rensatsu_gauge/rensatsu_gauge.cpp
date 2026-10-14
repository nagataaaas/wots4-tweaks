// The Spring Harvest manager [0x9e9644] belongs to the player alone. Its per-frame update
// 0x43cc80 (edi = the manager) drains the gauge during a Spring Harvest and ends it at 0; the
// hook fills the gauge right after, so enemies' Spring Harvests are untouched and the update's
// own check never sees an empty gauge.
#include "rensatsu_gauge.h"
#include "../game_code.h"

namespace tweaks {

namespace {

constexpr uintptr_t rensatsuUpdate = 0x43cc80;
constexpr uint32_t rensatsuUpdateClaim = 9;  // push ebp; mov ebp,esp; mov ecx,[0x9e9658]
constexpr uintptr_t characterManagerGlobal = 0x9e9658;
// Offsets the update itself uses: the manager's player character (set at each update), the
// character's parameters, and in them the gauge and its maximum (f32; 1000 at first).
constexpr uint32_t managerPlayerCharacter = 0x54;
constexpr uint32_t characterParameters = 0xc;
constexpr uint32_t parameterGauge = 0x364;
constexpr uint32_t parameterGaugeMaximum = 0x368;

uint32_t enabled = toggleOff;
GameImage game;
TweakLog logger("Full Spring Harvest Gauge");
const uintptr_t* characterManager;  // Relocated at install
void* originalRensatsuUpdate;

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    // push ebp; mov ebp,esp; mov ecx,[0x9e9658]; sub esp,0x14; push esi; ...; call 0x465590;
    // mov edx,[edi+8]; mov ecx,[edi+4]; mov [edi+0x54],eax
    const uint8_t prologue[] = {0x55, 0x8b, 0xec, 0x8b, 0x0d};
    const uint8_t setsPlayer[] = {0x83, 0xec, 0x14, 0x56, 0x85, 0xc9, 0x74, 0x67, 0x33, 0xf6};
    const uint8_t storesPlayer[] = {0x8b, 0x57, 0x08, 0x8b, 0x4f, 0x04, 0x89, 0x47, 0x54};
    // mov ecx,[edi+0x54]; test ecx,ecx; je; cmp byte [edi+0x22],1; mov esi,[ecx+0xc]; jne;
    // fldz; fcomp dword [esi+0x364]
    const uint8_t endsAtEmpty[] = {0x8b, 0x4f, 0x54, 0x85, 0xc9, 0x74, 0x3c, 0x80, 0x7f, 0x22, 0x01, 0x8b, 0x71,
        0x0c, 0x75, 0x23, 0xd9, 0xee, 0xd8, 0x9e, 0x64, 0x03, 0x00, 0x00};
    return game.codeIs(rensatsuUpdate, prologue, sizeof(prologue)) &&
           game.operandIs(rensatsuUpdate + 5, characterManagerGlobal) &&
           game.codeIs(0x43cc89, setsPlayer, sizeof(setsPlayer)) && game.callsTo(0x43cc93, 0x465590) &&
           game.codeIs(0x43cc98, storesPlayer, sizeof(storesPlayer)) &&
           game.codeIs(0x43ccb5, endsAtEmpty, sizeof(endsAtEmpty));
}

// Called each frame while the player is in the field: two loads and a store when on.
void __cdecl fillGauge(const uint8_t* manager) {
    if (enabled != toggleOn || *characterManager == 0) return;
    const uint8_t* character = *reinterpret_cast<uint8_t* const*>(manager + managerPlayerCharacter);
    if (!character) return;
    uint8_t* parameters = *reinterpret_cast<uint8_t* const*>(character + characterParameters);
    if (!parameters) return;
    *reinterpret_cast<float*>(parameters + parameterGauge) =
        *reinterpret_cast<const float*>(parameters + parameterGaugeMaximum);
}

// The update takes the manager in edi and nothing on the stack; it keeps edi only by convention,
// so the hook keeps its own copy.
__declspec(naked) void rensatsuUpdateHook() {
    __asm {
        push edi
        call dword ptr [originalRensatsuUpdate]
        pop edi
        pushad
        push edi
        call fillGauge
        add esp, 4
        popad
        ret
    }
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the Spring Harvest update is not the known version", rensatsuUpdate);
        return false;
    }
    characterManager = reinterpret_cast<const uintptr_t*>(game.address(characterManagerGlobal));
    if (CodeChangeError error = hookGameCode(context, game, rensatsuUpdate, rensatsuUpdateClaim,
            reinterpret_cast<void*>(&rensatsuUpdateHook), &originalRensatsuUpdate)) {
        logger.installFailed(error, rensatsuUpdate);
        return false;
    }
    return true;
}

// つま斬り is "Spring Harvest" in the game's English texts.
const Option options[] = {
    toggle(L"enabled", {L"つま斬りゲージMAX", L"Full Spring Harvest Gauge"}, &enabled, false),
};

}  // namespace

const Tweak rensatsuGaugeTweak{L"rensatsu_gauge", options, 1, install};

}  // namespace tweaks
