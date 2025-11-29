#include "stdafx.h"
#include "constants.h"
#include "gm.h"
#include "messenger_manager.h"
#include "buffer_manager.h"
#include "desc_client.h"
#include "log.h"
#include "config.h"
#include "p2p.h"
#include "crc32.h"
#include "char.h"
#include "char_manager.h"
#include "questmanager.h"

#if defined(CROSS_CHANNEL_FRIEND_REQUEST) && defined(FIX_MESSENGER_ACTION_SYNC)
#include <vector>
#include <cstdarg>

#include "locale_service.h"
#endif

#ifdef FIX_MESSENGER_ACTION_SYNC
static char __account[CHARACTER_NAME_MAX_LEN * 2 + 1];
static char __companion[CHARACTER_NAME_MAX_LEN * 2 + 1];
#endif

MessengerManager::MessengerManager()
{
}

MessengerManager::~MessengerManager()
{
}

void MessengerManager::Initialize()
{
}

void MessengerManager::Destroy()
{
}

void MessengerManager::P2PLogin(MessengerManager::keyA account)
{
	Login(account);
}

void MessengerManager::P2PLogout(MessengerManager::keyA account)
{
	Logout(account);
}

void MessengerManager::Login(MessengerManager::keyA account)
{
	if (m_set_loginAccount.find(account) != m_set_loginAccount.end())
		return;

#ifdef FIX_MESSENGER_ACTION_SYNC
	DBManager::instance().EscapeString(__account, sizeof(__account), account.c_str(), account.size());

	if (account.compare(__account))
		return;
#endif

	DBManager::instance().FuncQuery(std::bind(&MessengerManager::LoadList, this, std::placeholders::_1),
			"SELECT account, companion FROM messenger_list%s WHERE account='%s'", get_table_postfix(), account.c_str());

	m_set_loginAccount.insert(account);
}

void MessengerManager::LoadList(SQLMsg * msg)
{
	if (NULL == msg)
		return;

	if (NULL == msg->Get())
		return;

	if (msg->Get()->uiNumRows == 0)
		return;

	std::string account;

	sys_log(1, "Messenger::LoadList");

	for (uint i = 0; i < msg->Get()->uiNumRows; ++i)
	{
		MYSQL_ROW row = mysql_fetch_row(msg->Get()->pSQLResult);

		if (row[0] && row[1])
		{
			if (account.length() == 0)
				account = row[0];

			m_Relation[row[0]].insert(row[1]);
			m_InverseRelation[row[1]].insert(row[0]);
		}
	}

	SendList(account);

	std::set<MessengerManager::keyT>::iterator it;

	for (it = m_InverseRelation[account].begin(); it != m_InverseRelation[account].end(); ++it)
		SendLogin(*it, account);
}

void MessengerManager::Logout(MessengerManager::keyA account)
{
	if (m_set_loginAccount.find(account) == m_set_loginAccount.end())
		return;

	m_set_loginAccount.erase(account);

	std::set<MessengerManager::keyT>::iterator it;

	for (it = m_InverseRelation[account].begin(); it != m_InverseRelation[account].end(); ++it)
	{
		SendLogout(*it, account);
	}

	std::map<keyT, std::set<keyT> >::iterator it2 = m_Relation.begin();

	while (it2 != m_Relation.end())
	{
		it2->second.erase(account);
		++it2;
	}

	m_Relation.erase(account);
	//m_map_stMobile.erase(account);
}

#if defined(CROSS_CHANNEL_FRIEND_REQUEST) || defined(FIX_MESSENGER_ACTION_SYNC)
#if defined(CROSS_CHANNEL_FRIEND_REQUEST) && defined(FIX_MESSENGER_ACTION_SYNC)
std::string MessengerManager::NormalizeCharacterName(const char* name)
{
	if (!name)
		return std::string();

	// P2P_MANAGER::Find does trim_and_lower for Brazil — match that here.
	if (LC_IsBrazil())
	{
		char tmp[CHARACTER_NAME_MAX_LEN + 1];
		trim_and_lower(name, tmp, sizeof(tmp));
		return std::string(tmp);
	}

	return std::string(name);
}

uint32_t MessengerManager::FriendRequestCRC(const char* a, const char* b)
{
	std::string na = NormalizeCharacterName(a);
	std::string nb = NormalizeCharacterName(b);

	uint32_t dw1 = GetCRC32(na.c_str(), na.size());
	uint32_t dw2 = GetCRC32(nb.c_str(), nb.size());

	char buf[64];
	snprintf(buf, sizeof(buf), "%u:%u", dw1, dw2);
	buf[sizeof(buf) - 1] = '\0';

	return GetCRC32(buf, strlen(buf));
}
#endif

void MessengerManager::NotifyRequesterOrLocal(const char* requesterName, LPCHARACTER ch, const char* fmt, ...)
{
	if (!fmt)
		return;

	char msgBuf[256];

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msgBuf, sizeof(msgBuf), fmt, ap);
	va_end(ap);

	if (ch)
	{
		// Send formatted message locally. Use simple "%s" to avoid LC_TEXT runtime issue.
		ch->ChatPacket(CHAT_TYPE_INFO, "%s", msgBuf);

		return;
	}

	// No local character -> relay to requester via P2P if possible.
	if (!requesterName)
		return;

	TPacketGCChat pack;

	pack.header = HEADER_GC_CHAT;
	pack.size = sizeof(TPacketGCChat) + (int)strlen(msgBuf);
	pack.type = CHAT_TYPE_INFO;
	pack.id = 0;

	std::vector<char> payload(pack.size);

	memcpy(payload.data(), &pack, sizeof(pack));
	memcpy(payload.data() + sizeof(pack), msgBuf, strlen(msgBuf));

	MessengerManager::SendP2PInfoToRequester(requesterName, payload.data(), pack.size);
}

void MessengerManager::SendP2PInfoToRequester(const char* requesterName, const void* gcPayload, int gcPayloadSize)
{
	if (!requesterName || !gcPayload || gcPayloadSize <= 0)
		return;

	CCI* pkCCI = P2P_MANAGER::instance().Find(requesterName);

	if (!pkCCI || !pkCCI->pkDesc)
		return;

	// Build P2P relay packet: TPacketGGRelay + gcPayload
	const int totalSize = sizeof(TPacketGGRelay) + gcPayloadSize;
	std::vector<char> buf;
	buf.resize(totalSize);

	TPacketGGRelay* relay = reinterpret_cast<TPacketGGRelay*>(buf.data());
	relay->bHeader = HEADER_GG_RELAY;
	strlcpy(relay->szName, requesterName, sizeof(relay->szName));
	relay->lSize = gcPayloadSize;

	memcpy(buf.data() + sizeof(TPacketGGRelay), gcPayload, gcPayloadSize);

	// Send to the peer that hosts the requester.
	pkCCI->pkDesc->Packet(buf.data(), totalSize);
}
#endif

#ifdef CROSS_CHANNEL_FRIEND_REQUEST
void MessengerManager::RegisterRequestToAdd(const char* name, const char* targetName)
{
	LPCHARACTER ch = CHARACTER_MANAGER::Instance().FindPC(name);

#ifdef FIX_MESSENGER_ACTION_SYNC
	// In-memory quick check (fast, works if lists are loaded)
	if (IsInList(name, targetName) || IsInList(targetName, name))
	{
		MessengerManager::NotifyRequesterOrLocal(name, ch, LC_TEXT("[Friends] You are already friends with %s."), targetName);

		return;
	}
#endif

#ifdef FIX_MESSENGER_ACTION_SYNC
	uint32_t dwComplex = FriendRequestCRC(name, targetName);
#else
	uint32_t dw1 = GetCRC32(name, strlen(name));
	uint32_t dw2 = GetCRC32(targetName, strlen(targetName));

	char buf[64]{ 0, };
	snprintf(buf, sizeof(buf), "%u:%u", dw1, dw2);
	buf[63] = '\0';

	uint32_t dwComplex = GetCRC32(buf, strlen(buf));
#endif

#ifdef FIX_MESSENGER_ACTION_SYNC
	uint32_t dwComplexRev = FriendRequestCRC(targetName, name);

	// Check if this requester already sent the same request
	if (m_set_requestToAdd.find(dwComplex) != m_set_requestToAdd.end())
	{
		MessengerManager::NotifyRequesterOrLocal(name, ch, LC_TEXT("[Friends] You already sent a friend request to %s."), targetName);

		return;
	}

	// Check if target already sent a request to requester (reverse)
	if (m_set_requestToAdd.find(dwComplexRev) != m_set_requestToAdd.end())
	{
		MessengerManager::NotifyRequesterOrLocal(name, ch, LC_TEXT("[Friends] %s has already sent you a friend request."), targetName);

		sys_log(0, "MessengerManager::RegisterRequestToAdd: Duplicate friend request between %s and %s YOU DUMDUM!", name, targetName);
		return;
	}
#endif

	m_set_requestToAdd.insert(dwComplex);
}

// stage 1: starts on the core where "ch" resides. Validate ch and move to stage 2
void MessengerManager::P2PRequestToAdd_Stage1(LPCHARACTER ch, const char* targetName)
{
	LPCHARACTER pkTarget = CHARACTER_MANAGER::Instance().FindPC(targetName);

	if (!pkTarget)
	{
		if (!ch || !ch->IsPC())
			return;

		if (quest::CQuestManager::instance().GetPCForce(ch->GetPlayerID())->IsRunning() == true)
		{
			ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("상대방이 친구 추가를 받을 수 없는 상태입니다."));

			return;
		}

		TPacketGGMessengerRequest p2pp{};

		p2pp.header = HEADER_GG_MESSENGER_REQUEST_ADD;

		strlcpy(p2pp.account, ch->GetName(), CHARACTER_NAME_MAX_LEN + 1);
		strlcpy(p2pp.target, targetName, CHARACTER_NAME_MAX_LEN + 1);

		P2P_MANAGER::Instance().Send(&p2pp, sizeof(TPacketGGMessengerRequest));
	}
	else // if we have both, just continue normally
		RequestToAdd(ch, pkTarget);
}

// stage 2: ends up on the core where the target resides
void MessengerManager::P2PRequestToAdd_Stage2(const char* characterName, const char* targetName)
{
	if (!targetName || !characterName)
		return;

	LPCHARACTER target = CHARACTER_MANAGER::Instance().FindPC(targetName);

	if (!target || !target->IsPC())
		return;

	LPCHARACTER ch = CHARACTER_MANAGER::Instance().FindPC(characterName);

#ifdef FIX_MESSENGER_ACTION_SYNC
	if (0 == strncmp(characterName, targetName, CHARACTER_NAME_MAX_LEN))
	{
		MessengerManager::NotifyRequesterOrLocal(characterName, ch, LC_TEXT("[Friends] You cannot add yourself as a friend."));

		return;
	}
#endif

	if (quest::CQuestManager::instance().GetPCForce(target->GetPlayerID())->IsRunning())
		return;

	if (target->IsBlockMode(BLOCK_MESSENGER_INVITE))
	{
		MessengerManager::NotifyRequesterOrLocal(characterName, ch, LC_TEXT("상대방이 메신져 추가 거부 상태입니다."));

		return;
	}


#ifdef FIX_MESSENGER_ACTION_SYNC
	// In-memory quick check (fast, works if lists are loaded)
	if (IsInList(characterName, targetName) || IsInList(targetName, characterName))
	{
		MessengerManager::NotifyRequesterOrLocal(characterName, ch, LC_TEXT("[Friends] You are already friends with %s."), targetName);

		return;
	}

	uint32_t dwComplex = FriendRequestCRC(characterName, targetName);
	uint32_t dwComplexRev = FriendRequestCRC(targetName, characterName);

	// Check if this requester already sent the same request
	if (m_set_requestToAdd.find(dwComplex) != m_set_requestToAdd.end())
	{
		MessengerManager::NotifyRequesterOrLocal(characterName, ch, LC_TEXT("[Friends] You already sent a friend request to %s."), targetName);

		return;
	}

	// Check if target already sent a request to requester (reverse)
	if (m_set_requestToAdd.find(dwComplexRev) != m_set_requestToAdd.end())
	{
		MessengerManager::NotifyRequesterOrLocal(characterName, ch, LC_TEXT("[Friends] %s has already sent you a friend request."), targetName);

		sys_log(0, "MessengerManager::P2PRequestToAdd_Stage2 : request already exist %s -> %s YOU DUMDUM!", characterName, targetName);
		return;
	}
#endif

	MessengerManager::Instance().RegisterRequestToAdd(characterName, targetName);
	target->ChatPacket(CHAT_TYPE_COMMAND, "messenger_auth %s", characterName);
}
#endif

void MessengerManager::RequestToAdd(LPCHARACTER ch, LPCHARACTER target)
{
	if (!ch->IsPC() || !target->IsPC())
		return;

#ifdef FIX_MESSENGER_ACTION_SYNC
	if (ch->GetPlayerID() == target->GetPlayerID())
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("[Friends] You cannot add yourself as a friend."));

		return;
	}
#endif

	if (quest::CQuestManager::instance().GetPCForce(ch->GetPlayerID())->IsRunning() == true)
	{
	    ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("상대방이 친구 추가를 받을 수 없는 상태입니다."));
	    return;
	}

	if (quest::CQuestManager::instance().GetPCForce(target->GetPlayerID())->IsRunning() == true)
		return;

#ifdef FIX_MESSENGER_ACTION_SYNC
	std::string requester = ch->GetName();
	std::string companion = target->GetName();

	// In-memory quick check (fast, works if lists are loaded)
	if (IsInList(requester, companion) || IsInList(companion, requester))
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("[Friends] You are already friends with %s."), companion.c_str());
		return;
	}

	DWORD dwComplex = FriendRequestCRC(ch->GetName(), target->GetName());
	DWORD dwComplexRev = FriendRequestCRC(target->GetName(), ch->GetName());

	// Check if this requester already sent the same request
	if (m_set_requestToAdd.find(dwComplex) != m_set_requestToAdd.end())
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("[Friends] You already sent a friend request to %s."), companion.c_str());
		return;
	}

	// Check if target already sent a request to requester (reverse)
	if (m_set_requestToAdd.find(dwComplexRev) != m_set_requestToAdd.end())
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("[Friends] %s has already sent you a friend request."), companion.c_str());
		sys_log(0, "MessengerManager::RequestToAdd: Duplicate friend request between %s and %s YOU DUMDUM!", requester.c_str(), companion.c_str());

		return;
	}
#else
	DWORD dw1 = GetCRC32(ch->GetName(), strlen(ch->GetName()));
	DWORD dw2 = GetCRC32(target->GetName(), strlen(target->GetName()));

	char buf[64];
	snprintf(buf, sizeof(buf), "%u:%u", dw1, dw2);
	DWORD dwComplex = GetCRC32(buf, strlen(buf));
#endif

	m_set_requestToAdd.insert(dwComplex);

	target->ChatPacket(CHAT_TYPE_COMMAND, "messenger_auth %s", ch->GetName());
}

// void MessengerManager::AuthToAdd(MessengerManager::keyA account, MessengerManager::keyA companion, bool bDeny)
// {
	// DWORD dw1 = GetCRC32(companion.c_str(), companion.length());
	// DWORD dw2 = GetCRC32(account.c_str(), account.length());

	// char buf[64];
	// snprintf(buf, sizeof(buf), "%u:%u", dw1, dw2);
	// DWORD dwComplex = GetCRC32(buf, strlen(buf));

	// if (m_set_requestToAdd.find(dwComplex) == m_set_requestToAdd.end())
	// {
		// sys_log(0, "MessengerManager::AuthToAdd : request not exist %s -> %s", companion.c_str(), account.c_str());
		// return;
	// }

	// m_set_requestToAdd.erase(dwComplex);

	// if (!bDeny)
	// {
		// AddToList(companion, account);
		// AddToList(account, companion);
	// }
// }

bool MessengerManager::AuthToAdd(MessengerManager::keyA account, MessengerManager::keyA companion, bool bDeny)
{
	DWORD dw1 = GetCRC32(companion.c_str(), companion.length());
	DWORD dw2 = GetCRC32(account.c_str(), account.length());

	char buf[64];
	snprintf(buf, sizeof(buf), "%u:%u", dw1, dw2);
	DWORD dwComplex = GetCRC32(buf, strlen(buf));

	if (m_set_requestToAdd.find(dwComplex) == m_set_requestToAdd.end())
	{
		sys_log(0, "MessengerManager::AuthToAdd : request not exist %s -> %s", companion.c_str(), account.c_str());
		return false;
	}

	m_set_requestToAdd.erase(dwComplex);

#ifdef FIX_MESSENGER_ACTION_SYNC
	// In-memory quick check (fast, works if lists are loaded)
	if (IsInList(account, companion) || IsInList(companion, account))
	{
		LPCHARACTER acc_ch = CHARACTER_MANAGER::instance().FindPC(account.c_str());

		if (acc_ch)
			acc_ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("[Friends] You are already friends with %s."), companion.c_str());

		return false;
	}
#endif

	if (!bDeny)
	{
		AddToList(companion, account);
		AddToList(account, companion);
	}

	return true;
}

void MessengerManager::__AddToList(MessengerManager::keyA account, MessengerManager::keyA companion)
{
	m_Relation[account].insert(companion);
	m_InverseRelation[companion].insert(account);
//#ifdef FIX_MESSENGER_ACTION_SYNC
//	m_Relation[companion].insert(account);
//	m_InverseRelation[account].insert(companion);
//#endif


	LPCHARACTER ch = CHARACTER_MANAGER::instance().FindPC(account.c_str());
	LPDESC d = ch ? ch->GetDesc() : NULL;

	if (d)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<메신져> %s 님을 친구로 추가하였습니다."), companion.c_str());
	}

	LPCHARACTER tch = CHARACTER_MANAGER::instance().FindPC(companion.c_str());

#ifdef CROSS_CHANNEL_FRIEND_REQUEST
	if (tch || P2P_MANAGER::Instance().Find(companion.c_str()))
#else
	if (tch)
#endif
		SendLogin(account, companion);
	else
		SendLogout(account, companion);
}

void MessengerManager::AddToList(MessengerManager::keyA account, MessengerManager::keyA companion)
{
	if (companion.size() == 0)
		return;

	if (m_Relation[account].find(companion) != m_Relation[account].end())
		return;

#ifdef FIX_MESSENGER_ACTION_SYNC
	DBManager::instance().EscapeString(__account, sizeof(__account), account.c_str(), account.size());
	DBManager::instance().EscapeString(__companion, sizeof(__companion), companion.c_str(), companion.size());

	if (account.compare(__account) || companion.compare(__companion))
		return;
#endif

	sys_log(0, "Messenger Add %s %s", account.c_str(), companion.c_str());

	DBManager::instance().Query("INSERT INTO messenger_list%s VALUES ('%s', '%s')", 
			get_table_postfix(), account.c_str(), companion.c_str());

	__AddToList(account, companion);

	TPacketGGMessenger p2ppck;

	p2ppck.bHeader = HEADER_GG_MESSENGER_ADD;
	strlcpy(p2ppck.szAccount, account.c_str(), sizeof(p2ppck.szAccount));
	strlcpy(p2ppck.szCompanion, companion.c_str(), sizeof(p2ppck.szCompanion));
	P2P_MANAGER::instance().Send(&p2ppck, sizeof(TPacketGGMessenger));
}

void MessengerManager::__RemoveFromList(MessengerManager::keyA account, MessengerManager::keyA companion)
{
	m_Relation[account].erase(companion);
	m_InverseRelation[companion].erase(account);
#ifdef FIX_MESSENGER_ACTION_SYNC
	m_Relation[companion].erase(account);
	m_InverseRelation[account].erase(companion);
#endif

	LPCHARACTER ch = CHARACTER_MANAGER::instance().FindPC(account.c_str());
	LPDESC d = ch ? ch->GetDesc() : NULL;

	if (d)
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("<메신져> %s 님을 메신저에서 삭제하였습니다."), companion.c_str());

#ifdef FIX_MESSENGER_ACTION_SYNC
	LPCHARACTER tch = CHARACTER_MANAGER::Instance().FindPC(companion.c_str());

	if (tch && tch->GetDesc())
	{
		TPacketGCMessenger p;
		p.header		= HEADER_GC_MESSENGER;
		p.subheader		= MESSENGER_SUBHEADER_GC_REMOVE_FRIEND;
		p.size			= sizeof(TPacketGCMessenger) + sizeof(BYTE) + account.size();

		BYTE bLen		= account.size();
		tch->GetDesc()->BufferedPacket(&p, sizeof(p));
		tch->GetDesc()->BufferedPacket(&bLen, sizeof(BYTE));
		tch->GetDesc()->Packet(account.c_str(), account.size());
	}
#endif
}

bool MessengerManager::IsInList(MessengerManager::keyA account, MessengerManager::keyA companion) // Fix
{
    if (m_Relation.find(account) == m_Relation.end())
        return false;

    if (m_Relation[account].empty())
        return false;

    return m_Relation[account].find(companion) != m_Relation[account].end();
}

void MessengerManager::RemoveFromList(MessengerManager::keyA account, MessengerManager::keyA companion)
{
	if (companion.empty())
		return;
	
	if (companion.size() == 0)
		return;
	
	if (!IsInList(account, companion)) // Fix
		return;
	
#ifdef FIX_MESSENGER_ACTION_SYNC
	DBManager::instance().EscapeString(__account, sizeof(__account), account.c_str(), account.size());
    DBManager::instance().EscapeString(__companion, sizeof(__companion), companion.c_str(), companion.size());
  
    if (account.compare(__account) || companion.compare(__companion))
        return;
#else
	char companionEscaped[CHARACTER_NAME_MAX_LEN * 2 + 1];

	DBManager::instance().EscapeString(companionEscaped, sizeof(companionEscaped), companion.c_str(), companion.length());
#endif

	sys_log(1, "Messenger Remove %s %s", account.c_str(), companion.c_str());
	
	// DBManager::instance().Query("DELETE FROM messenger_list%s WHERE account='%s' AND companion = '%s'",
			// get_table_postfix(), account.c_str(), companion.c_str());

	// Fix
#ifdef FIX_MESSENGER_ACTION_SYNC
	DBManager::instance().Query("DELETE FROM messenger_list%s WHERE (account='%s' AND companion = '%s') OR (account = '%s' AND companion = '%s')",
			get_table_postfix(), account.c_str(), companion.c_str(), companion.c_str(), account.c_str());
#else
	DBManager::instance().Query("DELETE FROM messenger_list%s WHERE account='%s' AND companion = '%s'",
			get_table_postfix(), account.c_str(), companion.c_str());
#endif

	__RemoveFromList(account, companion);

	TPacketGGMessenger p2ppck;
	
	p2ppck.bHeader = HEADER_GG_MESSENGER_REMOVE;
	strlcpy(p2ppck.szAccount, account.c_str(), sizeof(p2ppck.szAccount));
	strlcpy(p2ppck.szCompanion, companion.c_str(), sizeof(p2ppck.szCompanion));
	P2P_MANAGER::instance().Send(&p2ppck, sizeof(TPacketGGMessenger));
}

void MessengerManager::RemoveAllList(keyA account)
{
	std::set<keyT>	company(m_Relation[account]);

#ifdef FIX_MESSENGER_ACTION_SYNC
    DBManager::instance().EscapeString(__account, sizeof(__account), account.c_str(), account.size());

	if (account.compare(__account))
        	return;
#endif

	/* SQL Data 삭제 */
	DBManager::instance().Query("DELETE FROM messenger_list%s WHERE account='%s' OR companion='%s'",
			get_table_postfix(), account.c_str(), account.c_str());

	/* 내가 가지고있는 리스트 삭제 */
	for (std::set<keyT>::iterator iter = company.begin();
			iter != company.end();
			iter++ )
	{
		this->RemoveFromList(account, *iter);
#ifdef FIX_MESSENGER_ACTION_SYNC
		this->RemoveFromList(*iter, account);
#endif
	}

	/* 복사한 데이타 삭제 */
	for (std::set<keyT>::iterator iter = company.begin();
			iter != company.end();
			)
	{
		company.erase(iter++);
	}

	company.clear();
}


void MessengerManager::SendList(MessengerManager::keyA account)
{
	LPCHARACTER ch = CHARACTER_MANAGER::instance().FindPC(account.c_str());

	if (!ch)
		return;

	LPDESC d = ch->GetDesc();

	if (!d)
		return;

	if (m_Relation.find(account) == m_Relation.end())
		return;

	if (m_Relation[account].empty())
		return;

	TPacketGCMessenger pack;

	pack.header		= HEADER_GC_MESSENGER;
	pack.subheader	= MESSENGER_SUBHEADER_GC_LIST;
	pack.size		= sizeof(TPacketGCMessenger);

	TPacketGCMessengerListOffline pack_offline;
	TPacketGCMessengerListOnline pack_online;

	TEMP_BUFFER buf(128 * 1024); // 128k

	itertype(m_Relation[account]) it = m_Relation[account].begin(), eit = m_Relation[account].end();

	while (it != eit)
	{
		if (m_set_loginAccount.find(*it) != m_set_loginAccount.end())
		{
			pack_online.connected = 1;

			// Online
			pack_online.length = it->size();

			buf.write(&pack_online, sizeof(TPacketGCMessengerListOnline));
			buf.write(it->c_str(), it->size());
		}
		else
		{
			pack_offline.connected = 0;

			// Offline
			pack_offline.length = it->size();

			buf.write(&pack_offline, sizeof(TPacketGCMessengerListOffline));
			buf.write(it->c_str(), it->size());
		}

		++it;
	}

	pack.size += buf.size();

	d->BufferedPacket(&pack, sizeof(TPacketGCMessenger));
	d->Packet(buf.read_peek(), buf.size());
}

void MessengerManager::SendLogin(MessengerManager::keyA account, MessengerManager::keyA companion)
{
	LPCHARACTER ch = CHARACTER_MANAGER::instance().FindPC(account.c_str());
	LPDESC d = ch ? ch->GetDesc() : NULL;

	if (!d)
		return;

	if (!d->GetCharacter())
		return;

	if (ch->GetGMLevel() == GM_PLAYER && gm_get_level(companion.c_str()) != GM_PLAYER)
		return;

	BYTE bLen = companion.size();

	TPacketGCMessenger pack;

	pack.header			= HEADER_GC_MESSENGER;
	pack.subheader		= MESSENGER_SUBHEADER_GC_LOGIN;
	pack.size			= sizeof(TPacketGCMessenger) + sizeof(BYTE) + bLen;

	d->BufferedPacket(&pack, sizeof(TPacketGCMessenger));
	d->BufferedPacket(&bLen, sizeof(BYTE));
	d->Packet(companion.c_str(), companion.size());
}

void MessengerManager::SendLogout(MessengerManager::keyA account, MessengerManager::keyA companion)
{
	if (!companion.size())
		return;

	LPCHARACTER ch = CHARACTER_MANAGER::instance().FindPC(account.c_str());
	LPDESC d = ch ? ch->GetDesc() : NULL;

	if (!d)
		return;

	BYTE bLen = companion.size();

	TPacketGCMessenger pack;

	pack.header		= HEADER_GC_MESSENGER;
	pack.subheader	= MESSENGER_SUBHEADER_GC_LOGOUT;
	pack.size		= sizeof(TPacketGCMessenger) + sizeof(BYTE) + bLen;

	d->BufferedPacket(&pack, sizeof(TPacketGCMessenger));
	d->BufferedPacket(&bLen, sizeof(BYTE));
	d->Packet(companion.c_str(), companion.size());
}

