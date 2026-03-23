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
#include <extensions/ScriptCommands.h>
#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <winhttp.h>
#include <winsqlite/winsqlite3.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace plugin;

#pragma comment(lib, "winsqlite3.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "winmm.lib")

namespace {

constexpr float kMaxBubbleDistance = 25.0f;
constexpr float kMaxInteractionDistance = 8.5f;
constexpr unsigned int kBubbleLifetimeMs = 220;
constexpr unsigned int kInteractionBubbleLifetimeMs = 2600;
constexpr unsigned int kInteractionStatusLifetimeMs = 2600;
constexpr unsigned int kPlayerBubbleLifetimeMs = 2600;
constexpr int kMaxLinesPerBubble = 2;
constexpr int kMaxUiLines = 7;
constexpr int kMaxConversationHistoryLines = 6;
constexpr size_t kMaxCustomInputLength = 96;
constexpr int kPlayerTtsModelId = 0;
constexpr char kTargetLogRelativePath[] = "modding_sdk\\projects\\AIMOD\\data\\logs\\observed.tsv";
constexpr char kObservedRootRelativePath[] = "modding_sdk\\projects\\AIMOD\\data\\observed";
constexpr char kCatalogDbRelativePath[] = "modding_sdk\\projects\\AIMOD\\data\\aimod_catalog.db";
constexpr char kTtsStartServerRelativePath[] = "modding_sdk\\projects\\AIMOD\\tts\\start_tts_server.cmd";
constexpr char kLlmBridgeStartServerRelativePath[] = "modding_sdk\\projects\\AIMOD\\tools\\start_llm_bridge.cmd";
constexpr char kDbMissingSeedText[] = "[db:sin_seed]";
constexpr char kDbMissingCatalogText[] = "[db:sin_catalogo]";
constexpr char kDbMissingReplyText[] = "[db:reply_missing]";
constexpr char kAiPendingText[] = "...";
constexpr unsigned int kTtsStatusLifetimeMs = 3200;
constexpr unsigned int kTtsBusyStatusLifetimeMs = 1500;
constexpr wchar_t kTtsServerHost[] = L"127.0.0.1";
constexpr INTERNET_PORT kTtsServerPort = 5055;
constexpr wchar_t kTtsHealthPath[] = L"/health";
constexpr wchar_t kTtsSynthesizeVoicePath[] = L"/synthesize";
constexpr wchar_t kTtsSynthesizePath[] = L"/synthesize-for-ped";
constexpr wchar_t kLlmBridgeHost[] = L"127.0.0.1";
constexpr INTERNET_PORT kLlmBridgePort = 5056;
constexpr wchar_t kLlmBridgeHealthPath[] = L"/health";
constexpr wchar_t kLlmBridgeChatPath[] = L"/npc-chat";

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

enum class InteractionActionId {
    Greet,
    Ask,
    Insult,
    Threaten,
    Calm,
    Recruit,
    Dismiss
};

struct InteractionActionConfig {
    InteractionActionId id = InteractionActionId::Greet;
    std::string actionKey;
    std::string menuLabel;
    std::string playerText;
    int virtualKey = 0;
    int displayOrder = 0;
};

struct InteractionProfile {
    std::string groupName;
    std::string profileName;
    int aggression = 2;
    int bravery = 2;
    int authority = 0;
    int warmth = 2;
    int sociability = 2;
    int loyalty = 1;
};

struct PedInteractionMemory {
    int trust = 0;
    int anger = 0;
    int fear = 0;
    int respect = 0;
    int rapport = 0;
    int suspicion = 0;
    int encounters = 0;
    bool followingPlayer = false;
    unsigned int lastInteractionAt = 0;
};

struct GroupInteractionMemory {
    int trust = 0;
    int anger = 0;
    int fear = 0;
    int respect = 0;
    unsigned int lastInteractionAt = 0;
};

struct ConversationLine {
    bool fromPlayer = false;
    std::string text;
};

struct InteractionSession {
    bool open = false;
    int targetPedRef = -1;
    std::string targetName;
    std::string targetProfile;
    std::string playerText;
    unsigned int playerTextExpiresAt = 0;
    bool textEntryOpen = false;
    std::string inputBuffer;
    std::string inputIntentLabel;
    std::deque<ConversationLine> history;
};

struct TtsAssignment {
    std::string groupName;
    std::string voiceId;
    float pitch = 1.0f;
    float speed = 1.0f;
};

struct PedDialogueProfile {
    std::string groupName;
    std::string personaTitle;
    std::string temperament;
    std::string streetRole;
    std::string promptHint;
    std::string speechStyle;
    std::string slangPack;
    std::string verbalTick;
};

struct TunedInteractionProfile {
    InteractionProfile profile;
    int volatility = 0;
};

struct PedInstanceIdentity {
    std::string alias;
    std::string moodTag;
    std::string quirkWord;
    int aggressionBias = 0;
    int warmthBias = 0;
    int braveryBias = 0;
    int suspicionBias = 0;
    float pitchBias = 0.0f;
    float speedBias = 0.0f;
};

struct InteractionKeywordRule {
    std::string keyword;
    InteractionActionId actionId = InteractionActionId::Ask;
    int weight = 1;
};

struct TtsJob {
    int modelId = 0;
    int pedRef = -1;
    std::string voiceId;
    float pitch = 1.0f;
    float speed = 1.0f;
    std::string text;
};

struct AiJob {
    int pedRef = -1;
    int modelId = -1;
    std::string npcName;
    std::string groupName;
    std::string profileName;
    std::string playerText;
    InteractionActionId actionId = InteractionActionId::Ask;
    std::string fallbackReactionKey;
    unsigned int submittedAt = 0;
};

struct AiResult {
    int pedRef = -1;
    int modelId = -1;
    std::string groupName;
    std::string replyText;
    std::string reactionKey;
    InteractionActionId actionId = InteractionActionId::Ask;
    unsigned int submittedAt = 0;
    bool usedBridge = false;
};

struct RuntimeVoiceCatalog {
    bool attemptedLoad = false;
    bool loaded = false;
    std::unordered_map<int, std::string> modelNames;
    std::unordered_map<int, std::string> voiceLabels;
    std::unordered_map<int, std::vector<std::string>> modelSeedTexts;
    std::unordered_map<std::string, std::vector<std::string>> groupSeedTexts;
    std::unordered_map<int, TtsAssignment> ttsAssignments;
    std::unordered_map<std::string, std::vector<std::string>> ttsVoicePools;
    std::unordered_map<int, PedDialogueProfile> pedDialogueProfiles;
    std::vector<InteractionKeywordRule> interactionKeywordRules;
    std::vector<InteractionActionConfig> interactionActions;
    std::unordered_map<std::string, InteractionProfile> interactionProfiles;
    std::unordered_map<std::string, std::vector<std::string>> interactionReplies;
};

RuntimeVoiceCatalog g_runtimeCatalog;

void ResetRuntimeVoiceCatalog() {
    g_runtimeCatalog = RuntimeVoiceCatalog {};
}

float ScaleX(float value) {
    return value * static_cast<float>(RsGlobal.maximumWidth) / 640.0f;
}

float ScaleY(float value) {
    return value * static_cast<float>(RsGlobal.maximumHeight) / 448.0f;
}

std::string GetGameRootPath() {
    char exePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(exePath).parent_path().string();
}

std::string GetAbsoluteRuntimePath(const char *relativePath) {
    const std::string gameRoot = GetGameRootPath();
    if (gameRoot.empty()) {
        return {};
    }
    return (std::filesystem::path(gameRoot) / relativePath).string();
}

std::string GetCatalogDbAbsolutePath() {
    return GetAbsoluteRuntimePath(kCatalogDbRelativePath);
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
    if (IsBallasModel(modelId)) return "ballas";
    if (IsPoliceModel(modelId)) return "police";
    return GetVoiceTypeGroupName(voiceType);
}

int VirtualKeyForAction(InteractionActionId actionId) {
    switch (actionId) {
    case InteractionActionId::Greet: return '1';
    case InteractionActionId::Ask: return '2';
    case InteractionActionId::Insult: return '3';
    case InteractionActionId::Threaten: return '4';
    case InteractionActionId::Calm: return '5';
    case InteractionActionId::Recruit: return '6';
    case InteractionActionId::Dismiss: return '7';
    default: return 0;
    }
}

InteractionActionId ActionIdFromKey(const std::string &actionKey) {
    if (actionKey == "greet") return InteractionActionId::Greet;
    if (actionKey == "ask") return InteractionActionId::Ask;
    if (actionKey == "insult") return InteractionActionId::Insult;
    if (actionKey == "threaten") return InteractionActionId::Threaten;
    if (actionKey == "calm") return InteractionActionId::Calm;
    if (actionKey == "recruit") return InteractionActionId::Recruit;
    return InteractionActionId::Dismiss;
}

const char *ActionKeyName(InteractionActionId actionId) {
    switch (actionId) {
    case InteractionActionId::Greet: return "greet";
    case InteractionActionId::Ask: return "ask";
    case InteractionActionId::Insult: return "insult";
    case InteractionActionId::Threaten: return "threaten";
    case InteractionActionId::Calm: return "calm";
    case InteractionActionId::Recruit: return "recruit";
    case InteractionActionId::Dismiss: return "dismiss";
    default: return "dismiss";
    }
}

bool IsKnownReactionKey(const std::string &reactionKey) {
    return reactionKey == "friendly" ||
        reactionKey == "neutral" ||
        reactionKey == "warn" ||
        reactionKey == "dismiss" ||
        reactionKey == "refuse" ||
        reactionKey == "flee" ||
        reactionKey == "follow" ||
        reactionKey == "attack";
}

bool IsReactionAllowedForAction(InteractionActionId actionId, const std::string &reactionKey) {
    if (!IsKnownReactionKey(reactionKey)) {
        return false;
    }

    switch (actionId) {
    case InteractionActionId::Greet:
        return reactionKey == "friendly" || reactionKey == "neutral" || reactionKey == "dismiss" || reactionKey == "warn";
    case InteractionActionId::Ask:
        return reactionKey == "friendly" || reactionKey == "neutral" || reactionKey == "dismiss" || reactionKey == "warn" || reactionKey == "refuse";
    case InteractionActionId::Insult:
        return reactionKey == "dismiss" || reactionKey == "warn" || reactionKey == "attack";
    case InteractionActionId::Threaten:
        return reactionKey == "warn" || reactionKey == "attack" || reactionKey == "flee";
    case InteractionActionId::Calm:
        return reactionKey == "friendly" || reactionKey == "neutral" || reactionKey == "warn";
    case InteractionActionId::Recruit:
        return reactionKey == "follow" || reactionKey == "refuse" || reactionKey == "dismiss" || reactionKey == "neutral";
    case InteractionActionId::Dismiss:
        return reactionKey == "dismiss" || reactionKey == "warn" || reactionKey == "neutral";
    default:
        return false;
    }
}

std::string CanonicalizeReactionKey(
    const std::string &groupName,
    InteractionActionId actionId,
    const std::string &fallbackReactionKey,
    const std::string &suggestedReactionKey
) {
    if (suggestedReactionKey.empty() || !IsReactionAllowedForAction(actionId, suggestedReactionKey)) {
        return fallbackReactionKey;
    }

    if (actionId == InteractionActionId::Insult || actionId == InteractionActionId::Threaten) {
        return fallbackReactionKey;
    }

    if (groupName == "police") {
        if (actionId == InteractionActionId::Recruit) {
            return "refuse";
        }
        if (actionId == InteractionActionId::Dismiss && suggestedReactionKey == "dismiss") {
            return "warn";
        }
        if ((actionId == InteractionActionId::Greet || actionId == InteractionActionId::Ask || actionId == InteractionActionId::Calm) &&
            suggestedReactionKey == "friendly") {
            return fallbackReactionKey == "friendly" ? "neutral" : fallbackReactionKey;
        }
    }

    if ((groupName == "ballas" || groupName == "gang")) {
        if ((actionId == InteractionActionId::Greet || actionId == InteractionActionId::Ask) && suggestedReactionKey == "friendly") {
            return fallbackReactionKey;
        }
        if (actionId == InteractionActionId::Recruit && suggestedReactionKey == "friendly") {
            return "refuse";
        }
    }

    if (fallbackReactionKey == "follow" && suggestedReactionKey != "follow" && suggestedReactionKey != "refuse") {
        return fallbackReactionKey;
    }

    return suggestedReactionKey;
}

std::vector<InteractionActionConfig> GetFallbackInteractionActions() {
    return {
        { InteractionActionId::Greet, "greet", "1 Saludar", "Oye, buenas.", '1', 1 },
        { InteractionActionId::Ask, "ask", "2 Preguntar", "Necesito hablar contigo.", '2', 2 },
        { InteractionActionId::Insult, "insult", "3 Insultar", "Que te pasa, ah?", '3', 3 },
        { InteractionActionId::Threaten, "threaten", "4 Amenazar", "Bajale o te va mal.", '4', 4 },
        { InteractionActionId::Calm, "calm", "5 Calmar", "Tranqui, no busco problemas.", '5', 5 },
        { InteractionActionId::Recruit, "recruit", "6 Seguirme", "Ven conmigo un momento.", '6', 6 },
        { InteractionActionId::Dismiss, "dismiss", "7 Largate", "Ya estuvo. Sigue tu camino.", '7', 7 }
    };
}

std::string MakeReplyLookupKey(const std::string &groupName, const std::string &actionKey, const std::string &reactionKey) {
    return groupName + "|" + actionKey + "|" + reactionKey;
}

std::string TrimBubbleText(const std::string &text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string SanitizeBubbleText(const std::string &text) {
    const std::string trimmed = TrimBubbleText(text);
    return trimmed.empty() ? "..." : trimmed;
}

std::string SanitizeBubbleText(const char *text) {
    return text ? SanitizeBubbleText(std::string(text)) : "...";
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
        if (db) sqlite3_close(db);
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
            if (modelName) g_runtimeCatalog.modelNames[modelId] = reinterpret_cast<const char *>(modelName);
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
            if (!groupName || !text) continue;

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
    stmt = nullptr;

    const char *ttsSql =
        "SELECT model_id, group_name, voice_id, pitch, speed "
        "FROM ped_tts_assignments";
    if (sqlite3_prepare_v2(db, ttsSql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const int modelId = sqlite3_column_int(stmt, 0);
            const unsigned char *groupName = sqlite3_column_text(stmt, 1);
            const unsigned char *voiceId = sqlite3_column_text(stmt, 2);
            if (!groupName || !voiceId) continue;

            TtsAssignment assignment;
            assignment.groupName = reinterpret_cast<const char *>(groupName);
            assignment.voiceId = reinterpret_cast<const char *>(voiceId);
            assignment.pitch = static_cast<float>(sqlite3_column_double(stmt, 3));
            assignment.speed = static_cast<float>(sqlite3_column_double(stmt, 4));
            g_runtimeCatalog.ttsAssignments[modelId] = assignment;
        }
    }
    sqlite3_finalize(stmt);
    stmt = nullptr;

    const char *ttsPoolSql =
        "SELECT pool_name, voice_id "
        "FROM tts_voice_pools "
        "WHERE voice_id IS NOT NULL AND TRIM(voice_id) <> '' "
        "ORDER BY pool_name, voice_id";
    if (sqlite3_prepare_v2(db, ttsPoolSql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *poolName = sqlite3_column_text(stmt, 0);
            const unsigned char *voiceId = sqlite3_column_text(stmt, 1);
            if (!poolName || !voiceId) continue;

            g_runtimeCatalog.ttsVoicePools[reinterpret_cast<const char *>(poolName)].push_back(
                reinterpret_cast<const char *>(voiceId)
            );
        }
    }
    sqlite3_finalize(stmt);
    stmt = nullptr;

    const char *pedProfileSql =
        "SELECT model_id, group_name, persona_title, temperament, street_role, prompt_hint, speech_style, slang_pack, verbal_tick "
        "FROM ped_dialogue_profiles";
    if (sqlite3_prepare_v2(db, pedProfileSql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const int modelId = sqlite3_column_int(stmt, 0);
            const unsigned char *groupName = sqlite3_column_text(stmt, 1);
            const unsigned char *personaTitle = sqlite3_column_text(stmt, 2);
            const unsigned char *temperament = sqlite3_column_text(stmt, 3);
            const unsigned char *streetRole = sqlite3_column_text(stmt, 4);
            const unsigned char *promptHint = sqlite3_column_text(stmt, 5);
            const unsigned char *speechStyle = sqlite3_column_text(stmt, 6);
            const unsigned char *slangPack = sqlite3_column_text(stmt, 7);
            const unsigned char *verbalTick = sqlite3_column_text(stmt, 8);
            if (!groupName || !personaTitle || !temperament || !streetRole || !promptHint) continue;

            PedDialogueProfile profile;
            profile.groupName = reinterpret_cast<const char *>(groupName);
            profile.personaTitle = reinterpret_cast<const char *>(personaTitle);
            profile.temperament = reinterpret_cast<const char *>(temperament);
            profile.streetRole = reinterpret_cast<const char *>(streetRole);
            profile.promptHint = reinterpret_cast<const char *>(promptHint);
            profile.speechStyle = speechStyle ? reinterpret_cast<const char *>(speechStyle) : "";
            profile.slangPack = slangPack ? reinterpret_cast<const char *>(slangPack) : "";
            profile.verbalTick = verbalTick ? reinterpret_cast<const char *>(verbalTick) : "";
            g_runtimeCatalog.pedDialogueProfiles[modelId] = profile;
        }
    }
    sqlite3_finalize(stmt);
    stmt = nullptr;

    const char *keywordSql =
        "SELECT keyword, action_key, weight "
        "FROM interaction_keyword_rules "
        "WHERE keyword IS NOT NULL AND TRIM(keyword) <> ''";
    if (sqlite3_prepare_v2(db, keywordSql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *keyword = sqlite3_column_text(stmt, 0);
            const unsigned char *actionKey = sqlite3_column_text(stmt, 1);
            if (!keyword || !actionKey) continue;

            InteractionKeywordRule rule;
            rule.keyword = reinterpret_cast<const char *>(keyword);
            rule.actionId = ActionIdFromKey(reinterpret_cast<const char *>(actionKey));
            rule.weight = std::max(1, sqlite3_column_int(stmt, 2));
            g_runtimeCatalog.interactionKeywordRules.push_back(rule);
        }
    }
    sqlite3_finalize(stmt);
    stmt = nullptr;

    const char *actionSql =
        "SELECT action_key, display_order, menu_label_es, player_text_es "
        "FROM interaction_actions "
        "ORDER BY display_order, action_key";
    if (sqlite3_prepare_v2(db, actionSql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *actionKey = sqlite3_column_text(stmt, 0);
            const int displayOrder = sqlite3_column_int(stmt, 1);
            const unsigned char *menuLabel = sqlite3_column_text(stmt, 2);
            const unsigned char *playerText = sqlite3_column_text(stmt, 3);
            if (!actionKey || !menuLabel || !playerText) continue;

            InteractionActionConfig cfg;
            cfg.actionKey = reinterpret_cast<const char *>(actionKey);
            cfg.id = ActionIdFromKey(cfg.actionKey);
            cfg.menuLabel = reinterpret_cast<const char *>(menuLabel);
            cfg.playerText = reinterpret_cast<const char *>(playerText);
            cfg.virtualKey = VirtualKeyForAction(cfg.id);
            cfg.displayOrder = displayOrder;
            g_runtimeCatalog.interactionActions.push_back(cfg);
        }
    }
    sqlite3_finalize(stmt);
    stmt = nullptr;

    const char *profileSql =
        "SELECT group_name, profile_name, aggression, bravery, authority, warmth, sociability, loyalty "
        "FROM interaction_profiles";
    if (sqlite3_prepare_v2(db, profileSql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *groupName = sqlite3_column_text(stmt, 0);
            const unsigned char *profileName = sqlite3_column_text(stmt, 1);
            if (!groupName || !profileName) continue;

            InteractionProfile profile;
            profile.groupName = reinterpret_cast<const char *>(groupName);
            profile.profileName = reinterpret_cast<const char *>(profileName);
            profile.aggression = sqlite3_column_int(stmt, 2);
            profile.bravery = sqlite3_column_int(stmt, 3);
            profile.authority = sqlite3_column_int(stmt, 4);
            profile.warmth = sqlite3_column_int(stmt, 5);
            profile.sociability = sqlite3_column_int(stmt, 6);
            profile.loyalty = sqlite3_column_int(stmt, 7);
            g_runtimeCatalog.interactionProfiles[profile.groupName] = profile;
        }
    }
    sqlite3_finalize(stmt);
    stmt = nullptr;

    const char *replySql =
        "SELECT group_name, action_key, reaction_key, reply_text_es "
        "FROM interaction_replies "
        "WHERE reply_text_es IS NOT NULL AND TRIM(reply_text_es) <> ''";
    if (sqlite3_prepare_v2(db, replySql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *groupName = sqlite3_column_text(stmt, 0);
            const unsigned char *actionKey = sqlite3_column_text(stmt, 1);
            const unsigned char *reactionKey = sqlite3_column_text(stmt, 2);
            const unsigned char *replyText = sqlite3_column_text(stmt, 3);
            if (!groupName || !actionKey || !reactionKey || !replyText) continue;

            g_runtimeCatalog.interactionReplies[MakeReplyLookupKey(
                reinterpret_cast<const char *>(groupName),
                reinterpret_cast<const char *>(actionKey),
                reinterpret_cast<const char *>(reactionKey)
            )].push_back(reinterpret_cast<const char *>(replyText));
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (g_runtimeCatalog.interactionActions.empty()) {
        g_runtimeCatalog.interactionActions = GetFallbackInteractionActions();
    }

    g_runtimeCatalog.loaded = true;
}

const std::vector<InteractionActionConfig> &GetInteractionActions() {
    LoadRuntimeVoiceCatalog();
    if (g_runtimeCatalog.interactionActions.empty()) {
        static std::vector<InteractionActionConfig> fallback = GetFallbackInteractionActions();
        return fallback;
    }
    return g_runtimeCatalog.interactionActions;
}

InteractionProfile GetInteractionProfileForGroup(const std::string &groupName) {
    LoadRuntimeVoiceCatalog();
    const auto it = g_runtimeCatalog.interactionProfiles.find(groupName);
    if (it != g_runtimeCatalog.interactionProfiles.end()) return it->second;

    const auto fallback = g_runtimeCatalog.interactionProfiles.find("default");
    if (fallback != g_runtimeCatalog.interactionProfiles.end()) return fallback->second;

    InteractionProfile profile;
    profile.groupName = "default";
    profile.profileName = "Desconocido";
    return profile;
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

std::string GetPedPersonaTitle(int modelId) {
    LoadRuntimeVoiceCatalog();
    const auto it = g_runtimeCatalog.pedDialogueProfiles.find(modelId);
    if (it != g_runtimeCatalog.pedDialogueProfiles.end() && !it->second.personaTitle.empty()) {
        return it->second.personaTitle;
    }
    return {};
}

const PedDialogueProfile *GetPedDialogueProfileData(int modelId) {
    LoadRuntimeVoiceCatalog();
    const auto it = g_runtimeCatalog.pedDialogueProfiles.find(modelId);
    if (it != g_runtimeCatalog.pedDialogueProfiles.end()) {
        return &it->second;
    }
    return nullptr;
}

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool ContainsWord(const std::string &haystack, const char *needle) {
    return haystack.find(needle) != std::string::npos;
}

TunedInteractionProfile TuneInteractionProfileForPed(int modelId, const InteractionProfile &baseProfile) {
    TunedInteractionProfile tuned;
    tuned.profile = baseProfile;

    const PedDialogueProfile *dialogue = GetPedDialogueProfileData(modelId);
    if (!dialogue) {
        return tuned;
    }

    const std::string temperament = ToLowerCopy(dialogue->temperament);
    const std::string streetRole = ToLowerCopy(dialogue->streetRole);

    auto clampStat = [](int value) {
        return std::clamp(value, 0, 7);
    };

    if (ContainsWord(temperament, "agresivo")) {
        tuned.profile.aggression += 2;
        tuned.profile.bravery += 1;
        tuned.profile.warmth -= 1;
        tuned.volatility += 1;
    }
    if (ContainsWord(temperament, "paranoico")) {
        tuned.profile.aggression += 1;
        tuned.profile.bravery -= 1;
        tuned.profile.sociability -= 1;
        tuned.profile.warmth -= 1;
        tuned.volatility += 2;
    }
    if (ContainsWord(temperament, "burlon")) {
        tuned.profile.aggression += 1;
        tuned.profile.sociability += 1;
    }
    if (ContainsWord(temperament, "territorial")) {
        tuned.profile.aggression += 1;
        tuned.profile.bravery += 1;
        tuned.profile.loyalty += 1;
    }
    if (ContainsWord(temperament, "autoritario")) {
        tuned.profile.authority += 2;
        tuned.profile.bravery += 1;
        tuned.profile.warmth -= 1;
    }
    if (ContainsWord(temperament, "seco")) {
        tuned.profile.warmth -= 2;
        tuned.profile.sociability -= 1;
    }
    if (ContainsWord(temperament, "impaciente")) {
        tuned.profile.aggression += 1;
        tuned.profile.warmth -= 1;
        tuned.profile.sociability -= 1;
        tuned.volatility += 2;
    }
    if (ContainsWord(temperament, "disciplinado")) {
        tuned.profile.authority += 1;
        tuned.profile.bravery += 1;
        tuned.profile.loyalty += 1;
        tuned.profile.aggression -= 1;
    }
    if (ContainsWord(temperament, "desconfiado")) {
        tuned.profile.aggression += 1;
        tuned.profile.bravery -= 1;
        tuned.profile.sociability -= 2;
        tuned.profile.warmth -= 1;
    }
    if (ContainsWord(temperament, "callejero")) {
        tuned.profile.aggression += 1;
        tuned.profile.bravery += 1;
        tuned.profile.sociability += 1;
    }
    if (ContainsWord(temperament, "picado")) {
        tuned.profile.aggression += 2;
        tuned.profile.warmth -= 1;
        tuned.volatility += 1;
    }
    if (ContainsWord(temperament, "leal")) {
        tuned.profile.loyalty += 2;
        tuned.profile.bravery += 1;
        tuned.profile.warmth += 1;
    }
    if (ContainsWord(temperament, "urgente")) {
        tuned.profile.authority += 1;
        tuned.profile.sociability -= 1;
        tuned.profile.warmth -= 1;
        tuned.volatility += 1;
    }
    if (ContainsWord(temperament, "practico")) {
        tuned.profile.authority += 1;
        tuned.profile.warmth -= 1;
    }
    if (ContainsWord(temperament, "sereno")) {
        tuned.profile.aggression -= 1;
        tuned.profile.warmth += 1;
    }
    if (ContainsWord(temperament, "firme")) {
        tuned.profile.authority += 1;
        tuned.profile.bravery += 1;
    }
    if (ContainsWord(temperament, "enigmatico")) {
        tuned.profile.sociability -= 1;
        tuned.profile.warmth -= 1;
    }
    if (ContainsWord(temperament, "teatral")) {
        tuned.profile.sociability += 1;
        tuned.profile.bravery += 1;
    }
    if (ContainsWord(temperament, "intenso")) {
        tuned.profile.aggression += 1;
        tuned.profile.bravery += 1;
        tuned.volatility += 1;
    }
    if (ContainsWord(temperament, "raro")) {
        tuned.profile.sociability -= 1;
        tuned.profile.warmth -= 1;
    }
    if (ContainsWord(temperament, "amigable")) {
        tuned.profile.warmth += 2;
        tuned.profile.sociability += 2;
        tuned.profile.aggression -= 1;
    }
    if (ContainsWord(temperament, "cansado")) {
        tuned.profile.bravery -= 1;
        tuned.profile.sociability -= 1;
        tuned.profile.warmth -= 1;
    }
    if (ContainsWord(temperament, "curioso")) {
        tuned.profile.warmth += 1;
        tuned.profile.sociability += 2;
    }
    if (ContainsWord(temperament, "apresurado")) {
        tuned.profile.warmth -= 1;
        tuned.profile.sociability -= 1;
        tuned.volatility += 1;
    }
    if (ContainsWord(temperament, "defensivo")) {
        tuned.profile.aggression += 1;
        tuned.profile.bravery -= 1;
        tuned.profile.warmth -= 1;
        tuned.volatility += 1;
    }
    if (ContainsWord(temperament, "resuelto")) {
        tuned.profile.bravery += 2;
        tuned.profile.loyalty += 1;
        tuned.profile.warmth += 1;
        tuned.profile.authority += 1;
    }

    if (ContainsWord(streetRole, "protagonista")) {
        tuned.profile.bravery += 2;
        tuned.profile.loyalty += 2;
        tuned.profile.authority += 1;
    }
    if (ContainsWord(streetRole, "oficial") || ContainsWord(streetRole, "policia") || ContainsWord(streetRole, "sheriff") || ContainsWord(streetRole, "agente")) {
        tuned.profile.authority += 1;
        tuned.profile.bravery += 1;
    }
    if (ContainsWord(streetRole, "soldado") || ContainsWord(streetRole, "halcon") || ContainsWord(streetRole, "vago") || ContainsWord(streetRole, "tirador") || ContainsWord(streetRole, "mano derecha")) {
        tuned.profile.aggression += 1;
        tuned.profile.loyalty += 1;
        tuned.profile.bravery += 1;
    }
    if (ContainsWord(streetRole, "paramedico") || ContainsWord(streetRole, "bombero") || ContainsWord(streetRole, "rescatista") || ContainsWord(streetRole, "medico")) {
        tuned.profile.authority += 1;
        tuned.profile.warmth += 1;
        tuned.profile.aggression -= 1;
    }
    if (ContainsWord(streetRole, "metiche")) {
        tuned.profile.sociability += 1;
        tuned.profile.bravery -= 1;
    }
    if (ContainsWord(streetRole, "buscavidas")) {
        tuned.profile.sociability += 1;
        tuned.profile.loyalty -= 1;
    }
    if (ContainsWord(streetRole, "sobreviviente")) {
        tuned.profile.bravery += 1;
    }
    if (ContainsWord(streetRole, "peaton curtido")) {
        tuned.profile.bravery += 1;
        tuned.profile.warmth -= 1;
    }

    tuned.profile.aggression = clampStat(tuned.profile.aggression);
    tuned.profile.bravery = clampStat(tuned.profile.bravery);
    tuned.profile.authority = clampStat(tuned.profile.authority);
    tuned.profile.warmth = clampStat(tuned.profile.warmth);
    tuned.profile.sociability = clampStat(tuned.profile.sociability);
    tuned.profile.loyalty = clampStat(tuned.profile.loyalty);
    tuned.volatility = std::clamp(tuned.volatility, 0, 3);
    return tuned;
}

void ReplaceAllCaseSensitive(std::string &value, const std::string &from, const std::string &to) {
    if (from.empty()) {
        return;
    }

    size_t startPos = 0;
    while ((startPos = value.find(from, startPos)) != std::string::npos) {
        value.replace(startPos, from.length(), to);
        startPos += to.length();
    }
}

std::string EnsureSentencePunctuation(const std::string &text, char ch) {
    std::string result = TrimBubbleText(text);
    if (result.empty()) {
        return result;
    }
    const char last = result.back();
    if (last == '.' || last == '!' || last == '?') {
        return result;
    }
    result.push_back(ch);
    return result;
}

std::string StylizeReplyForPed(int modelId, const std::string &baseText, InteractionActionId actionId, const std::string &reactionKey) {
    const PedDialogueProfile *dialogue = GetPedDialogueProfileData(modelId);
    if (!dialogue) {
        return SanitizeBubbleText(baseText);
    }

    std::string text = SanitizeBubbleText(baseText);
    if (text.empty()) {
        return text;
    }

    const std::string speechStyle = ToLowerCopy(dialogue->speechStyle);
    const std::string slangPack = ToLowerCopy(dialogue->slangPack);
    const std::string verbalTick = TrimBubbleText(dialogue->verbalTick);
    const std::string temperament = ToLowerCopy(dialogue->temperament);
    const std::string streetRole = ToLowerCopy(dialogue->streetRole);
    const int styleSeed = std::abs((modelId * 17) + (static_cast<int>(actionId) * 31) + static_cast<int>(reactionKey.size()));

    if (slangPack == "mx_ballas") {
        ReplaceAllCaseSensitive(text, "compa", "carnal");
        ReplaceAllCaseSensitive(text, "hermano", "carnal");
        ReplaceAllCaseSensitive(text, "loco", "perro");
        if (reactionKey == "warn" && text.find("al tiro") == std::string::npos) {
            text = EnsureSentencePunctuation(text, '.') + " Al tiro.";
        }
    } else if (slangPack == "street_latam") {
        ReplaceAllCaseSensitive(text, "compa", "bro");
        ReplaceAllCaseSensitive(text, "amigo", "bro");
        if (reactionKey == "follow") {
            text = EnsureSentencePunctuation(text, '.') + " Vamos.";
        }
    } else if (slangPack == "police") {
        ReplaceAllCaseSensitive(text, "compa", "ciudadano");
        ReplaceAllCaseSensitive(text, "bro", "ciudadano");
        if (reactionKey == "warn") {
            text = EnsureSentencePunctuation(text, '.');
        }
    } else if (slangPack == "emergency") {
        ReplaceAllCaseSensitive(text, "compa", "senor");
        ReplaceAllCaseSensitive(text, "bro", "senor");
    } else if (slangPack == "civil_female") {
        if (reactionKey == "friendly" && styleSeed % 3 == 0) {
            text = "Oye, " + text;
        }
    } else if (slangPack == "civil_male") {
        if (reactionKey == "neutral" && styleSeed % 3 == 1) {
            text = "Mira, " + text;
        }
    } else if (slangPack == "cj_ls") {
        ReplaceAllCaseSensitive(text, "compa", "homie");
    }

    if (speechStyle == "leader_street") {
        if (reactionKey == "friendly" && styleSeed % 2 == 0) {
            text = "Todo bien, " + text;
        } else if ((reactionKey == "warn" || reactionKey == "attack") && styleSeed % 2 == 1) {
            text = EnsureSentencePunctuation(text, '!') + " Ponte serio.";
        }
    } else if (speechStyle == "commanding" || speechStyle == "procedural") {
        text = EnsureSentencePunctuation(text, '.');
        if ((reactionKey == "warn" || reactionKey == "attack") && styleSeed % 2 == 0) {
            text += " Ahora.";
        }
    } else if (speechStyle == "streetwise") {
        if (styleSeed % 2 == 0) {
            text = "Mira bien, " + text;
        }
    } else if (speechStyle == "taunting" || speechStyle == "hotheaded") {
        text = EnsureSentencePunctuation(text, '!');
    } else if (speechStyle == "snappy") {
        if (styleSeed % 2 == 0) {
            text = EnsureSentencePunctuation(text, '.');
        }
        if (reactionKey == "dismiss") {
            text = "Ya, " + text;
        }
    } else if (speechStyle == "warm") {
        if (reactionKey == "friendly" && styleSeed % 2 == 0) {
            text = "Oye, " + text;
        }
    } else if (speechStyle == "curious") {
        if (reactionKey == "neutral" || reactionKey == "friendly") {
            text = "Entonces... " + text;
        }
    } else if (speechStyle == "guarded") {
        if (reactionKey == "warn" || reactionKey == "dismiss") {
            text = "Mejor calmado, " + text;
        }
    } else if (speechStyle == "cryptic") {
        if (styleSeed % 2 == 0) {
            text = "Mira... " + text;
        }
    } else if (speechStyle == "dramatic") {
        text = EnsureSentencePunctuation(text, '!');
        if (styleSeed % 2 == 0) {
            text = "Escucha bien: " + text;
        }
    } else if (speechStyle == "intense") {
        text = EnsureSentencePunctuation(text, '!');
        if (reactionKey == "attack" || reactionKey == "warn") {
            text += " Sin vueltas.";
        }
    } else if (speechStyle == "odd") {
        if (styleSeed % 2 == 1) {
            text = "Hmm... " + text;
        }
    } else if (speechStyle == "urgent") {
        text = EnsureSentencePunctuation(text, '.');
        if (text.find("rapido") == std::string::npos && styleSeed % 2 == 1) {
            text += " Rapido.";
        }
    } else if (speechStyle == "calm") {
        text = EnsureSentencePunctuation(text, '.');
        if (reactionKey == "friendly" && styleSeed % 2 == 0) {
            text = "Tranquilo, " + text;
        }
    } else if (speechStyle == "hurried") {
        if (styleSeed % 2 == 0) {
            text = "Ya, " + text;
        }
    } else if (speechStyle == "tired") {
        if (styleSeed % 2 == 0) {
            text = "Uf... " + text;
        }
    }

    if (temperament == "paranoico" && (reactionKey == "warn" || reactionKey == "dismiss") && styleSeed % 3 == 0) {
        text = "No te me acerques, " + text;
    } else if (temperament == "territorial" && (reactionKey == "warn" || reactionKey == "attack") && styleSeed % 2 == 0) {
        text = EnsureSentencePunctuation(text, '!') + " Este lado tiene dueno.";
    } else if (temperament == "autoritario" && reactionKey == "warn" && styleSeed % 2 == 0) {
        text = "Ultima advertencia. " + text;
    } else if (temperament == "disciplinado" && reactionKey == "warn" && styleSeed % 3 == 1) {
        text = "Mantenga la distancia. " + text;
    } else if (temperament == "amigable" && reactionKey == "friendly" && styleSeed % 2 == 0) {
        text = "Tranqui, " + text;
    } else if (temperament == "curioso" && reactionKey == "friendly" && styleSeed % 2 == 1) {
        text = "A ver, " + text;
    } else if (temperament == "apresurado" && reactionKey == "dismiss" && styleSeed % 2 == 0) {
        text = "Ando corto de tiempo, " + text;
    } else if (temperament == "defensivo" && (reactionKey == "warn" || reactionKey == "dismiss") && styleSeed % 2 == 1) {
        text = "No busco problemas, " + text;
    }

    if (streetRole.find("veterano") != std::string::npos && reactionKey == "warn" && styleSeed % 2 == 0) {
        text = EnsureSentencePunctuation(text, '.') + " Ya te lo dije.";
    } else if (streetRole.find("halcon") != std::string::npos && (reactionKey == "warn" || reactionKey == "attack") && styleSeed % 2 == 1) {
        text = EnsureSentencePunctuation(text, '!') + " Te estoy midiendo.";
    } else if (streetRole.find("rescatista") != std::string::npos && reactionKey == "friendly" && styleSeed % 2 == 0) {
        text = "Respira. " + text;
    } else if (streetRole.find("buscavidas") != std::string::npos && reactionKey == "neutral" && styleSeed % 3 == 2) {
        text = "Yo ando en lo mio, " + text;
    } else if (streetRole.find("mano derecha") != std::string::npos && reactionKey == "attack" && styleSeed % 2 == 0) {
        text = EnsureSentencePunctuation(text, '!') + " Aqui nadie juega.";
    }

    if (!verbalTick.empty()) {
        const bool canAppendTick = reactionKey != "attack" && reactionKey != "flee";
        if (canAppendTick && text.find(verbalTick) == std::string::npos) {
            if (styleSeed % 4 == 0) {
                text = verbalTick + ", " + text;
            } else if (styleSeed % 4 == 1) {
                text = EnsureSentencePunctuation(text, '.') + " " + verbalTick + ".";
            }
        }
    }

    return SanitizeBubbleText(text);
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

std::string PickInteractionReply(const std::string &groupName, InteractionActionId actionId, const std::string &reactionKey, unsigned int seed);

const TtsAssignment *GetTtsAssignmentForModel(int modelId) {
    LoadRuntimeVoiceCatalog();
    const auto it = g_runtimeCatalog.ttsAssignments.find(modelId);
    if (it != g_runtimeCatalog.ttsAssignments.end()) {
        return &it->second;
    }
    return nullptr;
}

bool IsLikelyFemaleModel(int modelId) {
    if (const PedDialogueProfile *dialogue = GetPedDialogueProfileData(modelId)) {
        if (ToLowerCopy(dialogue->slangPack) == "civil_female") {
            return true;
        }
    }

    std::string modelName = ToLowerCopy(GetCatalogModelName(modelId));
    if (modelName.size() >= 2) {
        const std::string prefix = modelName.substr(0, 2);
        return prefix == "bf" || prefix == "hf" || prefix == "vf" || prefix == "wf";
    }
    return false;
}

const std::vector<std::string> *GetTtsVoicePool(const std::string &poolName) {
    LoadRuntimeVoiceCatalog();
    const auto it = g_runtimeCatalog.ttsVoicePools.find(poolName);
    if (it != g_runtimeCatalog.ttsVoicePools.end() && !it->second.empty()) {
        return &it->second;
    }
    return nullptr;
}

std::string ResolveTtsVoicePoolName(int modelId, const std::string &groupName) {
    const bool female = IsLikelyFemaleModel(modelId);
    if (modelId == kPlayerTtsModelId) {
        return "player_cj";
    }
    if (groupName == "ballas" || groupName == "gang") {
        return female ? "street_female" : "street_male";
    }
    if (groupName == "police" || groupName == "emergency" || groupName == "gfd") {
        return female ? "authority_female" : "authority_male";
    }
    if (groupName == "special") {
        return female ? "special_female" : "special_male";
    }
    return female ? "civil_female" : "civil_male";
}

std::string PickVoiceIdForInstance(int modelId, int pedRef, const TtsAssignment &assignment) {
    const std::string poolName = ResolveTtsVoicePoolName(modelId, assignment.groupName);
    const std::vector<std::string> *pool = GetTtsVoicePool(poolName);
    if (!pool || pool->empty()) {
        return assignment.voiceId;
    }

    const unsigned int seed = static_cast<unsigned int>(std::abs((modelId * 73) + ((pedRef == -1 ? modelId : pedRef) * 17)));
    return (*pool)[seed % static_cast<unsigned int>(pool->size())];
}

float ClampPitch(float value) {
    return std::max(0.82f, std::min(1.22f, value));
}

float ClampSpeed(float value) {
    return std::max(0.86f, std::min(1.18f, value));
}

std::string BuildPedStreetAlias(int pedRef, int modelId, const std::string &groupName);

PedInstanceIdentity DescribePedInstance(int pedRef, int modelId, const std::string &groupName) {
    static const std::array<const char *, 8> kStreetMood = { "encendido", "mosca", "territorial", "afilado", "frio", "retador", "picado", "relajado" };
    static const std::array<const char *, 6> kPoliceMood = { "frio", "duro", "metodico", "cansado", "serio", "hostil" };
    static const std::array<const char *, 7> kCivilMood = { "curioso", "apurado", "guardado", "buena_onda", "cansado", "metiche", "tranquilo" };
    static const std::array<const char *, 6> kSpecialMood = { "raro", "teatral", "mistico", "pesado", "intenso", "callado" };
    static const std::array<const char *, 8> kStreetQuirk = { "mira", "perro", "carnal", "ya", "eh", "ojo", "dale", "firme" };
    static const std::array<const char *, 6> kPoliceQuirk = { "atento", "proceda", "muevase", "claro", "entendido", "ahora" };
    static const std::array<const char *, 7> kCivilQuirk = { "oye", "pues", "eh", "mira", "compa", "ya", "a ver" };
    static const std::array<const char *, 6> kSpecialQuirk = { "hmm", "escucha", "mira", "ojo", "curioso", "shh" };

    const unsigned int baseSeed = static_cast<unsigned int>(std::abs((modelId * 97) + ((pedRef == -1 ? modelId : pedRef) * 131)));
    const bool female = IsLikelyFemaleModel(modelId);
    const auto pickMood = [&](const auto &items) {
        return std::string(items[baseSeed % items.size()]);
    };
    const auto pickQuirk = [&](const auto &items) {
        return std::string(items[(baseSeed / 3u) % items.size()]);
    };

    PedInstanceIdentity identity;
    if (modelId == kPlayerTtsModelId || groupName == "player") {
        identity.alias = "CJ";
        identity.moodTag = "resuelto";
        identity.quirkWord = "homie";
        identity.aggressionBias = 1;
        identity.warmthBias = 1;
        identity.braveryBias = 2;
        identity.suspicionBias = 0;
        identity.pitchBias = -0.02f;
        identity.speedBias = -0.01f;
        return identity;
    }

    identity.alias = BuildPedStreetAlias(pedRef, modelId, groupName);

    if (groupName == "police" || groupName == "emergency" || groupName == "gfd") {
        identity.moodTag = pickMood(kPoliceMood);
        identity.quirkWord = pickQuirk(kPoliceQuirk);
        identity.aggressionBias = static_cast<int>(baseSeed % 3u) - 1;
        identity.warmthBias = -1 - static_cast<int>(baseSeed % 2u);
        identity.braveryBias = static_cast<int>((baseSeed / 5u) % 3u);
        identity.suspicionBias = 1 + static_cast<int>((baseSeed / 7u) % 2u);
        identity.pitchBias = female ? 0.02f : -0.015f;
        identity.speedBias = -0.02f + static_cast<float>((baseSeed % 5u)) * 0.008f;
    } else if (groupName == "ballas" || groupName == "gang") {
        identity.moodTag = pickMood(kStreetMood);
        identity.quirkWord = pickQuirk(kStreetQuirk);
        identity.aggressionBias = static_cast<int>(baseSeed % 5u) - 1;
        identity.warmthBias = -1;
        identity.braveryBias = static_cast<int>((baseSeed / 11u) % 5u) - 1;
        identity.suspicionBias = static_cast<int>((baseSeed / 13u) % 3u);
        identity.pitchBias = female ? 0.03f : (-0.03f + static_cast<float>(baseSeed % 4u) * 0.01f);
        identity.speedBias = 0.01f + static_cast<float>((baseSeed / 17u) % 4u) * 0.01f;
    } else if (groupName == "special") {
        identity.moodTag = pickMood(kSpecialMood);
        identity.quirkWord = pickQuirk(kSpecialQuirk);
        identity.aggressionBias = static_cast<int>((baseSeed / 19u) % 5u) - 2;
        identity.warmthBias = static_cast<int>((baseSeed / 23u) % 5u) - 2;
        identity.braveryBias = static_cast<int>((baseSeed / 29u) % 5u) - 2;
        identity.suspicionBias = static_cast<int>((baseSeed / 31u) % 3u);
        identity.pitchBias = -0.02f + static_cast<float>((baseSeed % 6u)) * 0.01f;
        identity.speedBias = -0.03f + static_cast<float>((baseSeed / 5u) % 7u) * 0.01f;
    } else {
        identity.moodTag = pickMood(kCivilMood);
        identity.quirkWord = pickQuirk(kCivilQuirk);
        identity.aggressionBias = -1 + static_cast<int>(baseSeed % 3u);
        identity.warmthBias = static_cast<int>((baseSeed / 7u) % 5u) - 1;
        identity.braveryBias = static_cast<int>((baseSeed / 9u) % 5u) - 2;
        identity.suspicionBias = static_cast<int>((baseSeed / 15u) % 3u);
        identity.pitchBias = female ? (0.01f + static_cast<float>(baseSeed % 4u) * 0.01f) : (-0.02f + static_cast<float>(baseSeed % 4u) * 0.01f);
        identity.speedBias = -0.01f + static_cast<float>((baseSeed / 21u) % 5u) * 0.008f;
    }

    return identity;
}

std::string BuildPedStreetAlias(int pedRef, int modelId, const std::string &groupName) {
    static const std::array<const char *, 10> kStreetMale = { "Sombra", "Navaja", "Roco", "Tigre", "Chino", "Fierro", "Rata", "Flaco", "Ghost", "Trueno" };
    static const std::array<const char *, 8> kStreetFemale = { "Loba", "Nena", "Roxy", "Siren", "China", "Mamba", "Mika", "Brava" };
    static const std::array<const char *, 8> kPolice = { "Bravo", "Delta", "Soto", "Rojas", "Mendez", "Vega", "Sierra", "Stone" };
    static const std::array<const char *, 8> kCivil = { "Veci", "Pana", "Mota", "Rulo", "Tessa", "Yeyo", "Nico", "Gaby" };
    static const std::array<const char *, 8> kSpecial = { "Zero", "Oracle", "Mistica", "Frost", "Rune", "Echo", "Nova", "Shade" };

    const bool female = IsLikelyFemaleModel(modelId);
    const unsigned int seed = static_cast<unsigned int>(std::abs((modelId * 41) + ((pedRef == -1 ? modelId : pedRef) * 29)));

    if (groupName == "police" || groupName == "emergency" || groupName == "gfd") {
        return kPolice[seed % kPolice.size()];
    }
    if (groupName == "ballas" || groupName == "gang") {
        return female ? kStreetFemale[seed % kStreetFemale.size()] : kStreetMale[seed % kStreetMale.size()];
    }
    if (groupName == "special") {
        return kSpecial[seed % kSpecial.size()];
    }
    return kCivil[seed % kCivil.size()];
}

std::string ApplyMemoryInflection(int pedRef, int modelId, const std::string &groupName, const std::string &baseText, const PedInteractionMemory &memory, const std::string &reactionKey) {
    std::string text = SanitizeBubbleText(baseText);
    if (text.empty()) {
        return text;
    }

    const PedInstanceIdentity identity = DescribePedInstance(pedRef, modelId, groupName);
    const int memorySeed = std::abs((modelId * 13) + (memory.encounters * 7) + (memory.rapport * 11) + (memory.suspicion * 17));
    if (memory.encounters >= 3 && memory.rapport >= 3 && (reactionKey == "friendly" || reactionKey == "neutral")) {
        if (memorySeed % 2 == 0) {
            text = "Ya te ubico, " + text;
        } else {
            text = "Contigo todo bien, " + text;
        }
    } else if (memory.suspicion >= 3 && (reactionKey == "warn" || reactionKey == "dismiss" || reactionKey == "attack")) {
        text = "Otra vez tu, " + text;
    } else if (memory.respect >= 3 && (reactionKey == "follow" || reactionKey == "friendly")) {
        text = "Va por respeto, " + text;
    } else if (memory.fear >= 3 && reactionKey == "flee") {
        text = "Ni loco, " + text;
    }

    if (memory.anger >= 5 && reactionKey == "attack" && text.find("Se acabo") == std::string::npos) {
        text = EnsureSentencePunctuation(text, '!') + " Se acabo.";
    }

    if (!identity.quirkWord.empty() && text.find(identity.quirkWord) == std::string::npos && reactionKey != "attack" && reactionKey != "flee") {
        if (memorySeed % 5 == 0) {
            text = identity.quirkWord + ", " + text;
        } else if (memorySeed % 5 == 1) {
            text = EnsureSentencePunctuation(text, '.') + " " + identity.quirkWord + ".";
        }
    }

    if (identity.moodTag == "guardado" && (reactionKey == "warn" || reactionKey == "dismiss") && text.find("con calma") == std::string::npos) {
        text = "Con calma, " + text;
    } else if (identity.moodTag == "encendido" && reactionKey == "attack") {
        text = EnsureSentencePunctuation(text, '!') + " Ya estuvo.";
    } else if (identity.moodTag == "curioso" && reactionKey == "neutral" && memorySeed % 3 == 0) {
        text = "A ver, " + text;
    } else if (identity.moodTag == "metodico" && reactionKey == "warn" && text.find("paso a paso") == std::string::npos) {
        text = EnsureSentencePunctuation(text, '.') + " Paso a paso.";
    }

    return SanitizeBubbleText(text);
}

std::string ComposePedReplyText(int pedRef, int modelId, const std::string &groupName, InteractionActionId actionId, const std::string &reactionKey, unsigned int seed, const PedInteractionMemory *memory) {
    std::string reply = StylizeReplyForPed(modelId, PickInteractionReply(groupName, actionId, reactionKey, seed), actionId, reactionKey);
    if (memory) {
        reply = ApplyMemoryInflection(pedRef, modelId, groupName, reply, *memory, reactionKey);
    } else {
        const PedInstanceIdentity identity = DescribePedInstance(pedRef, modelId, groupName);
        if (!identity.quirkWord.empty() && reply.find(identity.quirkWord) == std::string::npos && (seed % 4u) == 0u) {
            reply = identity.quirkWord + ", " + reply;
        }
    }
    return reply.empty() ? kDbMissingReplyText : reply;
}

std::string SummarizePedAttitude(const PedInteractionMemory &memory) {
    if (memory.anger >= 5) {
        return "a punto de explotar";
    }
    if (memory.suspicion >= 4) {
        return "te tiene en la mira";
    }
    if (memory.fear >= 4) {
        return "te teme";
    }
    if (memory.followingPlayer) {
        return "te sigue";
    }
    if (memory.rapport >= 4 && memory.trust >= 3) {
        return "ya te ubica";
    }
    if (memory.respect >= 4) {
        return "te respeta";
    }
    if (memory.encounters >= 2) {
        return "te esta midiendo";
    }
    return "sin lectura clara";
}

std::string GetResolvedGroupName(int modelId, short voiceType) {
    if (const TtsAssignment *assignment = GetTtsAssignmentForModel(modelId)) {
        if (!assignment->groupName.empty()) {
            return assignment->groupName;
        }
    }
    return GetTargetGroupName(modelId, voiceType);
}

std::string JsonEscape(const std::string &value) {
    std::ostringstream escaped;
    for (const unsigned char ch : value) {
        switch (ch) {
        case '\"': escaped << "\\\""; break;
        case '\\': escaped << "\\\\"; break;
        case '\b': escaped << "\\b"; break;
        case '\f': escaped << "\\f"; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default:
            if (ch < 0x20u) {
                escaped << ' ';
            } else {
                escaped << static_cast<char>(ch);
            }
            break;
        }
    }
    return escaped.str();
}

bool JsonContainsTrue(const std::string &json, const std::string &key) {
    const std::string strictNeedle = "\"" + key + "\":true";
    const std::string spacedNeedle = "\"" + key + "\": true";
    return json.find(strictNeedle) != std::string::npos || json.find(spacedNeedle) != std::string::npos;
}

std::string ExtractJsonStringValue(const std::string &json, const std::string &key) {
    const std::string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return {};
    }

    pos = json.find('\"', pos + needle.size());
    if (pos == std::string::npos) {
        return {};
    }

    ++pos;
    std::string value;
    bool escaped = false;
    for (; pos < json.size(); ++pos) {
        const char ch = json[pos];
        if (escaped) {
            switch (ch) {
            case '\"': value.push_back('\"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            default: value.push_back(ch); break;
            }
            escaped = false;
            continue;
        }

        if (ch == '\\') {
            escaped = true;
            continue;
        }

        if (ch == '\"') {
            break;
        }

        value.push_back(ch);
    }

    return value;
}

bool HttpPostJson(const wchar_t *host, INTERNET_PORT port, const wchar_t *path, const std::string &body, std::string &outResponse) {
    outResponse.clear();

    HINTERNET hSession = WinHttpOpen(L"AIMOD/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        return false;
    }

    WinHttpSetTimeouts(hSession, 1500, 1500, 10000, 15000);

    HINTERNET hConnect = WinHttpConnect(hSession, host, port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"POST",
        path,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0
    );
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    static const wchar_t kHeaders[] = L"Content-Type: application/json\r\n";
    const DWORD bodySize = static_cast<DWORD>(body.size());
    const BOOL sent = WinHttpSendRequest(
        hRequest,
        kHeaders,
        static_cast<DWORD>(-1L),
        bodySize ? const_cast<char *>(body.data()) : WINHTTP_NO_REQUEST_DATA,
        bodySize,
        bodySize,
        0
    );

    bool ok = false;
    if (sent && WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        if (WinHttpQueryHeaders(
                hRequest,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &statusCode,
                &statusCodeSize,
                WINHTTP_NO_HEADER_INDEX) && statusCode >= 200 && statusCode < 300) {
            ok = true;
        }

        while (true) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &available) || available == 0) {
                break;
            }

            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, chunk.data(), available, &read) || read == 0) {
                break;
            }

            chunk.resize(read);
            outResponse += chunk;
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

bool HttpGetText(const wchar_t *host, INTERNET_PORT port, const wchar_t *path, std::string &outResponse) {
    outResponse.clear();

    HINTERNET hSession = WinHttpOpen(L"AIMOD/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        return false;
    }

    WinHttpSetTimeouts(hSession, 1000, 1000, 3000, 3000);
    HINTERNET hConnect = WinHttpConnect(hSession, host, port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        path,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0
    );
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    bool ok = false;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        if (WinHttpQueryHeaders(
                hRequest,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &statusCode,
                &statusCodeSize,
                WINHTTP_NO_HEADER_INDEX) && statusCode >= 200 && statusCode < 300) {
            ok = true;
        }

        while (true) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &available) || available == 0) {
                break;
            }

            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, chunk.data(), available, &read) || read == 0) {
                break;
            }

            chunk.resize(read);
            outResponse += chunk;
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

bool ExecuteSql(sqlite3 *db, const char *sql) {
    char *error = nullptr;
    const int result = sqlite3_exec(db, sql, nullptr, nullptr, &error);
    if (error) {
        sqlite3_free(error);
    }
    return result == SQLITE_OK;
}

bool EnsureAimodSchema(sqlite3 *db) {
    return ExecuteSql(
        db,
        "CREATE TABLE IF NOT EXISTS social_state ("
        "subject_type TEXT NOT NULL,"
        "subject_key TEXT NOT NULL,"
        "trust INTEGER NOT NULL DEFAULT 0,"
        "anger INTEGER NOT NULL DEFAULT 0,"
        "fear INTEGER NOT NULL DEFAULT 0,"
        "respect INTEGER NOT NULL DEFAULT 0,"
        "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "PRIMARY KEY(subject_type, subject_key));"
    );
}

void LoadPersistedGroupMemories(std::unordered_map<std::string, GroupInteractionMemory> &outMap) {
    const std::string dbPath = GetCatalogDbAbsolutePath();
    if (dbPath.empty() || !std::filesystem::exists(dbPath)) {
        return;
    }

    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK || !db) {
        if (db) sqlite3_close(db);
        return;
    }

    sqlite3_busy_timeout(db, 1500);
    if (!EnsureAimodSchema(db)) {
        sqlite3_close(db);
        return;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT subject_key, trust, anger, fear, respect "
        "FROM social_state "
        "WHERE subject_type = 'group'";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *subjectKey = sqlite3_column_text(stmt, 0);
            if (!subjectKey) {
                continue;
            }

            GroupInteractionMemory memory;
            memory.trust = sqlite3_column_int(stmt, 1);
            memory.anger = sqlite3_column_int(stmt, 2);
            memory.fear = sqlite3_column_int(stmt, 3);
            memory.respect = sqlite3_column_int(stmt, 4);
            outMap[reinterpret_cast<const char *>(subjectKey)] = memory;
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

void SavePersistedGroupMemory(const std::string &groupName, const GroupInteractionMemory &memory) {
    if (groupName.empty()) {
        return;
    }

    const std::string dbPath = GetCatalogDbAbsolutePath();
    if (dbPath.empty()) {
        return;
    }

    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || !db) {
        if (db) sqlite3_close(db);
        return;
    }

    sqlite3_busy_timeout(db, 1500);
    if (!EnsureAimodSchema(db)) {
        sqlite3_close(db);
        return;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "INSERT INTO social_state (subject_type, subject_key, trust, anger, fear, respect, updated_at) "
        "VALUES ('group', ?, ?, ?, ?, ?, CURRENT_TIMESTAMP) "
        "ON CONFLICT(subject_type, subject_key) DO UPDATE SET "
        "trust = excluded.trust, "
        "anger = excluded.anger, "
        "fear = excluded.fear, "
        "respect = excluded.respect, "
        "updated_at = CURRENT_TIMESTAMP";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, groupName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, memory.trust);
        sqlite3_bind_int(stmt, 3, memory.anger);
        sqlite3_bind_int(stmt, 4, memory.fear);
        sqlite3_bind_int(stmt, 5, memory.respect);
        sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

bool IsTtsServerAvailable() {
    std::string response;
    return HttpGetText(kTtsServerHost, kTtsServerPort, kTtsHealthPath, response) && JsonContainsTrue(response, "ok");
}

void EnsureTtsServerRunning() {
    if (IsTtsServerAvailable()) {
        return;
    }

    const std::string scriptPath = GetAbsoluteRuntimePath(kTtsStartServerRelativePath);
    if (scriptPath.empty() || !std::filesystem::exists(scriptPath)) {
        return;
    }

    const std::string workingDir = std::filesystem::path(scriptPath).parent_path().string();
    ShellExecuteA(nullptr, "open", scriptPath.c_str(), nullptr, workingDir.c_str(), SW_MINIMIZE);
}

bool IsLlmBridgeAvailable() {
    std::string response;
    return HttpGetText(kLlmBridgeHost, kLlmBridgePort, kLlmBridgeHealthPath, response) && JsonContainsTrue(response, "ok");
}

void EnsureLlmBridgeRunning() {
    if (IsLlmBridgeAvailable()) {
        return;
    }

    const std::string scriptPath = GetAbsoluteRuntimePath(kLlmBridgeStartServerRelativePath);
    if (scriptPath.empty() || !std::filesystem::exists(scriptPath)) {
        return;
    }

    const std::string workingDir = std::filesystem::path(scriptPath).parent_path().string();
    ShellExecuteA(nullptr, "open", scriptPath.c_str(), nullptr, workingDir.c_str(), SW_MINIMIZE);
}

std::string ToLowerAscii(std::string value) {
    for (char &ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

const char *InteractionLabel(InteractionActionId actionId) {
    switch (actionId) {
    case InteractionActionId::Greet: return "saludo";
    case InteractionActionId::Ask: return "pregunta";
    case InteractionActionId::Insult: return "insulto";
    case InteractionActionId::Threaten: return "amenaza";
    case InteractionActionId::Calm: return "calmar";
    case InteractionActionId::Recruit: return "reclutar";
    case InteractionActionId::Dismiss: return "despedir";
    default: return "pregunta";
    }
}

InteractionActionId InferActionFromCustomText(const std::string &text) {
    const std::string lower = ToLowerAscii(text);
    std::array<int, 7> scores {};

    LoadRuntimeVoiceCatalog();
    for (const InteractionKeywordRule &rule : g_runtimeCatalog.interactionKeywordRules) {
        if (rule.keyword.empty()) {
            continue;
        }

        if (lower.find(ToLowerAscii(rule.keyword)) != std::string::npos) {
            scores[static_cast<size_t>(rule.actionId)] += rule.weight;
        }
    }

    if (lower.find('?') != std::string::npos) {
        scores[static_cast<size_t>(InteractionActionId::Ask)] += 2;
    }
    if (lower.find('!') != std::string::npos) {
        scores[static_cast<size_t>(InteractionActionId::Threaten)] += 1;
    }

    int bestScore = -1;
    InteractionActionId bestAction = InteractionActionId::Ask;
    for (size_t i = 0; i < scores.size(); ++i) {
        if (scores[i] > bestScore) {
            bestScore = scores[i];
            bestAction = static_cast<InteractionActionId>(i);
        }
    }

    if (bestScore <= 0) {
        if (lower.find('?') != std::string::npos) {
            return InteractionActionId::Ask;
        }
        if (lower.size() <= 18) {
            return InteractionActionId::Greet;
        }
        return InteractionActionId::Ask;
    }

    return bestAction;
}

char TranslateVirtualKeyToChar(int vk, bool shiftPressed) {
    if (vk >= 'A' && vk <= 'Z') {
        return static_cast<char>(shiftPressed ? vk : (vk + ('a' - 'A')));
    }

    if (vk >= '0' && vk <= '9') {
        if (!shiftPressed) {
            return static_cast<char>(vk);
        }
        switch (vk) {
        case '1': return '!';
        case '2': return '?';
        case '7': return '/';
        default: return static_cast<char>(vk);
        }
    }

    switch (vk) {
    case VK_SPACE: return ' ';
    case VK_OEM_COMMA: return shiftPressed ? ';' : ',';
    case VK_OEM_PERIOD: return shiftPressed ? ':' : '.';
    case VK_OEM_MINUS: return '-';
    case VK_OEM_PLUS: return shiftPressed ? '*' : '+';
    case VK_OEM_1: return shiftPressed ? ':' : ';';
    case VK_OEM_2: return shiftPressed ? '?' : '/';
    case VK_OEM_7: return '\'';
    default: return '\0';
    }
}

std::string PickRuntimeSeedText(CPed *ped, short phraseId, int pedRef, unsigned int now) {
    const unsigned int phraseSeed = phraseId >= 0 ? static_cast<unsigned int>(phraseId) : static_cast<unsigned int>((pedRef * 13) + static_cast<int>(now / 300u));
    const int modelId = ped->m_nModelIndex;
    const std::string groupName = GetResolvedGroupName(modelId, ped->m_pedSpeech.m_nVoiceType);

    LoadRuntimeVoiceCatalog();
    if (!g_runtimeCatalog.loaded) return kDbMissingCatalogText;

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

std::string PickInteractionReply(const std::string &groupName, InteractionActionId actionId, const std::string &reactionKey, unsigned int seed) {
    LoadRuntimeVoiceCatalog();
    if (!g_runtimeCatalog.loaded) return kDbMissingCatalogText;

    const std::string actionKey = ActionKeyName(actionId);
    const auto exactIt = g_runtimeCatalog.interactionReplies.find(MakeReplyLookupKey(groupName, actionKey, reactionKey));
    if (exactIt != g_runtimeCatalog.interactionReplies.end() && !exactIt->second.empty()) {
        return SanitizeBubbleText(exactIt->second[seed % static_cast<unsigned int>(exactIt->second.size())]);
    }

    const auto fallbackIt = g_runtimeCatalog.interactionReplies.find(MakeReplyLookupKey("default", actionKey, reactionKey));
    if (fallbackIt != g_runtimeCatalog.interactionReplies.end() && !fallbackIt->second.empty()) {
        return SanitizeBubbleText(fallbackIt->second[seed % static_cast<unsigned int>(fallbackIt->second.size())]);
    }

    return kDbMissingReplyText;
}

const CBankSlotBankAssignment *FindBankAssignment(short bankSlotId) {
    if (!AEAudioHardware.m_pMP3BankLoader || bankSlotId < 0) return nullptr;
    for (const CBankSlotBankAssignment &assignment : AEAudioHardware.m_pMP3BankLoader->m_aBankSlotBankAssignment) {
        if (assignment.m_nBankSlotId == bankSlotId && assignment.m_nBankId >= 0) {
            return &assignment;
        }
    }
    return nullptr;
}

const CBankSlotInfo *FindBankSlotInfo(short bankSlotId) {
    if (!AEAudioHardware.m_pMP3BankLoader || bankSlotId < 0) return nullptr;
    CAEMP3BankLoader *loader = AEAudioHardware.m_pMP3BankLoader;
    if (!loader->m_pBankSlotsInfos || bankSlotId >= loader->m_nNumBankSlotsInfos) return nullptr;
    const CBankSlotInfo *slotInfo = &loader->m_pBankSlotsInfos[bankSlotId];
    return slotInfo->m_nBankId >= 0 ? slotInfo : nullptr;
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
    if (!ped || ped == player || ped->m_nPedType == PED_TYPE_PLAYER1) return false;
    return DistanceSquared(ped->GetPosition(), player->GetPosition()) <= (kMaxBubbleDistance * kMaxBubbleDistance);
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

void SetupUiFont(float xScale, float yScale, eFontAlignment orientation = ALIGN_LEFT) {
    CFont::SetOrientation(orientation);
    CFont::SetJustify(false);
    CFont::SetBackground(false, false);
    CFont::SetBackgroundColor(CRGBA(0, 0, 0, 0));
    CFont::SetWrapx(RsGlobal.maximumWidth - ScaleX(12.0f));
    CFont::SetCentreSize(0.0f);
    CFont::SetRightJustifyWrap(0.0f);
    CFont::SetScale(ScaleX(xScale), ScaleY(yScale));
    CFont::SetFontStyle(FONT_SUBTITLES);
    CFont::SetProportional(true);
    CFont::SetEdge(0);
    CFont::SetAlphaFade(255.0f);
    CFont::SetDropShadowPosition(1);
    CFont::SetDropColor(CRGBA(0, 0, 0, 230));
}

float EstimateGlyphWidth(char c) {
    if (c == ' ' || c == '\t') return ScaleX(2.7f);
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
    for (char c : text) width += EstimateGlyphWidth(c);
    return width;
}

std::string FitLineToWidth(const std::string &text, float maxWidth, bool ellipsis) {
    std::string out = SanitizeBubbleText(text);
    const std::string suffix = ellipsis ? "..." : "";
    while (!out.empty() && EstimateTextWidth(out + suffix) > maxWidth) {
        out.pop_back();
    }
    if (out.empty()) return suffix.empty() ? "..." : suffix;
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
                while (stream >> word) rest += " " + word;
                lines.push_back(FitLineToWidth(rest, maxWidth, true));
                return lines;
            }
        } else {
            current = candidate;
        }
    }

    if (!current.empty()) lines.push_back(FitLineToWidth(current, maxWidth, false));
    if (lines.empty()) lines.push_back("...");
    return lines;
}

BubbleLayout MeasureBubbleLayout(const std::string &text, float centerX, float anchorY) {
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
    layout.bottom = anchorY - bubbleGapY;
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
    CSprite2d::Draw2DPolygon(centerX - halfBase + ScaleX(1.0f), baseY - ScaleY(0.7f), centerX + halfBase - ScaleX(1.0f), baseY - ScaleY(0.7f), tipX, tipY + ScaleY(1.8f), tipX, tipY + ScaleY(1.8f), fill);
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
    if (!ped) return false;

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

    const CVector pos = ped->GetPosition();
    anchor = { pos.x, pos.y, pos.z + 1.0f };
    return true;
}

CVector GetForwardVector(CPed *ped) {
    if (ped && ped->m_matrix) return ped->GetForward();
    const float heading = ped ? ped->GetHeading() : 0.0f;
    return { std::sin(heading), std::cos(heading), 0.0f };
}

} // namespace

struct Main {
    std::unordered_map<int, PedSpeechState> m_pedSpeechState;
    std::unordered_map<int, BubbleState> m_activeBubbles;
    std::unordered_set<std::string> m_loggedTargetKeys;
    std::unordered_map<int, PedInteractionMemory> m_pedInteractionMemory;
    std::unordered_map<std::string, GroupInteractionMemory> m_groupInteractionMemory;
    InteractionSession m_interactionSession;
    BubbleState m_playerBubble;
    std::array<bool, 256> m_prevKeyStates {};
    std::array<bool, 256> m_currKeyStates {};
    int m_currentTargetPedRef = -1;
    std::string m_currentTargetName;
    std::string m_currentTargetProfile;
    std::mutex m_ttsStatusMutex;
    std::string m_ttsStatusText;
    unsigned int m_ttsStatusUntilTick = 0;
    std::mutex m_ttsQueueMutex;
    std::condition_variable m_ttsQueueCv;
    std::deque<TtsJob> m_ttsQueue;
    std::thread m_ttsWorker;
    std::atomic<bool> m_ttsWorkerStop = false;
    std::mutex m_aiQueueMutex;
    std::condition_variable m_aiQueueCv;
    std::deque<AiJob> m_aiQueue;
    std::mutex m_aiResultMutex;
    std::deque<AiResult> m_aiResults;
    std::unordered_map<int, unsigned int> m_pendingAiByPed;
    std::thread m_aiWorker;
    std::atomic<bool> m_aiWorkerStop = false;
    bool m_runtimeBootstrapped = false;

    Main();
    ~Main();
    void EnsureRuntimeBootstrapped();
    void StartTtsWorkerIfNeeded();
    void StartAiWorkerIfNeeded();
    void PollKeys();
    bool IsKeyJustPressed(int vk) const;
    void SetPedBubble(int pedRef, const std::string &text, short phraseId, unsigned int expiresAt);
    void SetTtsStatus(const std::string &text, unsigned int holdMs);
    void QueueTtsLine(int modelId, const std::string &text, int pedRef = -1);
    void TtsWorkerLoop();
    void QueueAiReply(const AiJob &job);
    void AiWorkerLoop();
    void DrainAiResults(CPlayerPed *player, unsigned int now);
    void DecayInteractionMemory(PedInteractionMemory &memory, unsigned int now);
    void DecayGroupInteractionMemory(GroupInteractionMemory &memory, unsigned int now);
    void AddConversationLine(bool fromPlayer, const std::string &text);
    std::string DetermineReactionKey(int pedRef, int modelId, const std::string &groupName, const InteractionProfile &profile, PedInteractionMemory &memory, InteractionActionId actionId);
    void ApplyGroupAction(const std::string &groupName, InteractionActionId actionId, const std::string &reactionKey, unsigned int now);
    void ApplyPedReaction(CPed *ped, CPlayerPed *player, const std::string &reactionKey, PedInteractionMemory &memory);
    void TriggerNearbySocialRipple(CPed *sourcePed, CPlayerPed *player, const std::string &groupName, const std::string &reactionKey, unsigned int now);
    void TriggerCityWitnesses(CPed *sourcePed, CPlayerPed *player, const std::string &groupName, const std::string &reactionKey, unsigned int now);
    int FindBestInteractionTarget(CPlayerPed *player, std::string &outName, std::string &outProfile);
    void ExecuteInteraction(CPlayerPed *player, CPed *ped, const InteractionActionConfig &action, unsigned int now);
    void ExecuteCustomInteraction(CPlayerPed *player, CPed *ped, const std::string &customText, unsigned int now);
    void UpdateInteraction(CPlayerPed *player, unsigned int now);
    void LogTargetPhrase(CPed *ped, short phraseId);
    void OnGameProcess();
    void DrawInteractionUi();
    void OnDrawing();
} gInstance;

Main::Main() {
    Events::gameProcessEvent += [this] { OnGameProcess(); };
    Events::drawingEvent += [this] { OnDrawing(); };
}

Main::~Main() {
    m_ttsWorkerStop = true;
    m_ttsQueueCv.notify_all();
    if (m_ttsWorker.joinable()) {
        m_ttsWorker.join();
    }

    m_aiWorkerStop = true;
    m_aiQueueCv.notify_all();
    if (m_aiWorker.joinable()) {
        m_aiWorker.join();
    }
}

void Main::StartTtsWorkerIfNeeded() {
    if (m_ttsWorker.joinable()) {
        return;
    }

    m_ttsWorkerStop = false;
    m_ttsWorker = std::thread([this]() { TtsWorkerLoop(); });
}

void Main::StartAiWorkerIfNeeded() {
    if (m_aiWorker.joinable()) {
        return;
    }

    m_aiWorkerStop = false;
    m_aiWorker = std::thread([this]() { AiWorkerLoop(); });
}

void Main::EnsureRuntimeBootstrapped() {
    if (m_runtimeBootstrapped) {
        return;
    }

    LoadRuntimeVoiceCatalog();
    LoadPersistedGroupMemories(m_groupInteractionMemory);
    StartTtsWorkerIfNeeded();
    StartAiWorkerIfNeeded();
    m_runtimeBootstrapped = true;

    SetTtsStatus(g_runtimeCatalog.loaded ? "AIMOD listo" : "AIMOD sin catalogo", 1800);
}

void Main::PollKeys() {
    for (int vk = 0; vk < 256; ++vk) {
        m_prevKeyStates[vk] = m_currKeyStates[vk];
        m_currKeyStates[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
}

bool Main::IsKeyJustPressed(int vk) const {
    return m_currKeyStates[vk] && !m_prevKeyStates[vk];
}

void Main::SetPedBubble(int pedRef, const std::string &text, short phraseId, unsigned int expiresAt) {
    BubbleState &bubble = m_activeBubbles[pedRef];
    bubble.text = SanitizeBubbleText(text);
    bubble.phraseId = phraseId;
    bubble.expiresAt = expiresAt;
}

void Main::SetTtsStatus(const std::string &text, unsigned int holdMs) {
    std::lock_guard<std::mutex> lock(m_ttsStatusMutex);
    m_ttsStatusText = text;
    m_ttsStatusUntilTick = GetTickCount() + holdMs;
}

void Main::QueueTtsLine(int modelId, const std::string &text, int pedRef) {
    const TtsAssignment *assignment = GetTtsAssignmentForModel(modelId);
    if (!assignment || assignment->voiceId.empty()) {
        return;
    }

    const std::string safeText = SanitizeBubbleText(text);
    if (safeText.empty()) {
        return;
    }

    StartTtsWorkerIfNeeded();

    {
        std::lock_guard<std::mutex> lock(m_ttsQueueMutex);
        TtsJob job;
        job.modelId = modelId;
        job.pedRef = pedRef;
        job.text = safeText;
        job.voiceId = PickVoiceIdForInstance(modelId, pedRef, *assignment);
        const PedInstanceIdentity identity = DescribePedInstance(pedRef, modelId, assignment->groupName);

        const int jitterSeed = std::abs((modelId * 19) + ((pedRef == -1 ? modelId : pedRef) * 23));
        const float pitchJitter = ((jitterSeed % 9) - 4) * 0.012f;
        const float speedJitter = ((jitterSeed % 7) - 3) * 0.014f;
        job.pitch = ClampPitch(assignment->pitch + pitchJitter + identity.pitchBias);
        job.speed = ClampSpeed(assignment->speed + speedJitter + identity.speedBias);

        m_ttsQueue.push_back(job);
    }
    m_ttsQueueCv.notify_one();
}

void Main::TtsWorkerLoop() {
    while (!m_ttsWorkerStop) {
        TtsJob job;
        {
            std::unique_lock<std::mutex> lock(m_ttsQueueMutex);
            m_ttsQueueCv.wait(lock, [this]() { return m_ttsWorkerStop || !m_ttsQueue.empty(); });
            if (m_ttsWorkerStop) {
                return;
            }
            job = m_ttsQueue.front();
            m_ttsQueue.pop_front();
        }

        SetTtsStatus("TTS generando voz...", kTtsStatusLifetimeMs);
        const bool useExplicitVoice = !job.voiceId.empty();
        const std::wstring requestPath = useExplicitVoice ? kTtsSynthesizeVoicePath : kTtsSynthesizePath;
        const std::string body = useExplicitVoice
            ? (std::string("{\"voice_id\":\"") + JsonEscape(job.voiceId) +
               "\",\"text\":\"" + JsonEscape(job.text) +
               "\",\"pitch\":" + std::to_string(job.pitch) +
               ",\"speed\":" + std::to_string(job.speed) + "}")
            : (std::string("{\"model_id\":") + std::to_string(job.modelId) +
               ",\"text\":\"" + JsonEscape(job.text) + "\"}");

        std::string response;
        bool requestOk = HttpPostJson(kTtsServerHost, kTtsServerPort, requestPath.c_str(), body, response);
        if (!requestOk || !JsonContainsTrue(response, "ok")) {
            EnsureTtsServerRunning();
            Sleep(1500);
            response.clear();
            requestOk = HttpPostJson(kTtsServerHost, kTtsServerPort, requestPath.c_str(), body, response);
        }
        if (!requestOk || !JsonContainsTrue(response, "ok")) {
            SetTtsStatus("TTS offline o con error", kTtsStatusLifetimeMs);
            continue;
        }

        const std::string wavPath = ExtractJsonStringValue(response, "wav_path");
        const std::string voiceId = ExtractJsonStringValue(response, "voice_id");
        if (wavPath.empty()) {
            SetTtsStatus("TTS sin audio generado", kTtsStatusLifetimeMs);
            continue;
        }

        if (!PlaySoundA(wavPath.c_str(), nullptr, SND_FILENAME | SND_NODEFAULT | SND_SYNC)) {
            SetTtsStatus("TTS no pudo reproducir wav", kTtsStatusLifetimeMs);
            continue;
        }

        SetTtsStatus(voiceId.empty() ? "TTS listo" : ("TTS " + voiceId), 2200);
    }
}

void Main::QueueAiReply(const AiJob &job) {
    if (job.pedRef == -1 || job.playerText.empty()) {
        return;
    }

    StartAiWorkerIfNeeded();

    {
        std::lock_guard<std::mutex> lock(m_aiQueueMutex);
        m_pendingAiByPed[job.pedRef] = job.submittedAt;
        m_aiQueue.push_back(job);
    }
    m_aiQueueCv.notify_one();
}

void Main::AiWorkerLoop() {
    while (!m_aiWorkerStop) {
        AiJob job;
        {
            std::unique_lock<std::mutex> lock(m_aiQueueMutex);
            m_aiQueueCv.wait(lock, [this]() { return m_aiWorkerStop || !m_aiQueue.empty(); });
            if (m_aiWorkerStop) {
                return;
            }
            job = m_aiQueue.front();
            m_aiQueue.pop_front();
        }

        AiResult result;
        result.pedRef = job.pedRef;
        result.modelId = job.modelId;
        result.groupName = job.groupName;
        result.actionId = job.actionId;
        result.submittedAt = job.submittedAt;
        result.reactionKey = job.fallbackReactionKey;

        SetTtsStatus("AIMOD pensando...", kTtsStatusLifetimeMs);

        const std::string body =
            std::string("{\"model_id\":") + std::to_string(job.modelId) +
            ",\"npc_name\":\"" + JsonEscape(job.npcName) +
            "\",\"group_name\":\"" + JsonEscape(job.groupName) +
            "\",\"profile_name\":\"" + JsonEscape(job.profileName) +
            "\",\"player_text\":\"" + JsonEscape(job.playerText) +
            "\",\"expected_reaction\":\"" + JsonEscape(job.fallbackReactionKey) + "\"}";

        std::string response;
        bool requestOk = HttpPostJson(kLlmBridgeHost, kLlmBridgePort, kLlmBridgeChatPath, body, response);
        if (!requestOk || !JsonContainsTrue(response, "ok")) {
            EnsureLlmBridgeRunning();
            Sleep(1200);
            response.clear();
            requestOk = HttpPostJson(kLlmBridgeHost, kLlmBridgePort, kLlmBridgeChatPath, body, response);
        }

        if (requestOk && JsonContainsTrue(response, "ok")) {
            const std::string reaction = ExtractJsonStringValue(response, "reaction");
            result.reactionKey = CanonicalizeReactionKey(job.groupName, job.actionId, job.fallbackReactionKey, reaction);
            result.usedBridge = !reaction.empty() && result.reactionKey != job.fallbackReactionKey;
        }

        result.replyText = StylizeReplyForPed(
            job.modelId,
            PickInteractionReply(
                job.groupName,
                job.actionId,
                result.reactionKey,
                static_cast<unsigned int>(job.pedRef + job.submittedAt)
            ),
            job.actionId,
            result.reactionKey
        );

        if (result.replyText.empty()) {
            result.replyText = kDbMissingReplyText;
        }

        {
            std::lock_guard<std::mutex> lock(m_aiResultMutex);
            m_aiResults.push_back(result);
        }
    }
}

void Main::DrainAiResults(CPlayerPed *player, unsigned int now) {
    std::deque<AiResult> ready;
    {
        std::lock_guard<std::mutex> lock(m_aiResultMutex);
        ready.swap(m_aiResults);
    }

    for (const AiResult &result : ready) {
        bool stale = false;
        {
            std::lock_guard<std::mutex> lock(m_aiQueueMutex);
            const auto pendingIt = m_pendingAiByPed.find(result.pedRef);
            if (pendingIt == m_pendingAiByPed.end() || pendingIt->second != result.submittedAt) {
                stale = true;
            } else {
                m_pendingAiByPed.erase(pendingIt);
            }
        }

        if (stale || !player || !IsPedHandleValid(result.pedRef)) {
            continue;
        }

        CPed *ped = CPools::GetPed(result.pedRef);
        if (!ped || !ped->IsAlive()) {
            continue;
        }

        PedInteractionMemory &memory = m_pedInteractionMemory[result.pedRef];
        const std::string finalReply = ComposePedReplyText(
            result.pedRef,
            ped->m_nModelIndex,
            result.groupName,
            result.actionId,
            result.reactionKey,
            static_cast<unsigned int>(result.pedRef + result.submittedAt + static_cast<unsigned int>(result.actionId) * 17u),
            &memory
        );
        SetPedBubble(result.pedRef, finalReply, static_cast<short>(-850 - static_cast<int>(result.actionId)), now + kInteractionBubbleLifetimeMs);
        QueueTtsLine(ped->m_nModelIndex, finalReply, result.pedRef);
        AddConversationLine(false, finalReply);

        ApplyPedReaction(ped, player, result.reactionKey, memory);
        ApplyGroupAction(result.groupName, result.actionId, result.reactionKey, now);
        TriggerNearbySocialRipple(ped, player, result.groupName, result.reactionKey, now);
        TriggerCityWitnesses(ped, player, result.groupName, result.reactionKey, now);

        SetTtsStatus(result.usedBridge ? "AIMOD bridge listo" : "AIMOD fallback listo", 1800);
    }
}

void Main::DecayInteractionMemory(PedInteractionMemory &memory, unsigned int now) {
    if (memory.lastInteractionAt == 0 || now <= memory.lastInteractionAt) {
        return;
    }

    const unsigned int elapsed = now - memory.lastInteractionAt;
    const int decaySteps = static_cast<int>(elapsed / 15000u);
    if (decaySteps <= 0) {
        return;
    }

    memory.anger = std::max(0, memory.anger - decaySteps);
    memory.fear = std::max(0, memory.fear - decaySteps);
    memory.trust = std::max(0, memory.trust - (decaySteps / 2));
    memory.respect = std::max(0, memory.respect - (decaySteps / 3));
    memory.rapport = std::max(0, memory.rapport - (decaySteps / 2));
    memory.suspicion = std::max(0, memory.suspicion - (decaySteps / 2));
    memory.lastInteractionAt = now;
}

void Main::DecayGroupInteractionMemory(GroupInteractionMemory &memory, unsigned int now) {
    if (memory.lastInteractionAt == 0 || now <= memory.lastInteractionAt) {
        return;
    }

    const unsigned int elapsed = now - memory.lastInteractionAt;
    const int decaySteps = static_cast<int>(elapsed / 20000u);
    if (decaySteps <= 0) {
        return;
    }

    memory.anger = std::max(0, memory.anger - decaySteps);
    memory.fear = std::max(0, memory.fear - decaySteps);
    memory.trust = std::max(0, memory.trust - (decaySteps / 2));
    memory.respect = std::max(0, memory.respect - (decaySteps / 2));
    memory.lastInteractionAt = now;
}

void Main::AddConversationLine(bool fromPlayer, const std::string &text) {
    const std::string safeText = SanitizeBubbleText(text);
    if (safeText.empty()) {
        return;
    }

    m_interactionSession.history.push_back({ fromPlayer, safeText });
    while (static_cast<int>(m_interactionSession.history.size()) > kMaxConversationHistoryLines) {
        m_interactionSession.history.pop_front();
    }
}

std::string Main::DetermineReactionKey(int pedRef, int modelId, const std::string &groupName, const InteractionProfile &baseProfile, PedInteractionMemory &memory, InteractionActionId actionId) {
    const TunedInteractionProfile tuned = TuneInteractionProfileForPed(modelId, baseProfile);
    InteractionProfile profile = tuned.profile;
    const PedInstanceIdentity identity = DescribePedInstance(pedRef, modelId, groupName);
    profile.aggression = std::clamp(profile.aggression + identity.aggressionBias, 0, 7);
    profile.warmth = std::clamp(profile.warmth + identity.warmthBias, 0, 7);
    profile.bravery = std::clamp(profile.bravery + identity.braveryBias, 0, 7);
    GroupInteractionMemory &groupMemory = m_groupInteractionMemory[groupName];
    const int sharedPressure = groupMemory.anger + (groupMemory.fear / 2) - (groupMemory.trust / 2) - (groupMemory.respect / 3) + tuned.volatility;
    const int personalPressure = memory.anger + memory.suspicion + identity.suspicionBias + (memory.fear / 2) - memory.rapport - (memory.respect / 2);
    memory.encounters += 1;

    switch (actionId) {
    case InteractionActionId::Greet:
        memory.trust += 1 + (profile.warmth >= 4 ? 1 : 0);
        memory.rapport += 1 + (profile.sociability >= 4 ? 1 : 0);
        memory.anger = std::max(0, memory.anger - 1);
        memory.suspicion = std::max(0, memory.suspicion - 1);
        if (groupName == "police") return (personalPressure + sharedPressure) > 2 ? "warn" : "neutral";
        if (groupName == "ballas" || groupName == "gang") return (memory.respect + memory.rapport + profile.loyalty - sharedPressure >= 4) ? "neutral" : "dismiss";
        return (profile.warmth + memory.trust + memory.rapport >= 5) ? "friendly" : "neutral";

    case InteractionActionId::Ask:
        memory.respect += 1 + (profile.authority >= 4 ? 1 : 0);
        memory.rapport += profile.sociability >= 4 ? 1 : 0;
        if (groupName == "police") return "warn";
        if (groupName == "ballas" || groupName == "gang") return (memory.trust + memory.respect + memory.rapport - sharedPressure >= 5) ? "neutral" : "dismiss";
        return (profile.sociability + memory.trust + memory.rapport >= 5) ? "friendly" : "neutral";

    case InteractionActionId::Insult:
        memory.anger += 2 + (profile.aggression >= 4 ? 1 : 0) + tuned.volatility;
        memory.respect -= 1;
        memory.suspicion += 2;
        memory.rapport = std::max(0, memory.rapport - 1);
        if (groupName == "police") return (profile.authority + personalPressure + sharedPressure >= 6) ? "attack" : "warn";
        if (groupName == "ballas" || groupName == "gang") return (profile.aggression + personalPressure + sharedPressure >= 6) ? "attack" : "warn";
        return memory.anger >= 4 ? "attack" : "dismiss";

    case InteractionActionId::Threaten:
        memory.fear += 1 + (profile.bravery <= 2 ? 1 : 0);
        memory.suspicion += 2;
        if (groupName == "police") {
            memory.anger += 2;
            return "attack";
        }
        if (profile.bravery + profile.aggression + personalPressure + sharedPressure >= 7) {
            memory.anger += 2;
            return "attack";
        }
        return (profile.bravery <= 2 && profile.authority == 0 && sharedPressure < 2) ? "flee" : "warn";

    case InteractionActionId::Calm:
        memory.anger = std::max(0, memory.anger - 2);
        memory.fear = std::max(0, memory.fear - 1);
        memory.suspicion = std::max(0, memory.suspicion - 1);
        memory.trust += 1 + (profile.warmth >= 4 ? 1 : 0);
        memory.rapport += 1;
        if (groupName == "police") return (personalPressure + sharedPressure) > 2 ? "warn" : "neutral";
        if ((memory.anger + sharedPressure) >= 4 && profile.aggression >= 4) return "warn";
        return (memory.trust + memory.rapport + profile.warmth + groupMemory.trust >= 5) ? "friendly" : "neutral";

    case InteractionActionId::Recruit:
        if (groupName == "police") return "refuse";
        if (groupName == "ballas" || groupName == "gang") {
            return (memory.trust + memory.respect + memory.rapport + profile.loyalty + groupMemory.respect - sharedPressure >= 8) ? "follow" : "refuse";
        }
        return (memory.trust + memory.respect + memory.rapport + profile.loyalty + profile.warmth + groupMemory.trust >= 7) ? "follow" : "refuse";

    case InteractionActionId::Dismiss:
        memory.followingPlayer = false;
        memory.rapport = std::max(0, memory.rapport - 1);
        return groupName == "police" ? "warn" : "dismiss";
    }

    return "neutral";
}

void Main::ApplyGroupAction(const std::string &groupName, InteractionActionId actionId, const std::string &reactionKey, unsigned int now) {
    GroupInteractionMemory &memory = m_groupInteractionMemory[groupName];
    DecayGroupInteractionMemory(memory, now);
    memory.lastInteractionAt = now;

    switch (actionId) {
    case InteractionActionId::Greet:
        memory.trust += 1;
        memory.anger = std::max(0, memory.anger - 1);
        break;
    case InteractionActionId::Ask:
        memory.respect += 1;
        break;
    case InteractionActionId::Insult:
        memory.anger += 2;
        break;
    case InteractionActionId::Threaten:
        memory.anger += 2;
        memory.fear += 1;
        break;
    case InteractionActionId::Calm:
        memory.anger = std::max(0, memory.anger - 2);
        memory.trust += 1;
        break;
    case InteractionActionId::Recruit:
        memory.respect += 1;
        break;
    case InteractionActionId::Dismiss:
        memory.trust = std::max(0, memory.trust - 1);
        break;
    }

    if (reactionKey == "attack") {
        memory.anger += 2;
    } else if (reactionKey == "friendly" || reactionKey == "follow") {
        memory.trust += 2;
        memory.respect += 1;
    } else if (reactionKey == "flee") {
        memory.fear += 2;
    } else if (reactionKey == "warn") {
        memory.anger += 1;
    }

    SavePersistedGroupMemory(groupName, memory);
}

void Main::ApplyPedReaction(CPed *ped, CPlayerPed *player, const std::string &reactionKey, PedInteractionMemory &memory) {
    if (!ped || !player) {
        return;
    }

    ped->ClearAimFlag();
    ped->ClearLookFlag();
    ped->SetLookFlag(player, true, false);
    ped->SetLookTimer(1800);

    if (ped->m_pVehicle) {
        return;
    }

    if (reactionKey == "attack") {
        Command<Commands::CLEAR_CHAR_TASKS_IMMEDIATELY>(ped);
        Command<Commands::TASK_KILL_CHAR_ON_FOOT>(ped, player);
        ped->SetAimFlag(player);
        ped->SetMoveState(PEDMOVE_RUN);
        memory.followingPlayer = false;
        return;
    }

    if (reactionKey == "flee") {
        Command<Commands::CLEAR_CHAR_TASKS_IMMEDIATELY>(ped);
        Command<Commands::TASK_SMART_FLEE_CHAR>(ped, player, 100.0f, 12000);
        ped->SetMoveState(PEDMOVE_SPRINT);
        memory.followingPlayer = false;
        return;
    }

    if (reactionKey == "follow") {
        Command<Commands::CLEAR_CHAR_TASKS_IMMEDIATELY>(ped);
        Command<Commands::SET_CHAR_OBJ_FOLLOW_CHAR_IN_FORMATION>(ped, player);
        ped->SetMoveState(PEDMOVE_RUN);
        memory.followingPlayer = true;
        return;
    }

    if (reactionKey == "dismiss") {
        Command<Commands::CLEAR_CHAR_TASKS_IMMEDIATELY>(ped);
        ped->SetMoveState(PEDMOVE_WALK);
        memory.followingPlayer = false;
    }
}

void Main::TriggerNearbySocialRipple(CPed *sourcePed, CPlayerPed *player, const std::string &groupName, const std::string &reactionKey, unsigned int now) {
    if (!sourcePed || !player || !CPools::ms_pPedPool) {
        return;
    }

    if (reactionKey != "attack" && reactionKey != "warn" && reactionKey != "flee") {
        return;
    }

    const int sourceRef = CPools::GetPedRef(sourcePed);
    const CVector sourcePos = sourcePed->GetPosition();
    const float maxDistance = groupName == "police" ? 22.0f : 18.0f;
    const float maxDistanceSq = maxDistance * maxDistance;
    int affected = 0;

    for (int i = 0; i < CPools::ms_pPedPool->m_nSize && affected < 3; ++i) {
        CPed *ped = CPools::ms_pPedPool->GetAt(i);
        if (!ped || !ped->IsAlive() || !ped->IsPedInControl() || ped == sourcePed || ped == player) {
            continue;
        }

        const int pedRef = CPools::GetPedRef(ped);
        if (pedRef == sourceRef || ped->m_pVehicle) {
            continue;
        }

        if (DistanceSquared(ped->GetPosition(), sourcePos) > maxDistanceSq) {
            continue;
        }

        const std::string pedGroup = GetResolvedGroupName(ped->m_nModelIndex, ped->m_pedSpeech.m_nVoiceType);
        if (pedGroup != groupName) {
            continue;
        }

        PedInteractionMemory &memory = m_pedInteractionMemory[pedRef];
        DecayInteractionMemory(memory, now);

        const TunedInteractionProfile tuned = TuneInteractionProfileForPed(ped->m_nModelIndex, GetInteractionProfileForGroup(groupName));
        std::string rippleReaction;
        if (reactionKey == "attack") {
            if (groupName == "police") {
                rippleReaction = "attack";
            } else if (groupName == "ballas" || groupName == "gang") {
                rippleReaction = (tuned.profile.loyalty + tuned.profile.aggression + tuned.profile.bravery >= 9) ? "attack" : "warn";
            } else {
                rippleReaction = tuned.profile.bravery <= 2 ? "flee" : "warn";
            }
        } else if (reactionKey == "warn") {
            if (groupName == "police") {
                rippleReaction = "warn";
            } else if (groupName == "ballas" || groupName == "gang") {
                rippleReaction = tuned.profile.loyalty + tuned.profile.aggression >= 7 ? "warn" : "dismiss";
            } else {
                rippleReaction = tuned.profile.bravery <= 2 ? "flee" : "dismiss";
            }
        } else if (reactionKey == "flee") {
            if (groupName == "police") {
                rippleReaction = "attack";
            } else if (groupName == "ballas" || groupName == "gang") {
                rippleReaction = "warn";
            } else {
                rippleReaction = "flee";
            }
        }

        if (rippleReaction.empty()) {
            continue;
        }

        memory.lastInteractionAt = now;
        if (rippleReaction == "attack") {
            memory.anger += 2;
        } else if (rippleReaction == "warn") {
            memory.anger += 1;
        } else if (rippleReaction == "flee") {
            memory.fear += 2;
        } else {
            memory.followingPlayer = false;
        }

        ApplyPedReaction(ped, player, rippleReaction, memory);

        const InteractionActionId bubbleAction = rippleReaction == "flee" ? InteractionActionId::Threaten : InteractionActionId::Insult;
        const std::string bubbleText = ComposePedReplyText(
            pedRef,
            ped->m_nModelIndex,
            groupName,
            bubbleAction,
            rippleReaction,
            static_cast<unsigned int>(pedRef + now + affected),
            &memory
        );
        SetPedBubble(pedRef, bubbleText, static_cast<short>(-950 - affected), now + 2600);
        ++affected;
    }
}

void Main::TriggerCityWitnesses(CPed *sourcePed, CPlayerPed *player, const std::string &groupName, const std::string &reactionKey, unsigned int now) {
    if (!sourcePed || !player || !CPools::ms_pPedPool) {
        return;
    }

    if (reactionKey != "attack" && reactionKey != "warn") {
        return;
    }

    const CVector sourcePos = sourcePed->GetPosition();
    const float maxDistanceSq = 28.0f * 28.0f;
    int affected = 0;

    for (int i = 0; i < CPools::ms_pPedPool->m_nSize && affected < 4; ++i) {
        CPed *ped = CPools::ms_pPedPool->GetAt(i);
        if (!ped || !ped->IsAlive() || !ped->IsPedInControl() || ped == sourcePed || ped == player || ped->m_pVehicle) {
            continue;
        }

        if (DistanceSquared(ped->GetPosition(), sourcePos) > maxDistanceSq) {
            continue;
        }

        const int pedRef = CPools::GetPedRef(ped);
        const std::string witnessGroup = GetResolvedGroupName(ped->m_nModelIndex, ped->m_pedSpeech.m_nVoiceType);
        if (witnessGroup == groupName) {
            continue;
        }

        std::string witnessReaction;
        InteractionActionId bubbleAction = InteractionActionId::Threaten;

        if (witnessGroup == "police") {
            if (groupName == "police") {
                continue;
            }
            witnessReaction = reactionKey == "attack" ? "attack" : "warn";
            bubbleAction = InteractionActionId::Insult;
        } else if (witnessGroup == "emergency" || witnessGroup == "gfd") {
            witnessReaction = "flee";
            bubbleAction = InteractionActionId::Threaten;
        } else if (witnessGroup == "ambient" || witnessGroup == "special") {
            witnessReaction = reactionKey == "attack" ? "flee" : "dismiss";
            bubbleAction = InteractionActionId::Threaten;
        } else if ((witnessGroup == "ballas" || witnessGroup == "gang") && groupName == "police") {
            witnessReaction = "dismiss";
            bubbleAction = InteractionActionId::Dismiss;
        } else {
            continue;
        }

        PedInteractionMemory &memory = m_pedInteractionMemory[pedRef];
        DecayInteractionMemory(memory, now);
        memory.lastInteractionAt = now;
        if (witnessReaction == "attack" || witnessReaction == "warn") {
            memory.anger += 1;
        }
        if (witnessReaction == "flee") {
            memory.fear += 2;
        }

        ApplyPedReaction(ped, player, witnessReaction, memory);
        const std::string bubbleText = ComposePedReplyText(
            pedRef,
            ped->m_nModelIndex,
            witnessGroup,
            bubbleAction,
            witnessReaction,
            static_cast<unsigned int>(pedRef + now + affected + 50),
            &memory
        );
        SetPedBubble(pedRef, bubbleText, static_cast<short>(-980 - affected), now + 2400);
        ++affected;
    }
}

int Main::FindBestInteractionTarget(CPlayerPed *player, std::string &outName, std::string &outProfile) {
    if (!CPools::ms_pPedPool || !player) {
        return -1;
    }

    const float maxDistanceSq = kMaxInteractionDistance * kMaxInteractionDistance;
    const float centerX = RsGlobal.maximumWidth * 0.5f;
    const float centerY = RsGlobal.maximumHeight * 0.43f;
    const float maxScreenRadiusSq = ScaleX(135.0f) * ScaleX(135.0f);
    const CVector playerPos = player->GetPosition();
    const CVector playerForward = GetForwardVector(player);

    float bestScore = 1.0e20f;
    int bestRef = -1;

    for (int i = 0; i < CPools::ms_pPedPool->m_nSize; ++i) {
        CPed *ped = CPools::ms_pPedPool->GetAt(i);
        if (!ShouldTrackPed(ped, player) || !ped->IsAlive() || !ped->IsPedInControl()) {
            continue;
        }

        if (ped->m_pVehicle) {
            continue;
        }

        const CVector toPed = ped->GetPosition() - playerPos;
        const float distanceSq = toPed.x * toPed.x + toPed.y * toPed.y + toPed.z * toPed.z;
        if (distanceSq > maxDistanceSq) {
            continue;
        }

        const float planarLength = std::sqrt(std::max(0.0001f, toPed.x * toPed.x + toPed.y * toPed.y));
        const float forwardLength = std::sqrt(std::max(0.0001f, playerForward.x * playerForward.x + playerForward.y * playerForward.y));
        const float dot = ((playerForward.x * toPed.x) + (playerForward.y * toPed.y)) / (planarLength * forwardLength);
        if (dot < 0.1f) {
            continue;
        }

        RwV3d anchor {};
        if (!GetBubbleAnchor(ped, anchor)) {
            continue;
        }

        RwV3d screen {};
        float scaleW = 0.0f;
        float scaleH = 0.0f;
        if (!CSprite::CalcScreenCoors(anchor, &screen, &scaleW, &scaleH, true, true)) {
            continue;
        }

        const float dx = screen.x - centerX;
        const float dy = screen.y - centerY;
        const float screenRadiusSq = dx * dx + dy * dy;
        if (screenRadiusSq > maxScreenRadiusSq) {
            continue;
        }

        const float score = screenRadiusSq + distanceSq * 180.0f - dot * 300.0f;
        if (score < bestScore) {
            bestScore = score;
            bestRef = CPools::GetPedRef(ped);
            const std::string groupName = GetResolvedGroupName(ped->m_nModelIndex, ped->m_pedSpeech.m_nVoiceType);
            const PedInstanceIdentity identity = DescribePedInstance(bestRef, ped->m_nModelIndex, groupName);
            const std::string alias = identity.alias;
            outName = GetCatalogModelName(ped->m_nModelIndex) + " \"" + alias + "\"";
            const std::string personaTitle = GetPedPersonaTitle(ped->m_nModelIndex);
            const std::string baseProfile = !personaTitle.empty()
                ? personaTitle
                : GetInteractionProfileForGroup(groupName).profileName;
            outProfile = identity.moodTag.empty() ? baseProfile : (baseProfile + " / " + identity.moodTag);
        }
    }

    return bestRef;
}

void Main::ExecuteInteraction(CPlayerPed *player, CPed *ped, const InteractionActionConfig &action, unsigned int now) {
    if (!player || !ped) {
        return;
    }

    const int pedRef = CPools::GetPedRef(ped);
    PedInteractionMemory &memory = m_pedInteractionMemory[pedRef];
    DecayInteractionMemory(memory, now);

    const std::string groupName = GetResolvedGroupName(ped->m_nModelIndex, ped->m_pedSpeech.m_nVoiceType);
    const InteractionProfile profile = GetInteractionProfileForGroup(groupName);
    const std::string reactionKey = DetermineReactionKey(pedRef, ped->m_nModelIndex, groupName, profile, memory, action.id);
    memory.lastInteractionAt = now;

    const std::string reply = ComposePedReplyText(
        pedRef,
        ped->m_nModelIndex,
        groupName,
        action.id,
        reactionKey,
        static_cast<unsigned int>(pedRef + now),
        &memory
    );
    SetPedBubble(pedRef, reply, static_cast<short>(-200 - action.displayOrder), now + kInteractionBubbleLifetimeMs);
    m_playerBubble.text = SanitizeBubbleText(action.playerText);
    m_playerBubble.phraseId = static_cast<short>(-500 - action.displayOrder);
    m_playerBubble.expiresAt = now + kPlayerBubbleLifetimeMs;
    ApplyPedReaction(ped, player, reactionKey, memory);
    ApplyGroupAction(groupName, action.id, reactionKey, now);
    TriggerNearbySocialRipple(ped, player, groupName, reactionKey, now);
    TriggerCityWitnesses(ped, player, groupName, reactionKey, now);
    QueueTtsLine(kPlayerTtsModelId, action.playerText);
    QueueTtsLine(ped->m_nModelIndex, reply, pedRef);
    AddConversationLine(true, action.playerText);
    AddConversationLine(false, reply);

    m_interactionSession.playerText = action.playerText;
    m_interactionSession.playerTextExpiresAt = now + kInteractionStatusLifetimeMs;
}

void Main::ExecuteCustomInteraction(CPlayerPed *player, CPed *ped, const std::string &customText, unsigned int now) {
    if (!player || !ped) {
        return;
    }

    const std::string safeText = SanitizeBubbleText(customText);
    if (safeText.empty()) {
        return;
    }

    const InteractionActionId inferredAction = InferActionFromCustomText(safeText);
    const int pedRef = CPools::GetPedRef(ped);
    PedInteractionMemory &memory = m_pedInteractionMemory[pedRef];
    DecayInteractionMemory(memory, now);

    const std::string groupName = GetResolvedGroupName(ped->m_nModelIndex, ped->m_pedSpeech.m_nVoiceType);
    const InteractionProfile profile = GetInteractionProfileForGroup(groupName);
    const std::string personaTitle = GetPedPersonaTitle(ped->m_nModelIndex);
    const PedInstanceIdentity identity = DescribePedInstance(pedRef, ped->m_nModelIndex, groupName);
    const std::string reactionKey = DetermineReactionKey(pedRef, ped->m_nModelIndex, groupName, profile, memory, inferredAction);
    memory.lastInteractionAt = now;

    SetPedBubble(pedRef, kAiPendingText, static_cast<short>(-350 - static_cast<int>(inferredAction)), now + kInteractionBubbleLifetimeMs);
    m_playerBubble.text = SanitizeBubbleText(safeText);
    m_playerBubble.phraseId = static_cast<short>(-650 - static_cast<int>(inferredAction));
    m_playerBubble.expiresAt = now + kPlayerBubbleLifetimeMs;
    QueueTtsLine(kPlayerTtsModelId, safeText);
    AddConversationLine(true, safeText);

    QueueAiReply({
        pedRef,
        ped->m_nModelIndex,
        GetCatalogModelName(ped->m_nModelIndex),
        groupName,
        identity.moodTag.empty()
            ? (!personaTitle.empty() ? personaTitle : profile.profileName)
            : ((!personaTitle.empty() ? personaTitle : profile.profileName) + " / " + identity.moodTag),
        safeText,
        inferredAction,
        reactionKey,
        now
    });

    m_interactionSession.playerText = safeText;
    m_interactionSession.playerTextExpiresAt = now + kInteractionStatusLifetimeMs;
    m_interactionSession.inputIntentLabel = InteractionLabel(inferredAction);
    SetTtsStatus("AIMOD pensando...", kTtsStatusLifetimeMs);
}

void Main::UpdateInteraction(CPlayerPed *player, unsigned int now) {
    PollKeys();

    if (IsKeyJustPressed(VK_F5)) {
        ResetRuntimeVoiceCatalog();
        LoadRuntimeVoiceCatalog();
        m_groupInteractionMemory.clear();
        LoadPersistedGroupMemories(m_groupInteractionMemory);
        SetTtsStatus("AIMOD recargo la BD", 1800);
    }

    if (IsKeyJustPressed(VK_F6)) {
        EnsureTtsServerRunning();
        SetTtsStatus("AIMOD verifico TTS", 1800);
    }

    if (IsKeyJustPressed(VK_F7)) {
        EnsureLlmBridgeRunning();
        SetTtsStatus("AIMOD verifico bridge IA", 1800);
    }

    std::string targetName;
    std::string targetProfile;
    m_currentTargetPedRef = FindBestInteractionTarget(player, targetName, targetProfile);
    m_currentTargetName = targetName;
    m_currentTargetProfile = targetProfile;

    if (!m_interactionSession.open) {
        if (m_currentTargetPedRef != -1 && IsKeyJustPressed('E')) {
            m_interactionSession.open = true;
            m_interactionSession.targetPedRef = m_currentTargetPedRef;
            m_interactionSession.targetName = m_currentTargetName;
            m_interactionSession.targetProfile = m_currentTargetProfile;
            m_interactionSession.textEntryOpen = false;
            m_interactionSession.inputBuffer.clear();
            m_interactionSession.inputIntentLabel.clear();
            m_interactionSession.history.clear();
        }
        return;
    }

    if (IsKeyJustPressed(VK_ESCAPE) && m_interactionSession.textEntryOpen) {
        m_interactionSession.textEntryOpen = false;
        return;
    }

    if (!m_interactionSession.textEntryOpen && (IsKeyJustPressed(VK_ESCAPE) || IsKeyJustPressed('E'))) {
        m_interactionSession.open = false;
        return;
    }

    if (!IsPedHandleValid(m_interactionSession.targetPedRef)) {
        m_interactionSession.open = false;
        return;
    }

    CPed *targetPed = CPools::GetPed(m_interactionSession.targetPedRef);
    if (!targetPed || !ShouldTrackPed(targetPed, player) || !targetPed->IsAlive()) {
        m_interactionSession.open = false;
        return;
    }

    if (IsKeyJustPressed(VK_TAB) && !m_interactionSession.textEntryOpen) {
        m_interactionSession.textEntryOpen = true;
        m_interactionSession.inputBuffer.clear();
        m_interactionSession.inputIntentLabel = "pregunta";
        return;
    }

    if (m_interactionSession.textEntryOpen) {
        if (IsKeyJustPressed(VK_RETURN)) {
            if (!TrimBubbleText(m_interactionSession.inputBuffer).empty()) {
                ExecuteCustomInteraction(player, targetPed, m_interactionSession.inputBuffer, now);
                m_interactionSession.textEntryOpen = false;
                m_interactionSession.inputBuffer.clear();
                m_interactionSession.open = false;
            }
            return;
        }

        if (IsKeyJustPressed(VK_BACK) && !m_interactionSession.inputBuffer.empty()) {
            m_interactionSession.inputBuffer.pop_back();
            m_interactionSession.inputIntentLabel = InteractionLabel(InferActionFromCustomText(m_interactionSession.inputBuffer));
            return;
        }

        const bool shiftPressed = m_currKeyStates[VK_SHIFT] || m_currKeyStates[VK_LSHIFT] || m_currKeyStates[VK_RSHIFT];
        for (int vk = 0; vk < 256; ++vk) {
            if (!IsKeyJustPressed(vk)) {
                continue;
            }

            const char translated = TranslateVirtualKeyToChar(vk, shiftPressed);
            if (translated == '\0') {
                continue;
            }

            if (m_interactionSession.inputBuffer.size() < kMaxCustomInputLength) {
                m_interactionSession.inputBuffer.push_back(translated);
                m_interactionSession.inputIntentLabel = InteractionLabel(InferActionFromCustomText(m_interactionSession.inputBuffer));
            }
        }

        return;
    }

    for (const InteractionActionConfig &action : GetInteractionActions()) {
        if (action.virtualKey != 0 && IsKeyJustPressed(action.virtualKey)) {
            ExecuteInteraction(player, targetPed, action, now);
            m_interactionSession.open = false;
            return;
        }
    }
}

void Main::LogTargetPhrase(CPed *ped, short phraseId) {
    if (!ped || phraseId < 0) {
        return;
    }

    const int modelId = ped->m_nModelIndex;
    const std::string groupName = GetResolvedGroupName(modelId, ped->m_pedSpeech.m_nVoiceType);
    const std::string modelName = GetCatalogModelName(modelId);
    const std::string voiceLabel = GetCatalogVoiceLabel(ped);

    std::ostringstream keyStream;
    keyStream << groupName << '|' << modelId << '|' << phraseId;
    const std::string key = keyStream.str();
    if (!m_loggedTargetKeys.insert(key).second) {
        return;
    }

    try {
        const std::string logPath = GetAbsoluteRuntimePath(kTargetLogRelativePath);
        const std::string observedRoot = GetAbsoluteRuntimePath(kObservedRootRelativePath);
        if (logPath.empty() || observedRoot.empty()) {
            return;
        }

        std::filesystem::create_directories(std::filesystem::path(logPath).parent_path());
        std::filesystem::create_directories(observedRoot);
        std::ofstream file(logPath, std::ios::app);
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
        const short bankId = assignment ? assignment->m_nBankId : (slotInfo ? slotInfo->m_nBankId : -1);
        const short numSounds = assignment ? assignment->m_nNumSounds : (slotInfo ? slotInfo->m_nNumSoundInfo : -1);
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

        const std::filesystem::path groupRoot = std::filesystem::path(observedRoot) / groupName / modelName;
        std::filesystem::create_directories(groupRoot);

        std::ostringstream fileName;
        fileName << modelName << "__phrase_" << phraseId << "__bankid_" << bankId << "__bank_" << bankSlotId << "__sfx_" << soundIdInSlot << ".txt";

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

void Main::OnGameProcess() {
    if (!CPools::ms_pPedPool || FrontEndMenuManager.m_bMenuActive || FrontEndMenuManager.m_bDrawRadarOrMap) {
        return;
    }

    CPlayerPed *player = FindPlayerPed();
    if (!player) {
        return;
    }

    EnsureRuntimeBootstrapped();

    const unsigned int now = CTimer::m_snTimeInMilliseconds;
    DrainAiResults(player, now);
    UpdateInteraction(player, now);

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
            BubbleState &bubble = m_activeBubbles[pedRef];
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

    for (auto &entry : m_pedInteractionMemory) {
        DecayInteractionMemory(entry.second, now);
    }

    for (auto &entry : m_groupInteractionMemory) {
        DecayGroupInteractionMemory(entry.second, now);
    }

    for (auto it = m_pedSpeechState.begin(); it != m_pedSpeechState.end();) {
        if (!IsPedHandleValid(it->first) || now - it->second.lastSeenAt > 5000) {
            m_activeBubbles.erase(it->first);
            m_pedInteractionMemory.erase(it->first);
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

    if (m_playerBubble.expiresAt <= now) {
        m_playerBubble = BubbleState {};
    }
}

void Main::DrawInteractionUi() {
    const float left = ScaleX(16.0f);
    const float top = ScaleY(20.0f);

    if (m_currentTargetPedRef != -1 && !m_interactionSession.open) {
        const float width = ScaleX(258.0f);
        const float height = ScaleY(42.0f);
        CSprite2d::DrawRect(CRect(left, top, left + width, top + height), CRGBA(0, 0, 0, 110));
        SetupUiFont(0.28f, 0.72f);
        CFont::SetColor(CRGBA(255, 255, 255, 255));
        std::ostringstream prompt;
        prompt << "AIMOD [E] " << m_currentTargetName << " / " << m_currentTargetProfile << " / TAB chat / F5 DB / F7 IA";
        CFont::PrintString(left + ScaleX(6.0f), top + ScaleY(6.0f), prompt.str().c_str());

        const auto memoryIt = m_pedInteractionMemory.find(m_currentTargetPedRef);
        const PedInteractionMemory memory = memoryIt != m_pedInteractionMemory.end() ? memoryIt->second : PedInteractionMemory {};
        SetupUiFont(0.24f, 0.62f);
        CFont::SetColor(CRGBA(170, 230, 255, 255));
        std::ostringstream relation;
        relation << "Actitud: " << SummarizePedAttitude(memory)
                 << " / rap " << memory.rapport
                 << " / enojo " << memory.anger
                 << " / sospecha " << memory.suspicion;
        CFont::PrintString(left + ScaleX(6.0f), top + ScaleY(18.0f), relation.str().c_str());
    }

    if (m_interactionSession.open) {
        const auto &actions = GetInteractionActions();
        const int lineCount = std::min<int>(static_cast<int>(actions.size()), kMaxUiLines);
        const int historyCount = std::min<int>(static_cast<int>(m_interactionSession.history.size()), kMaxConversationHistoryLines);
        const float width = ScaleX(258.0f);
        const float historyHeight = historyCount > 0 ? ScaleY(10.5f * (historyCount + 1)) : 0.0f;
        const float height = ScaleY(56.0f + 12.0f * (lineCount + 3)) + historyHeight;
        const float boxTop = top + ScaleY(36.0f);
        CSprite2d::DrawRect(CRect(left, boxTop, left + width, boxTop + height), CRGBA(0, 0, 0, 150));

        SetupUiFont(0.29f, 0.72f);
        CFont::SetColor(CRGBA(255, 220, 120, 255));
        std::string header = "AIMOD // " + m_interactionSession.targetName + " // " + m_interactionSession.targetProfile;
        CFont::PrintString(left + ScaleX(6.0f), boxTop + ScaleY(6.0f), header.c_str());

        const auto memoryIt = m_pedInteractionMemory.find(m_interactionSession.targetPedRef);
        const PedInteractionMemory memory = memoryIt != m_pedInteractionMemory.end() ? memoryIt->second : PedInteractionMemory {};
        SetupUiFont(0.24f, 0.60f);
        CFont::SetColor(CRGBA(150, 220, 255, 255));
        std::ostringstream memoryLine;
        memoryLine << "Actitud: " << SummarizePedAttitude(memory)
                   << " / encuentros " << memory.encounters
                   << " / respeto " << memory.respect
                   << " / confianza " << memory.trust;
        CFont::PrintString(left + ScaleX(8.0f), boxTop + ScaleY(15.0f), memoryLine.str().c_str());

        SetupUiFont(0.26f, 0.66f);
        CFont::SetColor(CRGBA(255, 255, 255, 255));
        float cursorY = boxTop + ScaleY(25.0f);
        for (int i = 0; i < lineCount; ++i) {
            cursorY += ScaleY(10.5f);
            CFont::PrintString(left + ScaleX(8.0f), cursorY, actions[i].menuLabel.c_str());
        }

        cursorY += ScaleY(12.0f);
        CFont::SetColor(CRGBA(180, 240, 255, 255));
        CFont::PrintString(left + ScaleX(8.0f), cursorY, "TAB Chat libre");

        if (historyCount > 0) {
            cursorY += ScaleY(12.0f);
            CFont::SetColor(CRGBA(255, 210, 140, 255));
            CFont::PrintString(left + ScaleX(8.0f), cursorY, "Historial");

            SetupUiFont(0.24f, 0.60f);
            for (const ConversationLine &line : m_interactionSession.history) {
                cursorY += ScaleY(9.5f);
                CFont::SetColor(line.fromPlayer ? CRGBA(150, 220, 255, 255) : CRGBA(255, 255, 255, 255));
                const std::string prefix = line.fromPlayer ? "CJ: " : "PED: ";
                CFont::PrintString(left + ScaleX(10.0f), cursorY, (prefix + line.text).c_str());
            }
        }

        CFont::SetColor(CRGBA(170, 210, 255, 255));
        CFont::PrintString(left + ScaleX(8.0f), boxTop + height - ScaleY(11.0f), "ESC cerrar / F6 TTS / F7 IA");

        if (m_interactionSession.textEntryOpen) {
            const float inputTop = boxTop + height + ScaleY(6.0f);
            const float inputHeight = ScaleY(34.0f);
            CSprite2d::DrawRect(CRect(left, inputTop, left + width, inputTop + inputHeight), CRGBA(0, 0, 0, 170));
            SetupUiFont(0.27f, 0.68f);
            CFont::SetColor(CRGBA(130, 255, 180, 255));
            std::string modeLine = "Chat libre // intencion: " + (m_interactionSession.inputIntentLabel.empty() ? "pregunta" : m_interactionSession.inputIntentLabel);
            CFont::PrintString(left + ScaleX(6.0f), inputTop + ScaleY(4.0f), modeLine.c_str());

            SetupUiFont(0.28f, 0.70f);
            CFont::SetColor(CRGBA(255, 255, 255, 255));
            const std::string inputLine = "> " + m_interactionSession.inputBuffer + "_";
            CFont::PrintString(left + ScaleX(6.0f), inputTop + ScaleY(15.0f), inputLine.c_str());
        }
    }

    if (m_interactionSession.playerTextExpiresAt > CTimer::m_snTimeInMilliseconds) {
        const float boxLeft = ScaleX(16.0f);
        const float boxBottom = RsGlobal.maximumHeight - ScaleY(18.0f);
        const float width = ScaleX(200.0f);
        const float height = ScaleY(22.0f);
        CSprite2d::DrawRect(CRect(boxLeft, boxBottom - height, boxLeft + width, boxBottom), CRGBA(0, 0, 0, 120));
        SetupUiFont(0.28f, 0.72f);
        CFont::SetColor(CRGBA(180, 240, 255, 255));
        const std::string line = "Tu: " + m_interactionSession.playerText;
        CFont::PrintString(boxLeft + ScaleX(6.0f), boxBottom - height + ScaleY(5.0f), line.c_str());
    }

    std::string ttsStatus;
    {
        std::lock_guard<std::mutex> lock(m_ttsStatusMutex);
        if (m_ttsStatusUntilTick > GetTickCount()) {
            ttsStatus = m_ttsStatusText;
        }
    }

    if (!ttsStatus.empty()) {
        const float width = ScaleX(210.0f);
        const float height = ScaleY(20.0f);
        const float leftStatus = RsGlobal.maximumWidth - width - ScaleX(16.0f);
        const float topStatus = ScaleY(18.0f);
        CSprite2d::DrawRect(CRect(leftStatus, topStatus, leftStatus + width, topStatus + height), CRGBA(0, 0, 0, 120));
        SetupUiFont(0.27f, 0.68f);
        CFont::SetColor(CRGBA(160, 255, 180, 255));
        CFont::PrintString(leftStatus + ScaleX(6.0f), topStatus + ScaleY(4.0f), ttsStatus.c_str());
    }
}

void Main::OnDrawing() {
    if (FrontEndMenuManager.m_bMenuActive || FrontEndMenuManager.m_bDrawRadarOrMap) {
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

    if (m_playerBubble.expiresAt > CTimer::m_snTimeInMilliseconds) {
        RwV3d playerAnchor {};
        if (GetBubbleAnchor(player, playerAnchor)) {
            RwV3d playerScreen {};
            float scaleW = 0.0f;
            float scaleH = 0.0f;
            if (CSprite::CalcScreenCoors(playerAnchor, &playerScreen, &scaleW, &scaleH, true, true)) {
                BubbleLayout layout = MeasureBubbleLayout(m_playerBubble.text, playerScreen.x, playerScreen.y);
                ClampLayoutToScreen(layout);
                DrawCloudBubble(layout, playerScreen.y);
                SetupBubbleFont(layout.width - ScaleX(12.0f));
                CFont::SetColor(CRGBA(20, 40, 20, 255));
                const float lineHeight = ScaleY(10.0f);
                for (size_t i = 0; i < layout.lines.size(); ++i) {
                    const std::string line = SanitizeBubbleText(layout.lines[i]);
                    CFont::PrintString(layout.textX, layout.textY + static_cast<float>(i) * lineHeight, line.c_str());
                }
            }
        }
    }

    DrawInteractionUi();
}
