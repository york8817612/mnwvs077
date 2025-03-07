#include "DropPool.h"
#include "Drop.h"
#include "Reward.h"
#include "ItemInfo.h"
#include "Field.h"
#include "User.h"
#include "QWUInventory.h"
#include "StaticFoothold.h"
#include "WvsPhysicalSpace2D.h"
#include "..\Database\GW_ItemSlotBase.h"
#include "..\Database\GW_ItemSlotBundle.h"
#include "..\Database\GW_ItemSlotEquip.h"
#include "..\WvsLib\DateTime\GameDateTime.h"
#include "..\WvsLib\Memory\MemoryPoolMan.hpp"
#include "..\WvsGame\UserPacketTypes.hpp"

DropPool::DropPool(Field *pField)
	: m_pField(pField)
{
	m_tLastExpire = GameDateTime::GetTime();
	m_nDropIdCounter = 10000;
}

DropPool::~DropPool()
{
}

ZSharedPtr<Drop> DropPool::GetDrop(int nDropID)
{
	std::lock_guard<std::mutex> dropPoolock(m_mtxDropPoolLock);
	return m_mDrop[nDropID];
}

void DropPool::Create(ZUniquePtr<Reward>& zpReward, unsigned int dwOwnerID, unsigned int dwOwnPartyID, int nOwnType, unsigned int dwSourceID, int x1, int y1, int x2, int y2, unsigned int tDelay, int bAdmin, int nPos, bool bByPet)
{
	std::lock_guard<std::mutex> dropPoolock(m_mtxDropPoolLock);
	auto pFoothold = m_pField->GetSpace2D()->GetFootholdUnderneath(x2, y1 - 100, &y2);
	if (!m_pField->GetSpace2D()->IsPointInMBR(x2, y2, true))
	{
		pFoothold = m_pField->GetSpace2D()->GetFootholdClosest(m_pField, x2, y1, &x2, &y2, x1);
	}
	ZSharedPtr<Drop> pDrop = AllocObj(Drop);
	pDrop->Init(++m_nDropIdCounter, zpReward, dwOwnerID, dwOwnPartyID, nOwnType, dwSourceID, x1, y1, x2, y2, bByPet);
	auto pItem = pDrop->GetItem();
	if (pItem != nullptr && zpReward->GetType() == 1 && zpReward->GetPeriod() != 0)
		pItem->liExpireDate = GameDateTime::GetDateExpireFromPeriod(zpReward->GetPeriod());
	bool bEverLasting = m_bDropEverlasting ? dwSourceID == 0 : false;
	/*if (bAdmin)
		bEverLasting = reward->GetType() == 1
		&& (ItemInfo::GetInstance()->IsQuestItem(pItem->nItemID) || ItemInfo::GetInstance()->IsTradeBlockItem(pItem->nItemID));*/

	if (zpReward->GetType() == 1
		&& !dwSourceID
		&& !bAdmin
		&& pItem
		&& (ItemInfo::GetInstance()->IsQuestItem(pItem->nItemID) || ItemInfo::GetInstance()->IsTradeBlockItem(pItem->nItemID)))
	{
		//丟出後立即消失
		OutPacket oPacket;
		pDrop->MakeEnterFieldPacket(&oPacket, 3, tDelay);
		m_pField->BroadcastPacket(&oPacket);
		//if (!dwOwnerID)
		return;
	}
	else
	{
		OutPacket oPacket;
		pDrop->MakeEnterFieldPacket(&oPacket, 1, tDelay);
		//m_pField->BroadcastPacket(&oPacket);
		m_pField->RegisterFieldObj(pDrop, &oPacket);

		OutPacket oPacket2;
		pDrop->MakeEnterFieldPacket(&oPacket2, 0, tDelay);
		//m_pField->BroadcastPacket(&oPacket2);
		m_pField->RegisterFieldObj(pDrop, &oPacket2);
	}
	pDrop->m_tCreateTime = GameDateTime::GetTime();
	m_mDrop.insert({ pDrop->m_dwDropID, pDrop });
}

void DropPool::OnEnter(User * pUser)
{
	std::lock_guard<std::mutex> dropPoolock(m_mtxDropPoolLock);
	for (auto& drop : m_mDrop)
	{
		if (!drop.second->IsShowTo(pUser))
			continue;
		OutPacket oPacket;
		drop.second->MakeEnterFieldPacket(&oPacket);
		pUser->SendPacket(&oPacket);
	}
}

void DropPool::OnPacket(User *pUser, int nType, InPacket *iPacket)
{
	if(nType == UserRecvPacketType::User_OnUserPickupRequest)
		OnPickUpRequest(pUser, iPacket, nullptr);
}

void DropPool::OnPickUpRequest(User *pUser, InPacket *iPacket, Pet *pPet)
{
	iPacket->Decode4();
	iPacket->Decode1();
	unsigned int tCur = GameDateTime::GetTime();
	int nX = iPacket->Decode2();
	int nY = iPacket->Decode2();
	int nObjectID = iPacket->Decode4();
	bool bDropRemained = false;
	std::lock_guard<std::mutex> dropPoolLock(m_mtxDropPoolLock);
	auto findIter = m_mDrop.find(nObjectID);
	if (findIter != m_mDrop.end())
	{
		auto pDrop = findIter->second;
		int nItemID = 0, nCount = 0;

		//Check owner
		if(!pDrop->m_nOwnType &&
			pDrop->m_dwOwnerID &&
			pDrop->m_dwOwnerID != pUser->GetUserID() && 
			tCur - pDrop->m_tCreateTime < 10 * 1000)
			pUser->SendDropPickUpResultPacket(false, false, 0, 0, !pPet);

		//Check own party.
		if (pDrop->m_nOwnType &&
			pDrop->m_dwOwnPartyID != pUser->GetPartyID() &&
			tCur - pDrop->m_tCreateTime < 10 * 1000)
			pUser->SendDropPickUpResultPacket(false, false, 0, 0, !pPet);

		if (pDrop->m_bIsMoney)
		{
			nCount = pDrop->m_nMoney;
			bDropRemained = (QWUInventory::PickUpMoney(pUser, false, pDrop->m_nMoney) == false);
		}
		else 
		{
			nItemID = pDrop->m_pItem->nItemID;
			if (!ItemInfo::IsTreatSingly(pDrop->m_pItem->nItemID, pDrop->m_pItem->liExpireDate))
				nCount = ((GW_ItemSlotBundle*)pDrop->m_pItem)->nNumber;

			if (ItemInfo::GetInstance()->ConsumeOnPickup(nItemID))
			{
				auto *psItem = ItemInfo::GetInstance()->GetStateChangeItem(nItemID);
				psItem->Apply(pUser, GameDateTime::GetTime(), false);
				bDropRemained = false;
			}
			else
				bDropRemained = (QWUInventory::PickUpItem(pUser, false, pDrop->m_pItem) == false);
		}
		pUser->SendDropPickUpResultPacket(
			!bDropRemained,
			pDrop->m_bIsMoney,
			nItemID,
			nCount,
			!pPet
		);

		if (!bDropRemained)
		{
			OutPacket oPacket;
			pDrop->MakeLeaveFieldPacket(&oPacket, pPet ? 5 : 2, pUser->GetUserID(), pPet);
			m_pField->SplitSendPacket(&oPacket, nullptr);
			m_mDrop.erase(nObjectID);
		}
	}
}

std::vector<ZSharedPtr<Drop>> DropPool::FindDropInRect(const FieldRect & rc, unsigned int tTimeAfter)
{
	std::lock_guard<std::mutex> dropPoolLock(m_mtxDropPoolLock);
	std::vector<ZSharedPtr<Drop>> aRet;
	unsigned int tCur = GameDateTime::GetTime();
	for (auto& prDrop : m_mDrop)
	{
		if (tCur - prDrop.second->m_tCreateTime >= tTimeAfter &&
			rc.PtInRect({ prDrop.second->GetPosX(), prDrop.second->GetPosY() }))
			aRet.push_back(prDrop.second);
	}
	return aRet;
}

void DropPool::Remove(int nID, int tDelay)
{
	std::lock_guard<std::mutex> dropPoolLock(m_mtxDropPoolLock);
	auto findIter = m_mDrop.find(nID);
	if (findIter == m_mDrop.end())
		return;
	auto pDrop = findIter->second;
	m_mDrop.erase(findIter);
	OutPacket oPacket;
	pDrop->MakeLeaveFieldPacket(&oPacket, tDelay ? 4 : 0, tDelay, nullptr);
	m_pField->BroadcastPacket(&oPacket);
}

void DropPool::TryExpire(bool bRemoveAll)
{
	std::lock_guard<std::mutex> dropPoolLock(m_mtxDropPoolLock);
	unsigned int tCur = GameDateTime::GetTime();
	if (bRemoveAll || tCur - m_tLastExpire >= 10000)
	{
		for (auto iter = m_mDrop.begin(); iter != m_mDrop.end();)
		{
			if (!iter->second->m_bEverlasting &&
				(bRemoveAll || (tCur - iter->second->m_tCreateTime >= 180000)))
			{
				OutPacket oPacket;
				iter->second->MakeLeaveFieldPacket(&oPacket, 0, 0, nullptr);
				m_pField->SplitSendPacket(&oPacket, nullptr);
				iter = m_mDrop.erase(iter);
			}
			else
				++iter;
		}
	}
}
