#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Uplay R1 API Emulation
// ═══════════════════════════════════════════════════════════════════════

#ifndef ACU_UPLAY_API_HPP
#define ACU_UPLAY_API_HPP

#include <cstdint>

// All Uplay API functions must be exported with C linkage and __cdecl convention.
// The .def file handles export naming; these are the implementations.

extern "C" {

// ── Core Lifecycle ────────────────────────────────────────────────────
int __cdecl UplayStart(unsigned productId, unsigned flags);
int __cdecl UplayStop();
int __cdecl UplayInitialize(unsigned version, void* config);
int __cdecl UplayUpdate();

// ── User / Authentication ─────────────────────────────────────────────
int __cdecl UplayUserGetTicket(void* outTicket, unsigned maxSize, unsigned* outSize);
int __cdecl UplayUserGetTicketSize(unsigned* outSize);
int __cdecl UplayUserGetCdKeys(void** outList);
int __cdecl UplayUserGetCdKeyUtf8(char* outKey, unsigned maxSize);
int __cdecl UplayUserGetCredentials(void* outCreds);
int __cdecl UplayUserIsConnected();
int __cdecl UplayUserIsOwned(unsigned productId);
int __cdecl UplayUserGetEmail(char* outEmail, unsigned maxSize);
int __cdecl UplayUserGetName(wchar_t* outName, unsigned maxSize);
int __cdecl UplayUserGetNameUtf8(char* outName, unsigned maxSize);
int __cdecl UplayUserGetAccountId(void* outId);
int __cdecl UplayUserGetAccountIdUtf8(char* outId, unsigned maxSize);
int __cdecl UplayUserGetGPUScoreConfidenceLevel(int* outLevel);
int __cdecl UplayUserGetGPUScore(int* outScore);
int __cdecl UplayUserGetCPUScore(int* outScore);
int __cdecl UplayUserClearGameSession();
int __cdecl UplayUserSetGameSession(void* sessionData);
int __cdecl UplayUserGetConsumableItems(void** outList);
int __cdecl UplayUserConsumeItem(unsigned itemId);

// ── Overlay ───────────────────────────────────────────────────────────
int __cdecl UplayOverlayShow(unsigned overlayType);
int __cdecl UplayOverlaySetShopUrl(const char* url);

// ── Product / Install ─────────────────────────────────────────────────
int __cdecl UplayProductIsInstalled(unsigned productId);
int __cdecl UplayProductGetId(unsigned* outId);
int __cdecl UplayProductGetIdUtf8(char* outId, unsigned maxSize);
int __cdecl UplayInstallGetChunks(void** outChunks);
int __cdecl UplayInstallGetLanguage(unsigned* outLang);
int __cdecl UplayInstallIsDlcInstalled(unsigned dlcId);
int __cdecl UplayInstallGetLanguageUtf8(char* outLang, unsigned maxSize);

// ── Friends ───────────────────────────────────────────────────────────
int __cdecl UplayFriendInviteToGame(const char* friendId);
int __cdecl UplayFriendGetList(void** outList);
int __cdecl UplayFriendIsFriend(const char* userId);
int __cdecl UplayFriendGetNameUtf8(const char* userId, char* outName, unsigned maxSize);

// ── Party ─────────────────────────────────────────────────────────────
int __cdecl UplayPartyInit();
int __cdecl UplayPartyGetId(void* outId);
int __cdecl UplayPartyGetMembers(void** outList);
int __cdecl UplayPartyGetInGameMembers(void** outList);
int __cdecl UplayPartyInviteToParty(const char* userId);
int __cdecl UplayPartySetUserData(void* data, unsigned size);
int __cdecl UplayPartySendGameInvitation(const char* userId);
int __cdecl UplayPartyIsPartyLeader();
int __cdecl UplayPartyIsInParty();
int __cdecl UplayPartyMemberIsLeader(const char* userId);
int __cdecl UplayPartyRespondToGameInvitation(int accept);
int __cdecl UplayPartyShowGameInvitationDialog();

// ── Miscellaneous ─────────────────────────────────────────────────────
int __cdecl UplayWinIsInstalled();
int __cdecl UplayAchievement(unsigned achievementId);
int __cdecl UplayAchievementWrite(unsigned achievementId, unsigned value);
int __cdecl UplayEventRegisterHandler(unsigned eventId, void* handler);
int __cdecl UplayEventUnregisterHandler(unsigned eventId);

// ── Save System ───────────────────────────────────────────────────────
int __cdecl UplaySaveGetPath(wchar_t* outPath, unsigned maxSize);
int __cdecl UplaySaveGetPathUtf8(char* outPath, unsigned maxSize);
int __cdecl UplaySaveOpen(const char* filename, void** outHandle);
int __cdecl UplaySaveClose(void* handle);
int __cdecl UplaySaveRead(void* handle, void* buffer, unsigned size, unsigned* outRead);
int __cdecl UplaySaveWrite(void* handle, const void* buffer, unsigned size, unsigned* outWritten);
int __cdecl UplaySaveRemove(const char* filename);

} // extern "C"

#endif // ACU_UPLAY_API_HPP
