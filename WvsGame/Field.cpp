#include "Field.h"
#include "LifePool.h"
#include "..\WvsLib\Net\PacketFlags\UserPacketFlags.hpp"
#include "..\WvsLib\Net\PacketFlags\ReactorPacketFlags.hpp"
#include "..\WvsLib\Net\PacketFlags\MobPacketFlags.hpp"
#include "..\WvsLib\Net\PacketFlags\NpcPacketFlags.hpp"
#include "..\WvsLib\Net\PacketFlags\FieldPacketFlags.hpp"
#include "..\WvsLib\Net\InPacket.h"
#include "..\WvsLib\Net\OutPacket.h"
#include "..\WvsLib\DateTime\GameDateTime.h"
#include "..\WvsLib\Logger\WvsLogger.h"
#include "..\Database\GA_Character.hpp"
#include "..\Database\GW_CharacterStat.h"
#include "..\WvsLib\Task\AsyncScheduler.h"
#include "Mob.h"
#include "MovePath.h"
#include "PortalMap.h"
#include "TownPortalPool.h"
#include "ReactorPool.h"
#include "SummonedPool.h"
#include "DropPool.h"
#include "FieldSet.h"
#include "User.h"
#include "PartyMan.h"
#include "WvsPhysicalSpace2D.h"
#include "ContinentMan.h"

#include <mutex>
#include <functional>

Field::Field(int nFieldID)
	: m_pLifePool(AllocObj(LifePool)),
	  m_pPortalMap(AllocObj(PortalMap)),
	  m_pTownPortalPool(AllocObj(TownPortalPool)),
	  m_pReactorPool(AllocObj(ReactorPool)),
	  m_pSpace2D(AllocObj(WvsPhysicalSpace2D))
{
	m_nFieldID = nFieldID;
	m_pDropPool = AllocObjCtor(DropPool)(this);
	m_pSummonedPool = AllocObjCtor(SummonedPool)(this);
	//m_asyncUpdateTimer = AsyncScheduler::CreateTask(std::bind(&Field::UpdateTrigger, this), 5000, true);
	//this->m_asyncUpdateTimer = (void*)timer;
	//InitLifePool();
}

Field::~Field()
{
	FreeObj(m_pLifePool);
	FreeObj(m_pPortalMap);
	FreeObj(m_pDropPool);
	FreeObj(m_pSummonedPool);
	FreeObj(m_pSpace2D);
	FreeObj(m_pReactorPool);
	FreeObj(m_pTownPortalPool);
	//m_asyncUpdateTimer->Abort();
	//delete m_asyncUpdateTimer;
}

void Field::BroadcastPacket(OutPacket * oPacket)
{
	std::lock_guard<std::recursive_mutex> userGuard(m_mtxFieldLock);
	oPacket->GetSharedPacket()->ToggleBroadcasting();

	if (m_mUser.size() == 0)
		return;
	for (auto& user : m_mUser)
		user.second->SendPacket(oPacket);
}

void Field::SetCould(bool cloud)
{
	m_bCloud = cloud;
}

bool Field::IsCloud() const
{
	return m_bCloud;
}

void Field::SetTown(bool town)
{
	m_bTown = town;
}

bool Field::IsTown() const
{
	return m_bTown;
}

void Field::SetSwim(bool swim)
{
	m_bSwim = swim;
}

bool Field::IsSwim() const
{
	return m_bSwim;
}

void Field::SetFly(bool fly)
{
	m_bFly = fly;
}

bool Field::IsFly() const
{
	return m_bFly;
}

void Field::SetFieldID(int nFieldID)
{
	this->m_nFieldID = nFieldID;
}

int Field::GetFieldID() const
{
	return m_nFieldID;
}

void Field::SetReturnMap(int returnMap)
{
	m_nReturnMap = returnMap;
}

int Field::GetReturnMap() const
{
	return m_nReturnMap;
}

void Field::SetForcedReturn(int forcedReturn)
{
	m_nForcedReturn = forcedReturn;
}

int Field::GetForcedReturn() const
{
	return m_nForcedReturn;
}

void Field::SetMobRate(double dMobRate)
{
	m_dMobRate = dMobRate;
}

double Field::GetMobRate() const
{
	return m_dMobRate;
}

void Field::SetFieldType(int fieldType)
{
	m_nFieldType = fieldType;
}

int Field::GetFieldType() const
{
	return m_nFieldType;
}

void Field::SetFieldLimit(int fieldLimit)
{
	m_nFieldLimit = fieldLimit;
}

int Field::GetFieldLimit() const
{
	return m_nFieldLimit;
}

void Field::SetCreateMobInterval(int interval)
{
	m_nCreateMobInterval = interval;
}

int Field::GetCreateMobInterval() const
{
	return m_nCreateMobInterval;
}

void Field::SetFiexdMobCapacity(int capacity)
{
	m_nFixedMobCapacity = capacity;
}

int Field::GetFixedMobCapacity() const
{
	return m_nFixedMobCapacity;
}

void Field::SetFirstUserEnter(const std::string & script)
{
	m_strFirstUserEnter = script;
}

const std::string & Field::GetFirstUserEnter() const
{
	return m_strFirstUserEnter;
}

void Field::SetUserEnter(const std::string & script)
{
	m_strUserEnter = script;
}

void Field::SetMapSize(int x, int y)
{
	m_szMap.x = x;
	m_szMap.y = y;
}

const FieldPoint & Field::GetMapSize() const
{
	return m_szMap;
}

void Field::SetLeftTop(int x, int y)
{
	m_ptLeftTop.x = x;
	m_ptLeftTop.y = y;
}

const FieldPoint & Field::GetLeftTop() const
{
	return m_ptLeftTop;
}

double Field::GetIncEXPRate() const
{
	return m_dIncRate_EXP;
}

void Field::SetFieldSet(FieldSet * pFieldSet)
{
	m_pParentFieldSet = pFieldSet;
}

FieldSet * Field::GetFieldSet()
{
	return m_pParentFieldSet;
}

void Field::InitLifePool()
{
	std::lock_guard<std::recursive_mutex> lifePoolGuard(m_mtxFieldLock);
	m_pLifePool->Init(this, m_nFieldID);
}

LifePool * Field::GetLifePool()
{
	return m_pLifePool;
}

DropPool * Field::GetDropPool()
{
	return m_pDropPool;
}

void Field::OnEnter(User *pUser)
{
	std::lock_guard<std::recursive_mutex> userGuard(m_mtxFieldLock);
	//if (!m_asyncUpdateTimer->IsStarted())
	//	m_asyncUpdateTimer->Start();
	m_mUser.insert({ pUser->GetUserID(), pUser });
	m_pLifePool->OnEnter(pUser);
	m_pDropPool->OnEnter(pUser);
	m_pReactorPool->OnEnter(pUser);
	if (m_pParentFieldSet != nullptr)
		m_pParentFieldSet->OnUserEnterField(pUser, this);


	OutPacket oPacketForBroadcasting;
	pUser->MakeEnterFieldPacket(&oPacketForBroadcasting);
	SplitSendPacket(&oPacketForBroadcasting, pUser);
	for (auto pFieldUser : m_mUser) 
	{
		if (pFieldUser.second != pUser)
		{
			OutPacket oPacketToTarget;
			pFieldUser.second->MakeEnterFieldPacket(&oPacketToTarget);
			pUser->SendPacket(&oPacketToTarget);
		}
	}
	PartyMan::GetInstance()->NotifyTransferField(pUser->GetUserID(), GetFieldID());
}

void Field::OnLeave(User *pUser)
{
	std::lock_guard<std::recursive_mutex> userGuard(m_mtxFieldLock);
	m_mUser.erase(pUser->GetUserID());
	m_pLifePool->RemoveController(pUser);
	//if (m_mUser.size() == 0 && m_asyncUpdateTimer->IsStarted())
	//	m_asyncUpdateTimer->Pause();;

	OutPacket oPacketForBroadcasting;
	pUser->MakeLeaveFieldPacket(&oPacketForBroadcasting);
	SplitSendPacket(&oPacketForBroadcasting, nullptr);
}

//發送oPacket給該地圖的其他User，其中pExcept是例外對象
void Field::SplitSendPacket(OutPacket *oPacket, User *pExcept)
{
	std::lock_guard<std::recursive_mutex> userGuard(m_mtxFieldLock);
	oPacket->GetSharedPacket()->ToggleBroadcasting();

	for (auto& user : m_mUser)
	{
		if ((pExcept == nullptr) || user.second->GetUserID() != pExcept->GetUserID())
			user.second->SendPacket(oPacket);
	}
}

void Field::OnPacket(User* pUser, InPacket *iPacket)
{
	int nType = iPacket->Decode2();
	if (nType >= FlagMin(MobRecvPacketFlag) && nType <= 0x9A)
		m_pLifePool->OnPacket(pUser, nType, iPacket);
	else if (nType == UserRecvPacketFlag::User_OnUserPickupRequest)
		m_pDropPool->OnPacket(pUser, nType, iPacket);
	else if (nType == FieldRecvPacketFlag::Field_OnContiMoveStateRequest)
		OnContiMoveState(pUser, iPacket);
	else if (nType >= FlagMin(ReactorRecvPacketFlag) && nType <= FlagMax(ReactorRecvPacketFlag))
		m_pReactorPool->OnPacket(pUser, nType, iPacket);
}

void Field::OnUserMove(User * pUser, InPacket * iPacket)
{
	iPacket->Decode1();
	MovePath movePath;
	movePath.Decode(iPacket);
	auto& lastElem = movePath.m_lElem.rbegin();
	pUser->SetMovePosition(lastElem->x, lastElem->y, lastElem->bMoveAction, lastElem->fh);
	OutPacket oPacket;
	oPacket.Encode2(UserSendPacketFlag::UserRemote_OnMove);
	oPacket.Encode4(pUser->GetUserID());
	movePath.Encode(&oPacket);
	this->SplitSendPacket(&oPacket, pUser);
	//GetLifePool()->UpdateMobSplit(pUser);
}

PortalMap * Field::GetPortalMap()
{
	return m_pPortalMap;
}

TownPortalPool * Field::GetTownPortalPool()
{
	return m_pTownPortalPool;
}

ReactorPool * Field::GetReactorPool()
{
	return m_pReactorPool;
}

SummonedPool * Field::GetSummonedPool()
{
	return m_pSummonedPool;
}

std::recursive_mutex & Field::GetFieldLock()
{
	return m_mtxFieldLock;
}

WvsPhysicalSpace2D * Field::GetSpace2D()
{
	return m_pSpace2D;
}

const std::map<int, User*>& Field::GetUsers()
{
	return m_mUser;
}

void Field::OnMobMove(User * pCtrl, Mob * pMob, InPacket * iPacket)
{
	//std::lock_guard<std::mutex> userGuard(m_mtxFieldLock);
	//bool bIsAzawanMoving = iPacket->Decode1() > 0;
	/*
	  v122 = CMobTemplate::_ZtlSecureGet_dwTemplateID(v121) / 0x2710 == 250
      || CMobTemplate::_ZtlSecureGet_dwTemplateID(v121) / 0x2710 == 251;
		 COutPacket::Encode1(&oPacket, v122);
	*/

	//_ZtlSecureTear_m_nMobCtrlSN
	short nMobCtrlSN = iPacket->Decode2();

	char flag = iPacket->Decode1();
	char bNextAttackPossible = (flag & 0x0F) != 0;
	char pCenterSplit = iPacket->Decode1();

	//ZtlSecureTear_bSN_CS
	unsigned char nSkillCommand = (iPacket->Decode1() & 0xFF);
	unsigned char nSLV = iPacket->Decode1();
	short nSkillEffect = iPacket->Decode2();
	int nData = 0;
	((char*)&nData)[0] = nSkillCommand;
	((char*)&nData)[1] = nSLV;
	((short*)&nData)[1] = nSkillEffect;
	bool bShootAttack = false;

	pMob->OnMobMove(
		bNextAttackPossible == 1,
		(pCenterSplit < 0 ? -1 : pCenterSplit >> 1),
		nData,
		&nSkillCommand,
		&nSLV,
		&bShootAttack);

	iPacket->Decode1();
	iPacket->Decode4();

	MovePath movePath;
	movePath.Decode(iPacket);

	//Encode Ctrl Ack Packet
	OutPacket ctrlAckPacket;
	ctrlAckPacket.Encode2(MobSendPacketFlag::Mob_OnCtrlAck);
	ctrlAckPacket.Encode4(pMob->GetFieldObjectID());
	ctrlAckPacket.Encode2(nMobCtrlSN);
	ctrlAckPacket.Encode1(bNextAttackPossible);
	ctrlAckPacket.Encode2((int)pMob->GetMP());
	ctrlAckPacket.Encode1(nSkillCommand);
	ctrlAckPacket.Encode1(nSLV);
	

	//Encode Move Packet
	OutPacket movePacket;
	movePacket.Encode2(MobSendPacketFlag::Mob_OnMove); //CMob::OnMove
	movePacket.Encode4(pMob->GetFieldObjectID());
	movePacket.Encode1(bNextAttackPossible);
	movePacket.Encode1(pCenterSplit);

	//dwData
	movePacket.Encode1(nSkillCommand);
	movePacket.Encode1(nSLV);
	movePacket.Encode2(nSkillEffect);

	std::lock_guard<std::recursive_mutex> lifeGuard(m_pLifePool->GetLock());
	//for (auto& elem : movePath.m_lElem)
	auto& elem = *(movePath.m_lElem.rbegin());
	{
		elem.y += -1;
		auto& elem = *(movePath.m_lElem.rbegin());
		pMob->SetPosX(elem.x);
		pMob->SetPosY(elem.y);
		pMob->SetMoveAction(elem.bMoveAction);
		//if(elem.fh != 0)
		pMob->SetFh(elem.fh);
	}
	//movePacket.Encode2(pMob->GetPosX());
	//movePacket.Encode2(pMob->GetPosY());
	movePath.Encode(&movePacket);

	//pCtrl->SendPacket(&ctrlAckPacket);
	pCtrl->SendPacket(&ctrlAckPacket);
	SplitSendPacket(&movePacket, pCtrl);
}

void Field::LoadAreaRect(void *pData)
{
	auto& areaData = *((WZ::Node*)pData);
	FieldRect rect;
	for (auto& area : areaData)
	{
		rect.left = area["x1"];
		rect.top = area["y1"];
		rect.right = area["x2"];
		rect.bottom = area["y2"];
		m_mAreaRect[area.Name()] = rect;
	}
}

int Field::CountFemaleInArea(const std::string & sArea)
{
	std::lock_guard<std::recursive_mutex> lock(m_mtxFieldLock);
	auto findIter = m_mAreaRect.find(sArea);
	if (findIter == m_mAreaRect.end())
		return 0;
	int nRet = 0;
	for (auto pUser : m_mUser)
	{
		if (!pUser.second->GetCharacterData()->mStat->nGender)
			continue;
		if (findIter->second.PtInRect({ pUser.second->GetPosX(), pUser.second->GetPosY() }))
			++nRet;
	}
	return nRet;
}

int Field::CountMaleInArea(const std::string & sArea)
{
	std::lock_guard<std::recursive_mutex> lock(m_mtxFieldLock);
	auto findIter = m_mAreaRect.find(sArea);
	if (findIter == m_mAreaRect.end())
		return 0;
	int nRet = 0;
	for (auto pUser : m_mUser)
	{
		if (pUser.second->GetCharacterData()->mStat->nGender)
			continue;
		if (findIter->second.PtInRect({ pUser.second->GetPosX(), pUser.second->GetPosY() }))
			++nRet;
	}
	return nRet;
}

int Field::CountUserInArea(const std::string & sArea)
{
	std::lock_guard<std::recursive_mutex> lock(m_mtxFieldLock);
	auto findIter = m_mAreaRect.find(sArea);
	if (findIter == m_mAreaRect.end())
		return 0;
	int nRet = 0;
	for (auto pUser : m_mUser)
	{
		if (findIter->second.PtInRect({ pUser.second->GetPosX(), pUser.second->GetPosY() }))
			++nRet;
	}
	return nRet;
}

void Field::EffectScreen(const std::string& sEffect)
{
	OutPacket oPacket;
	oPacket.Encode2(FieldSendPacketFlag::Field_OnFieldEffect);
	oPacket.Encode1(FieldEffect::e_FieldEffect_Screen);
	oPacket.EncodeStr(sEffect);
	BroadcastPacket(&oPacket);
}

void Field::EffectSound(const std::string& sEffect)
{
	OutPacket oPacket;
	oPacket.Encode2(FieldSendPacketFlag::Field_OnFieldEffect);
	oPacket.Encode1(FieldEffect::e_FieldEffect_Sound);
	oPacket.EncodeStr(sEffect);
	BroadcastPacket(&oPacket);
}

void Field::EffectObject(const std::string& sEffect)
{
	OutPacket oPacket;
	oPacket.Encode2(FieldSendPacketFlag::Field_OnFieldEffect);
	oPacket.Encode1(FieldEffect::e_FieldEffect_Object);
	oPacket.EncodeStr(sEffect);
	BroadcastPacket(&oPacket);
}

void Field::EnablePortal(const std::string& sPortal, bool bEnable)
{
	m_pPortalMap->EnablePortal(sPortal, bEnable);
}

void Field::OnContiMoveState(User * pUser, InPacket * iPacket)
{
	int nFieldID = iPacket->Decode4();
	int nEventDoing = ContinentMan::GetInstance()->GetInfo(nFieldID, 0);
	int nState = ContinentMan::GetInstance()->GetInfo(nFieldID, 1);
	OutPacket oPacket;
	oPacket.Encode2(FieldSendPacketFlag::Field_OnContiState);
	oPacket.Encode1(nState);
	oPacket.Encode1((char)nEventDoing);
	pUser->SendPacket(&oPacket);
}

void Field::TransferAll(int nFieldID, const std::string& sPortal)
{
	std::lock_guard<std::recursive_mutex> userGuard(m_mtxFieldLock);
	auto iter = m_mUser.begin();
	while (iter != m_mUser.end())
		(iter++)->second->TryTransferField(nFieldID, sPortal);
}

void Field::Reset(bool bShuffleReactor)
{
	m_pDropPool->TryExpire(true);
	m_pLifePool->Reset();
	m_pReactorPool->Reset(true);
	m_pPortalMap->ResetPortal();
}

void Field::Update()
{
	int tCur = GameDateTime::GetTime();
	m_pLifePool->Update();
	m_pReactorPool->Update(tCur); 
	m_pDropPool->TryExpire(false);
}
