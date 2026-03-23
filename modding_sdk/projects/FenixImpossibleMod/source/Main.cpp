#include <plugin.h>
#include <CFont.h>
#include <CMenuManager.h>
#include <CAEAudioHardware.h>
#include <CPed.h>
#include <CPools.h>
#include <CRect.h>
#include <CSprite.h>
#include <CSprite2d.h>
#include <CTimer.h>
#include <CVehicle.h>
#include <RenderWare.h>
#include <common.h>
#include <ePedBones.h>
#include <windows.h>
#include <winsqlite/winsqlite3.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

using namespace plugin;

#pragma comment(lib, "winsqlite3.lib")

namespace {

constexpr float kMaxBubbleDistance = 25.0f;
constexpr unsigned int kBubbleLifetimeMs = 220;
constexpr int kMaxLinesPerBubble = 2;
constexpr char kTargetLogPath[] = "voice_workspace\\logs\\ballas_police_observed.tsv";
constexpr char kObservedRootPath[] = "voice_workspace\\observed";
constexpr char kCatalogDbRelativePath[] = "voice_workspace\\catalog.db";
constexpr char kDbMissingSeedText[] = "[db:sin_seed]";
constexpr char kDbMissingCatalogText[] = "[db:sin_catalogo]";

struct PedSpeechState {
    bool wasTalking = false;
    short lastPhraseId = -1;
    unsigned int lastSeenAt = 0;
};

struct BubbleState {
    std::string text;
    short phraseId = -1;
    unsigned int expiresAt = 0;
};

struct BubbleLayout {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float textX = 0.0f;
    float textY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    std::vector<std::string> lines;
};

struct BubbleRenderCandidate {
    int pedRef = -1;
    float distanceSq = 0.0f;
    float screenX = 0.0f;
    float screenY = 0.0f;
    std::string text;
};

struct RuntimeVoiceCatalog {
    bool attemptedLoad = false;
    bool loaded = false;
    std::unordered_map<int, std::string> modelNames;
    std::unordered_map<int, std::string> voiceLabels;
    std::unordered_map<int, std::vector<std::string>> modelSeedTexts;
    std::unordered_map<std::string, std::vector<std::string>> groupSeedTexts;
};

RuntimeVoiceCatalog g_runtimeCatalog;

std::string GetGameRootPath() {
    char exePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }

    std::filesystem::path path(exePath);
    return path.parent_path().string();
}

std::string GetCatalogDbAbsolutePath() {
    const std::string gameRoot = GetGameRootPath();
    if (gameRoot.empty()) {
        return {};
    }

    std::filesystem::path dbPath = std::filesystem::path(gameRoot) / kCatalogDbRelativePath;
    return dbPath.string();
}

bool IsBallasModel(int modelId) {
    return modelId >= 102 && modelId <= 104;
}

bool IsPoliceModel(int modelId) {
    switch (modelId) {
    case 280:
    case 281:
    case 282:
    case 283:
    case 284:
    case 288:
        return true;
    default:
        return false;
    }
}

const char *GetVoiceTypeGroupName(short voiceType) {
    switch (voiceType) {
    case PED_TYPE_EMG:
        return "emergency";
    case PED_TYPE_GANG:
        return "gang";
    case PED_TYPE_GFD:
        return "gfd";
    case PED_TYPE_SPC:
        return "special";
    default:
        return "ambient";
    }
}

const char *GetTargetGroupName(int modelId, short voiceType = PED_TYPE_GEN) {
    if (IsBallasModel(modelId)) {
        return "ballas";
    }
    if (IsPoliceModel(modelId)) {
        return "police";
    }
    return GetVoiceTypeGroupName(voiceType);
}

void LoadRuntimeVoiceCatalog() {
    if (g_runtimeCatalog.attemptedLoad) {
        return;
    }

    g_runtimeCatalog.attemptedLoad = true;
    const std::string dbPath = GetCatalogDbAbsolutePath();
    if (dbPath.empty() || !std::filesystem::exists(dbPath)) {
        return;
    }

    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK || !db) {
        if (db) {
            sqlite3_close(db);
        }
        return;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *modelSql =
        "SELECT pm.model_id, pm.model_name, "
        "COALESCE((SELECT GROUP_CONCAT(voice_label, '|') FROM ped_model_voices WHERE model_id = pm.model_id), '') "
        "FROM ped_models pm";

    if (sqlite3_prepare_v2(db, modelSql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const int modelId = sqlite3_column_int(stmt, 0);
            const unsigned char *modelName = sqlite3_column_text(stmt, 1);
            const unsigned char *voiceLabel = sqlite3_column_text(stmt, 2);
            if (modelName) {
                g_runtimeCatalog.modelNames[modelId] = reinterpret_cast<const char *>(modelName);
            }
            if (voiceLabel && sqlite3_column_bytes(stmt, 2) > 0) {
                g_runtimeCatalog.voiceLabels[modelId] = reinterpret_cast<const char *>(voiceLabel);
            }
        }
    }
    sqlite3_finalize(stmt);
    stmt = nullptr;

    const char *seedSql =
        "SELECT group_name, model_id, text_es "
        "FROM runtime_seed_texts "
        "WHERE text_es IS NOT NULL AND TRIM(text_es) <> ''";

    if (sqlite3_prepare_v2(db, seedSql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *groupName = sqlite3_column_text(stmt, 0);
            const bool hasModelId = sqlite3_column_type(stmt, 1) != SQLITE_NULL;
            const int modelId = hasModelId ? sqlite3_column_int(stmt, 1) : -1;
            const unsigned char *text = sqlite3_column_text(stmt, 2);
            if (!groupName || !text) {
                continue;
            }

            const std::string group = reinterpret_cast<const char *>(groupName);
            const std::string value = reinterpret_cast<const char *>(text);
            if (hasModelId) {
                g_runtimeCatalog.modelSeedTexts[modelId].push_back(value);
            } else {
                g_runtimeCatalog.groupSeedTexts[group].push_back(value);
            }
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    g_runtimeCatalog.loaded = true;
}

const CBankSlotBankAssignment *FindBankAssignment(short bankSlotId) {
    if (!AEAudioHardware.m_pMP3BankLoader || bankSlotId < 0) {
        return nullptr;
    }

    for (const CBankSlotBankAssignment &assignment : AEAudioHardware.m_pMP3BankLoader->m_aBankSlotBankAssignment) {
        if (assignment.m_nBankSlotId == bankSlotId && assignment.m_nBankId >= 0) {
            return &assignment;
        }
    }

    return nullptr;
}

const CBankSlotInfo *FindBankSlotInfo(short bankSlotId) {
    if (!AEAudioHardware.m_pMP3BankLoader || bankSlotId < 0) {
        return nullptr;
    }

    CAEMP3BankLoader *loader = AEAudioHardware.m_pMP3BankLoader;
    if (!loader->m_pBankSlotsInfos || bankSlotId >= loader->m_nNumBankSlotsInfos) {
        return nullptr;
    }

    const CBankSlotInfo *slotInfo = &loader->m_pBankSlotsInfos[bankSlotId];
    if (slotInfo->m_nBankId < 0) {
        return nullptr;
    }

    return slotInfo;
}

float DistanceSquared(const CVector &a, const CVector &b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

bool IsPedHandleValid(int pedRef) {
    return CPools::ms_pPedPool && CPools::ms_pPedPool->GetAtRef(pedRef) != nullptr;
}

bool ShouldTrackPed(CPed *ped, CPlayerPed *player) {
    if (!ped || ped == player) {
        return false;
    }

    if (ped->m_nPedType == PED_TYPE_PLAYER1) {
        return false;
    }

    return DistanceSquared(ped->GetPosition(), player->GetPosition()) <= (kMaxBubbleDistance * kMaxBubbleDistance);
}

float ScaleX(float value) {
    return value * static_cast<float>(RsGlobal.maximumWidth) / 640.0f;
}

float ScaleY(float value) {
    return value * static_cast<float>(RsGlobal.maximumHeight) / 448.0f;
}

void SetupBubbleFont(float centreSize) {
    CFont::SetOrientation(ALIGN_CENTER);
    CFont::SetJustify(false);
    CFont::SetBackground(false, false);
    CFont::SetBackgroundColor(CRGBA(0, 0, 0, 0));
    CFont::SetWrapx(RsGlobal.maximumWidth - ScaleX(8.0f));
    CFont::SetCentreSize(centreSize);
    CFont::SetRightJustifyWrap(0.0f);
    CFont::SetScale(ScaleX(0.22f), ScaleY(0.55f));
    CFont::SetFontStyle(FONT_SUBTITLES);
    CFont::SetProportional(true);
    CFont::SetEdge(0);
    CFont::SetAlphaFade(255.0f);
    CFont::SetDropShadowPosition(1);
    CFont::SetDropColor(CRGBA(0, 0, 0, 220));
}

float EstimateGlyphWidth(char c) {
    if (c == ' ' || c == '\t') {
        return ScaleX(2.7f);
    }

    switch (c) {
    case 'i':
    case 'l':
    case '!':
    case '.':
    case ',':
    case ':':
    case ';':
    case '\'':
        return ScaleX(2.2f);
    case 'm':
    case 'w':
    case 'M':
    case 'W':
        return ScaleX(5.8f);
    default:
        return ScaleX(4.2f);
    }
}

float EstimateTextWidth(const std::string &text) {
    float width = 0.0f;
    for (char c : text) {
        width += EstimateGlyphWidth(c);
    }
    return width;
}

std::string TrimBubbleText(const std::string &text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }

    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string SanitizeBubbleText(const std::string &text) {
    const std::string trimmed = TrimBubbleText(text);
    if (trimmed.empty()) {
        return "...";
    }
    return trimmed;
}

std::string SanitizeBubbleText(const char *text) {
    if (!text) {
        return "...";
    }
    return SanitizeBubbleText(std::string(text));
}

std::string FitLineToWidth(const std::string &text, float maxWidth, bool ellipsis) {
    std::string out = SanitizeBubbleText(text);
    const std::string suffix = ellipsis ? "..." : "";

    while (!out.empty() && EstimateTextWidth(out + suffix) > maxWidth) {
        out.pop_back();
    }

    if (out.empty()) {
        return suffix.empty() ? "..." : suffix;
    }

    return out + suffix;
}

std::vector<std::string> WrapBubbleText(const std::string &text, float maxWidth) {
    std::vector<std::string> lines;
    std::istringstream stream(SanitizeBubbleText(text));
    std::string word;
    std::string current;

    while (stream >> word) {
        const std::string candidate = current.empty() ? word : current + " " + word;

        if (!current.empty() && EstimateTextWidth(candidate) > maxWidth) {
            lines.push_back(current);
            current = word;

            if (static_cast<int>(lines.size()) >= kMaxLinesPerBubble - 1) {
                std::string rest = current;
                while (stream >> word) {
                    rest += " " + word;
                }
                lines.push_back(FitLineToWidth(rest, maxWidth, true));
                return lines;
            }
        } else {
            current = candidate;
        }
    }

    if (!current.empty()) {
        lines.push_back(FitLineToWidth(current, maxWidth, false));
    }

    if (lines.empty()) {
        lines.push_back("...");
    }

    return lines;
}

BubbleLayout MeasureBubbleLayout(const std::string &text, float centerX, float anchorY, float verticalOffset = 0.0f) {
    const float bubbleWidth = ScaleX(84.0f);
    const float bubbleHeight = ScaleY(24.0f);
    const float paddingX = ScaleX(7.0f);
    const float lineHeight = ScaleY(9.0f);
    const float bubbleGapY = ScaleY(20.0f);

    BubbleLayout layout;
    layout.width = bubbleWidth;
    layout.height = bubbleHeight;
    layout.left = centerX - bubbleWidth * 0.5f;
    layout.right = centerX + bubbleWidth * 0.5f;
    layout.bottom = anchorY + ScaleY(verticalOffset) - bubbleGapY;
    layout.top = layout.bottom - bubbleHeight;
    layout.textX = centerX;
    layout.lines = WrapBubbleText(text, bubbleWidth - paddingX * 2.0f);

    const float totalTextHeight = static_cast<float>(layout.lines.size()) * lineHeight;
    layout.textY = layout.top + (bubbleHeight - totalTextHeight) * 0.5f - ScaleY(0.5f);
    return layout;
}

void ClampLayoutToScreen(BubbleLayout &layout) {
    const float minLeft = ScaleX(4.0f);
    const float maxRight = RsGlobal.maximumWidth - ScaleX(4.0f);

    if (layout.left < minLeft) {
        const float delta = minLeft - layout.left;
        layout.left += delta;
        layout.right += delta;
        layout.textX += delta;
    } else if (layout.right > maxRight) {
        const float delta = layout.right - maxRight;
        layout.left -= delta;
        layout.right -= delta;
        layout.textX -= delta;
    }

    if (layout.top < ScaleY(4.0f)) {
        const float delta = ScaleY(4.0f) - layout.top;
        layout.top += delta;
        layout.bottom += delta;
        layout.textY += delta;
    }
}

void DrawThoughtTail(float centerX, float bubbleBottom, float anchorY) {
    const CRGBA outline(0, 0, 0, 170);
    const CRGBA fill(255, 255, 255, 220);
    const float halfBase = ScaleX(4.5f);
    const float tipX = centerX - ScaleX(1.5f);
    const float baseY = bubbleBottom;
    const float tipY = anchorY - ScaleY(4.0f);

    CSprite2d::Draw2DPolygon(centerX - halfBase, baseY, centerX + halfBase, baseY, tipX, tipY, tipX, tipY, outline);
    CSprite2d::Draw2DPolygon(centerX - halfBase + ScaleX(1.0f), baseY - ScaleY(0.7f), centerX + halfBase - ScaleX(1.0f), baseY - ScaleY(0.7f),
        tipX, tipY + ScaleY(1.8f), tipX, tipY + ScaleY(1.8f), fill);
}

void DrawCloudBubble(const BubbleLayout &layout, float anchorY) {
    const CRGBA outline(0, 0, 0, 170);
    const CRGBA fill(255, 255, 255, 220);
    const float border = ScaleX(1.0f);
    CSprite2d::DrawRect(CRect(layout.left - border, layout.top - border, layout.right + border, layout.bottom + border), outline);
    CSprite2d::DrawRect(CRect(layout.left, layout.top, layout.right, layout.bottom), fill);
    DrawThoughtTail(layout.left + layout.width * 0.50f, layout.bottom, anchorY);
}

bool GetBubbleAnchor(CPed *ped, RwV3d &anchor) {
    if (!ped) {
        return false;
    }

    if (ped->m_pRwObject && ped->m_pRwClump) {
        ped->GetBonePosition(anchor, BONE_HEAD, true);
        anchor.z += ped->m_pVehicle ? 0.12f : 0.18f;
        return true;
    }

    if (ped->m_pVehicle) {
        CVehicle *vehicle = ped->m_pVehicle;
        const CVector vehiclePos = vehicle->GetPosition();
        anchor = { vehiclePos.x, vehiclePos.y, vehiclePos.z + std::max(1.4f, vehicle->GetBoundRadius() + 0.35f) };
        return true;
    }

    CVector pos = ped->GetPosition();
    anchor = { pos.x, pos.y, pos.z + 1.0f };
    return true;
}

std::string GetCatalogModelName(int modelId) {
    LoadRuntimeVoiceCatalog();
    const auto it = g_runtimeCatalog.modelNames.find(modelId);
    if (it != g_runtimeCatalog.modelNames.end()) {
        return it->second;
    }
    std::ostringstream out;
    out << "[db:model_missing:" << modelId << "]";
    return out.str();
}

std::string GetCatalogVoiceLabel(CPed *ped) {
    LoadRuntimeVoiceCatalog();
    const auto it = g_runtimeCatalog.voiceLabels.find(ped->m_nModelIndex);
    if (it != g_runtimeCatalog.voiceLabels.end() && !it->second.empty()) {
        return it->second;
    }
    std::ostringstream out;
    out << "[db:voice_missing:model_" << ped->m_nModelIndex << "]";
    return out.str();
}

std::string PickRuntimeSeedText(CPed *ped, short phraseId, int pedRef, unsigned int now) {
    const unsigned int phraseSeed = phraseId >= 0
        ? static_cast<unsigned int>(phraseId)
        : static_cast<unsigned int>((pedRef * 13) + static_cast<int>(now / 300u));

    const int modelId = ped->m_nModelIndex;
    const std::string groupName = GetTargetGroupName(modelId, ped->m_pedSpeech.m_nVoiceType);

    LoadRuntimeVoiceCatalog();
    if (!g_runtimeCatalog.loaded) {
        return kDbMissingCatalogText;
    }

    const auto modelIt = g_runtimeCatalog.modelSeedTexts.find(modelId);
    if (modelIt != g_runtimeCatalog.modelSeedTexts.end() && !modelIt->second.empty()) {
        return SanitizeBubbleText(modelIt->second[phraseSeed % static_cast<unsigned int>(modelIt->second.size())]);
    }

    const auto groupIt = g_runtimeCatalog.groupSeedTexts.find(groupName);
    if (groupIt != g_runtimeCatalog.groupSeedTexts.end() && !groupIt->second.empty()) {
        return SanitizeBubbleText(groupIt->second[phraseSeed % static_cast<unsigned int>(groupIt->second.size())]);
    }

    return kDbMissingSeedText;
}

} // namespace

struct Main {
    std::unordered_map<int, PedSpeechState> m_pedSpeechState;
    std::unordered_map<int, BubbleState> m_activeBubbles;
    std::unordered_set<std::string> m_loggedTargetKeys;

    Main() {
        LoadRuntimeVoiceCatalog();
        Events::gameProcessEvent += [this] { OnGameProcess(); };
        Events::drawingEvent += [this] { OnDrawing(); };
    }

    void LogTargetPhrase(CPed *ped, short phraseId) {
        if (!ped || phraseId < 0) {
            return;
        }

        const int modelId = ped->m_nModelIndex;
        const std::string groupName = GetTargetGroupName(modelId, ped->m_pedSpeech.m_nVoiceType);
        const std::string modelName = GetCatalogModelName(modelId);
        const std::string voiceLabel = GetCatalogVoiceLabel(ped);

        std::ostringstream keyStream;
        keyStream << groupName << '|' << modelId << '|' << phraseId;
        const std::string key = keyStream.str();
        if (!m_loggedTargetKeys.insert(key).second) {
            return;
        }

        try {
            std::filesystem::create_directories("voice_workspace\\logs");
            std::filesystem::create_directories(kObservedRootPath);
            std::ofstream file(kTargetLogPath, std::ios::app);
            if (!file.is_open()) {
                return;
            }

            const CAESound *sound = ped->m_pedSpeech.m_pSound;
            const short bankSlotId = sound ? sound->m_nBankSlotId : -1;
            const short soundIdInSlot = sound ? sound->m_nSoundIdInSlot : -1;
            const short soundLength = sound ? sound->m_nSoundLength : -1;
            const short playPosition = sound ? sound->m_nCurrentPlayPosition : -1;
            const float finalVolume = sound ? sound->m_fFinalVolume : 0.0f;
            const float frequency = sound ? sound->m_fFrequency : 0.0f;
            const CBankSlotBankAssignment *assignment = FindBankAssignment(bankSlotId);
            const CBankSlotInfo *slotInfo = FindBankSlotInfo(bankSlotId);
            const short bankId = assignment
                ? assignment->m_nBankId
                : (slotInfo ? slotInfo->m_nBankId : -1);
            const short numSounds = assignment
                ? assignment->m_nNumSounds
                : (slotInfo ? slotInfo->m_nNumSoundInfo : -1);
            const int bankOffset = assignment ? assignment->m_nBankOffset : -1;
            const int bankLength = assignment ? assignment->m_nBankLength : -1;
            int pakFileNumber = assignment ? static_cast<int>(assignment->m_nPakFileNumber) : -1;
            if (pakFileNumber < 0 && AEAudioHardware.m_pMP3BankLoader && bankId >= 0) {
                CBankLkup *bankLkups = AEAudioHardware.m_pMP3BankLoader->m_pBankLkups;
                const short numBankLkup = AEAudioHardware.m_pMP3BankLoader->m_nNumBankLkup;
                if (bankLkups && bankId < numBankLkup) {
                    pakFileNumber = static_cast<int>(bankLkups[bankId].m_nPakFileNumber);
                }
            }

            const std::filesystem::path groupRoot = std::filesystem::path(kObservedRootPath) / groupName / modelName;
            std::filesystem::create_directories(groupRoot);

            std::ostringstream fileName;
            fileName << modelName
                     << "__phrase_" << phraseId
                     << "__bankid_" << bankId
                     << "__bank_" << bankSlotId
                     << "__sfx_" << soundIdInSlot
                     << ".txt";

            std::ofstream sample(groupRoot / fileName.str(), std::ios::trunc);
            if (sample.is_open()) {
                sample
                    << "group=" << groupName << '\n'
                    << "model_id=" << modelId << '\n'
                    << "model_name=" << modelName << '\n'
                    << "voice_label=" << voiceLabel << '\n'
                    << "phrase_id=" << phraseId << '\n'
                    << "voice_type=" << static_cast<int>(ped->m_pedSpeech.m_nVoiceType) << '\n'
                    << "voice_id=" << static_cast<int>(ped->m_pedSpeech.m_nVoiceID) << '\n'
                    << "voice_gender=" << static_cast<int>(ped->m_pedSpeech.m_nVoiceGender) << '\n'
                    << "bank_id=" << bankId << '\n'
                    << "bank_slot_id=" << bankSlotId << '\n'
                    << "num_sounds_in_bank=" << numSounds << '\n'
                    << "bank_offset=" << bankOffset << '\n'
                    << "bank_length=" << bankLength << '\n'
                    << "pak_file_number=" << pakFileNumber << '\n'
                    << "sound_id_in_slot=" << soundIdInSlot << '\n'
                    << "sound_length=" << soundLength << '\n'
                    << "play_position=" << playPosition << '\n'
                    << "final_volume=" << finalVolume << '\n'
                    << "frequency=" << frequency << '\n';
            }

            file << groupName << '\t'
                 << modelId << '\t'
                 << modelName << '\t'
                 << voiceLabel << '\t'
                 << phraseId << '\t'
                 << bankId << '\t'
                 << bankSlotId << '\t'
                 << numSounds << '\t'
                 << bankOffset << '\t'
                 << bankLength << '\t'
                 << pakFileNumber << '\t'
                 << soundIdInSlot << '\t'
                 << soundLength << '\t'
                 << playPosition << '\t'
                 << static_cast<int>(ped->m_pedSpeech.m_nVoiceType) << '\t'
                 << static_cast<int>(ped->m_pedSpeech.m_nVoiceID) << '\t'
                 << static_cast<int>(ped->m_pedSpeech.m_nVoiceGender) << '\t'
                 << finalVolume << '\t'
                 << frequency << '\n';
        } catch (...) {
        }
    }

    void OnGameProcess() {
        if (!CPools::ms_pPedPool || FrontEndMenuManager.m_bMenuActive || FrontEndMenuManager.m_bDrawRadarOrMap) {
            return;
        }

        CPlayerPed *player = FindPlayerPed();
        if (!player) {
            return;
        }

        const unsigned int now = CTimer::m_snTimeInMilliseconds;
        const int poolSize = CPools::ms_pPedPool->m_nSize;

        for (int i = 0; i < poolSize; ++i) {
            CPed *ped = CPools::ms_pPedPool->GetAt(i);
            if (!ShouldTrackPed(ped, player)) {
                continue;
            }

            const int pedRef = CPools::GetPedRef(ped);
            auto &state = m_pedSpeechState[pedRef];
            state.lastSeenAt = now;

            const short phraseId = ped->m_pedSpeech.m_nCurrentPhraseId;
            const bool hasPhrase = phraseId >= 0;
            const bool rawTalking = ped->GetPedTalking() || ped->bIsTalking;
            const bool startedSpeaking = rawTalking && !state.wasTalking;
            const short effectivePhraseId = hasPhrase ? phraseId : state.lastPhraseId;
            const bool changedPhrase = rawTalking && effectivePhraseId >= 0 && effectivePhraseId != state.lastPhraseId;

            if (rawTalking) {
                auto &bubble = m_activeBubbles[pedRef];
                if (startedSpeaking || bubble.text.empty() || bubble.phraseId != effectivePhraseId) {
                    bubble.text = PickRuntimeSeedText(ped, effectivePhraseId, pedRef, now);
                    bubble.phraseId = effectivePhraseId;
                    LogTargetPhrase(ped, effectivePhraseId);
                }
                bubble.expiresAt = now + kBubbleLifetimeMs;
            }

            state.wasTalking = rawTalking;
            if (hasPhrase || changedPhrase) {
                state.lastPhraseId = effectivePhraseId;
            }
        }

        for (auto it = m_pedSpeechState.begin(); it != m_pedSpeechState.end();) {
            if (!IsPedHandleValid(it->first) || now - it->second.lastSeenAt > 5000) {
                m_activeBubbles.erase(it->first);
                it = m_pedSpeechState.erase(it);
            } else {
                ++it;
            }
        }

        for (auto it = m_activeBubbles.begin(); it != m_activeBubbles.end();) {
            if (!IsPedHandleValid(it->first) || now >= it->second.expiresAt) {
                it = m_activeBubbles.erase(it);
            } else {
                ++it;
            }
        }
    }

    void OnDrawing() {
        if (FrontEndMenuManager.m_bMenuActive || FrontEndMenuManager.m_bDrawRadarOrMap || m_activeBubbles.empty()) {
            return;
        }

        CPlayerPed *player = FindPlayerPed();
        if (!player) {
            return;
        }

        std::vector<BubbleRenderCandidate> candidates;
        candidates.reserve(m_activeBubbles.size());

        for (const auto &[pedRef, bubble] : m_activeBubbles) {
            CPed *ped = CPools::GetPed(pedRef);
            if (!ShouldTrackPed(ped, player)) {
                continue;
            }

            RwV3d headPos {};
            if (!GetBubbleAnchor(ped, headPos)) {
                continue;
            }

            RwV3d screenCoors {};
            float scaleW = 0.0f;
            float scaleH = 0.0f;
            if (!CSprite::CalcScreenCoors(headPos, &screenCoors, &scaleW, &scaleH, true, true)) {
                continue;
            }

            BubbleRenderCandidate candidate;
            candidate.pedRef = pedRef;
            candidate.distanceSq = DistanceSquared(ped->GetPosition(), player->GetPosition());
            candidate.screenX = screenCoors.x;
            candidate.screenY = screenCoors.y;
            candidate.text = bubble.text;
            candidates.push_back(candidate);
        }

        std::sort(candidates.begin(), candidates.end(), [](const BubbleRenderCandidate &a, const BubbleRenderCandidate &b) {
            return a.distanceSq < b.distanceSq;
        });

        for (const BubbleRenderCandidate &candidate : candidates) {
            BubbleLayout layout = MeasureBubbleLayout(candidate.text, candidate.screenX, candidate.screenY);
            ClampLayoutToScreen(layout);

            DrawCloudBubble(layout, candidate.screenY);
            SetupBubbleFont(layout.width - ScaleX(12.0f));
            CFont::SetColor(CRGBA(15, 15, 15, 255));

            const float lineHeight = ScaleY(10.0f);
            for (size_t i = 0; i < layout.lines.size(); ++i) {
                const std::string line = SanitizeBubbleText(layout.lines[i]);
                CFont::PrintString(layout.textX, layout.textY + static_cast<float>(i) * lineHeight, line.c_str());
            }
        }
    }
} gInstance;
