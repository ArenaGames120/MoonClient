/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "controls.h"

#include <base/dbg.h>
#include <base/mem.h>
#include <base/time.h>
#include <base/vmath.h>
#include <cmath>

#include <engine/client.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/components/camera.h>
#include <game/client/components/chat.h>
#include <game/client/components/menus.h>
#include <game/client/components/scoreboard.h>
#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/localization.h>

#include <algorithm>

CControls::CControls()
{
	mem_zero(&m_aLastData, sizeof(m_aLastData));
	std::fill(std::begin(m_aMousePos), std::end(m_aMousePos), vec2(0.0f, 0.0f));
	std::fill(std::begin(m_aMousePosOnAction), std::end(m_aMousePosOnAction), vec2(0.0f, 0.0f));
	std::fill(std::begin(m_aTargetPos), std::end(m_aTargetPos), vec2(0.0f, 0.0f));
	std::fill(std::begin(m_aMouseInputType), std::end(m_aMouseInputType), EMouseInputType::ABSOLUTE);
}

void CControls::OnReset()
{
	ResetInput(0);
	ResetInput(1);

	std::fill(std::begin(m_aAmmoCount), std::end(m_aAmmoCount), 0);

	m_LastSendTime = 0;
}

void CControls::ResetInput(int Dummy)
{
	m_aLastData[Dummy].m_Direction = 0;
	// simulate releasing the fire button
	if((m_aLastData[Dummy].m_Fire & 1) != 0)
		m_aLastData[Dummy].m_Fire++;
	m_aLastData[Dummy].m_Fire &= INPUT_STATE_MASK;
	m_aLastData[Dummy].m_Jump = 0;
	m_aInputData[Dummy] = m_aLastData[Dummy];

	m_aInputDirectionLeft[Dummy] = 0;
	m_aInputDirectionRight[Dummy] = 0;
}

void CControls::OnPlayerDeath()
{
	std::fill(std::begin(m_aAmmoCount), std::end(m_aAmmoCount), 0);
}

struct CInputState
{
	CControls *m_pControls;
	int *m_apVariables[NUM_DUMMIES];
};

void CControls::ConKeyInputState(IConsole::IResult *pResult, void *pUserData)
{
	CInputState *pState = (CInputState *)pUserData;

	if(pState->m_pControls->GameClient()->m_GameInfo.m_BugDDRaceInput && pState->m_pControls->GameClient()->m_Snap.m_SpecInfo.m_Active)
		return;

	*pState->m_apVariables[g_Config.m_ClDummy] = pResult->GetInteger(0);
}

void CControls::ConKeyInputCounter(IConsole::IResult *pResult, void *pUserData)
{
	CInputState *pState = (CInputState *)pUserData;

	if((pState->m_pControls->GameClient()->m_GameInfo.m_BugDDRaceInput && pState->m_pControls->GameClient()->m_Snap.m_SpecInfo.m_Active) || pState->m_pControls->GameClient()->m_Spectator.IsActive())
		return;

	int *pVariable = pState->m_apVariables[g_Config.m_ClDummy];
	if(((*pVariable) & 1) != pResult->GetInteger(0))
		(*pVariable)++;
	*pVariable &= INPUT_STATE_MASK;
}

struct CInputSet
{
	CControls *m_pControls;
	int *m_apVariables[NUM_DUMMIES];
	int m_Value;
};

void CControls::ConKeyInputSet(IConsole::IResult *pResult, void *pUserData)
{
	CInputSet *pSet = (CInputSet *)pUserData;
	if(pResult->GetInteger(0))
	{
		*pSet->m_apVariables[g_Config.m_ClDummy] = pSet->m_Value;
	}
}

void CControls::ConKeyInputNextPrevWeapon(IConsole::IResult *pResult, void *pUserData)
{
	CInputSet *pSet = (CInputSet *)pUserData;
	ConKeyInputCounter(pResult, pSet);
	pSet->m_pControls->m_aInputData[g_Config.m_ClDummy].m_WantedWeapon = 0;
}

void CControls::OnConsoleInit()
{
	// game commands
	{
		static CInputState s_State = {this, {&m_aInputDirectionLeft[0], &m_aInputDirectionLeft[1]}};
		Console()->Register("+left", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Move left");
	}
	{
		static CInputState s_State = {this, {&m_aInputDirectionRight[0], &m_aInputDirectionRight[1]}};
		Console()->Register("+right", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Move right");
	}
	{
		static CInputState s_State = {this, {&m_aInputData[0].m_Jump, &m_aInputData[1].m_Jump}};
		Console()->Register("+jump", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Jump");
	}
	{
		static CInputState s_State = {this, {&m_aInputData[0].m_Hook, &m_aInputData[1].m_Hook}};
		Console()->Register("+hook", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Hook");
	}
	{
		static CInputState s_State = {this, {&m_aInputData[0].m_Fire, &m_aInputData[1].m_Fire}};
		Console()->Register("+fire", "", CFGFLAG_CLIENT, ConKeyInputCounter, &s_State, "Fire");
	}
	{
		static CInputState s_State = {this, {&m_aShowHookColl[0], &m_aShowHookColl[1]}};
		Console()->Register("+showhookcoll", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Show Hook Collision");
	}

	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_WantedWeapon, &m_aInputData[1].m_WantedWeapon}, 1};
		Console()->Register("+weapon1", "", CFGFLAG_CLIENT, ConKeyInputSet, &s_Set, "Switch to hammer");
	}
	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_WantedWeapon, &m_aInputData[1].m_WantedWeapon}, 2};
		Console()->Register("+weapon2", "", CFGFLAG_CLIENT, ConKeyInputSet, &s_Set, "Switch to gun");
	}
	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_WantedWeapon, &m_aInputData[1].m_WantedWeapon}, 3};
		Console()->Register("+weapon3", "", CFGFLAG_CLIENT, ConKeyInputSet, &s_Set, "Switch to shotgun");
	}
	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_WantedWeapon, &m_aInputData[1].m_WantedWeapon}, 4};
		Console()->Register("+weapon4", "", CFGFLAG_CLIENT, ConKeyInputSet, &s_Set, "Switch to grenade");
	}
	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_WantedWeapon, &m_aInputData[1].m_WantedWeapon}, 5};
		Console()->Register("+weapon5", "", CFGFLAG_CLIENT, ConKeyInputSet, &s_Set, "Switch to laser");
	}

	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_NextWeapon, &m_aInputData[1].m_NextWeapon}, 0};
		Console()->Register("+nextweapon", "", CFGFLAG_CLIENT, ConKeyInputNextPrevWeapon, &s_Set, "Switch to next weapon");
	}
	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_PrevWeapon, &m_aInputData[1].m_PrevWeapon}, 0};
		Console()->Register("+prevweapon", "", CFGFLAG_CLIENT, ConKeyInputNextPrevWeapon, &s_Set, "Switch to previous weapon");
	}

	Console()->Register("tp_to_cursor", "", CFGFLAG_CLIENT, ConTpToCursor, this, "Practice: teleport yourself to your cursor");
	Console()->Register("tp_dummy_to_cursor", "", CFGFLAG_CLIENT, ConTpDummyToCursor, this, "Practice: teleport the other tee to your cursor");
}

void CControls::ConTpToCursor(IConsole::IResult *pResult, void *pUserData)
{
	CControls *pSelf = (CControls *)pUserData;
	if(pSelf->Client()->State() != IClient::STATE_ONLINE)
		return;

	pSelf->SendPracticeTeleportToCursor(g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN);
}

void CControls::ConTpDummyToCursor(IConsole::IResult *pResult, void *pUserData)
{
	CControls *pSelf = (CControls *)pUserData;
	if(pSelf->Client()->State() != IClient::STATE_ONLINE)
		return;

	if(!pSelf->Client()->DummyConnected())
	{
		pSelf->GameClient()->Echo(Localize("Dummy is not connected"));
		return;
	}

	// Send on the inactive connection; the server only teleports the sender.
	pSelf->SendPracticeTeleportToCursor(g_Config.m_ClDummy ? IClient::CONN_MAIN : IClient::CONN_DUMMY);
}

vec2 CControls::CursorWorldPos() const
{
	const vec2 Center = GameClient()->m_Camera.m_Center;
	const vec2 TargetPos = m_aTargetPos[g_Config.m_ClDummy];
	return Center + (TargetPos - Center) * GameClient()->m_Camera.m_Zoom;
}

void CControls::SendPracticeTeleportToCursor(int Conn)
{
	const vec2 CursorPos = CursorWorldPos();
	CNetMsg_Cl_PracticeTeleport Msg;
	Msg.m_X = round_to_int(CursorPos.x);
	Msg.m_Y = round_to_int(CursorPos.y);
	Client()->SendPackMsg(Conn, &Msg, MSGFLAG_VITAL);
}

void CControls::OnMessage(int Msg, void *pRawMsg)
{
	if(Msg == NETMSGTYPE_SV_WEAPONPICKUP)
	{
		CNetMsg_Sv_WeaponPickup *pMsg = (CNetMsg_Sv_WeaponPickup *)pRawMsg;
		if(g_Config.m_ClAutoswitchWeapons)
			m_aInputData[g_Config.m_ClDummy].m_WantedWeapon = pMsg->m_Weapon + 1;
		// We don't really know ammo count, until we'll switch to that weapon, but any non-zero count will suffice here
		m_aAmmoCount[std::max(0, pMsg->m_Weapon % NUM_WEAPONS)] = 10;
	}
}

int CControls::SnapInput(int *pData)
{
	// update player state
	if(GameClient()->m_Chat.IsActive())
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags = PLAYERFLAG_CHATTING;
	else if(GameClient()->m_Menus.IsActive())
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags = PLAYERFLAG_IN_MENU;
	else
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags = PLAYERFLAG_PLAYING;

	if(GameClient()->m_Scoreboard.IsActive())
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags |= PLAYERFLAG_SCOREBOARD;

	if(Client()->ServerCapAnyPlayerFlag() && GameClient()->m_Controls.m_aShowHookColl[g_Config.m_ClDummy])
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags |= PLAYERFLAG_AIM;

	if(Client()->ServerCapAnyPlayerFlag() && GameClient()->m_Camera.CamType() == CCamera::CAMTYPE_SPEC)
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags |= PLAYERFLAG_SPEC_CAM;

	switch(m_aMouseInputType[g_Config.m_ClDummy])
	{
	case CControls::EMouseInputType::AUTOMATED:
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags |= PLAYERFLAG_INPUT_ABSOLUTE;
		break;
	case CControls::EMouseInputType::ABSOLUTE:
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags |= PLAYERFLAG_INPUT_ABSOLUTE | PLAYERFLAG_INPUT_MANUAL;
		break;
	case CControls::EMouseInputType::RELATIVE:
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags |= PLAYERFLAG_INPUT_MANUAL;
		break;
	}

	bool Send = m_aLastData[g_Config.m_ClDummy].m_PlayerFlags != m_aInputData[g_Config.m_ClDummy].m_PlayerFlags;

	m_aLastData[g_Config.m_ClDummy].m_PlayerFlags = m_aInputData[g_Config.m_ClDummy].m_PlayerFlags;

	// we freeze the input if chat or menu is activated
	if(!(m_aInputData[g_Config.m_ClDummy].m_PlayerFlags & PLAYERFLAG_PLAYING))
	{
		if(!GameClient()->m_GameInfo.m_BugDDRaceInput)
			ResetInput(g_Config.m_ClDummy);

		mem_copy(pData, &m_aInputData[g_Config.m_ClDummy], sizeof(m_aInputData[0]));

		// set the target anyway though so that we can keep seeing our surroundings,
		// even if chat or menu are activated
		m_aInputData[g_Config.m_ClDummy].m_TargetX = (int)m_aMousePos[g_Config.m_ClDummy].x;
		m_aInputData[g_Config.m_ClDummy].m_TargetY = (int)m_aMousePos[g_Config.m_ClDummy].y;

		// send once a second just to be sure
		Send = Send || time_get() > m_LastSendTime + time_freq();
	}
	else
	{
		m_aInputData[g_Config.m_ClDummy].m_TargetX = (int)m_aMousePos[g_Config.m_ClDummy].x;
		m_aInputData[g_Config.m_ClDummy].m_TargetY = (int)m_aMousePos[g_Config.m_ClDummy].y;

		if(g_Config.m_ClSubTickAiming && m_aMousePosOnAction[g_Config.m_ClDummy] != vec2(0.0f, 0.0f))
		{
			m_aInputData[g_Config.m_ClDummy].m_TargetX = (int)m_aMousePosOnAction[g_Config.m_ClDummy].x;
			m_aInputData[g_Config.m_ClDummy].m_TargetY = (int)m_aMousePosOnAction[g_Config.m_ClDummy].y;
			m_aMousePosOnAction[g_Config.m_ClDummy] = vec2(0.0f, 0.0f);
		}

		// MoonClient: spin bot - spin the aim around; while the hook or fire keys
		// are pressed the real cursor direction is used so you can hook anywhere.
		if(g_Config.m_ClSpinBot != 0 &&
			(m_aInputData[g_Config.m_ClDummy].m_Hook & 1) == 0 &&
			(m_aInputData[g_Config.m_ClDummy].m_Fire & 1) == 0 &&
			GameClient()->m_Snap.m_pGameInfoObj != nullptr &&
			!GameClient()->m_Snap.m_SpecInfo.m_Active)
		{
			constexpr double PiD = 3.14159265358979323846;
			const double AngleSpeed = g_Config.m_ClSpinBotSpeed * PiD / 180.0;
			const double Angle = Client()->LocalTime() * AngleSpeed;
			m_aInputData[g_Config.m_ClDummy].m_TargetX = (int)(std::cos(Angle) * 150.0);
			m_aInputData[g_Config.m_ClDummy].m_TargetY = (int)(std::sin(Angle) * 150.0);
		}

		// MoonClient: aim bot hook snap - when the hook key is pressed while a
		// player is inside the FOV zone, aim the hook at that player. The hook
		// is only sent because the user pressed the hook key, never on its own.
		if(g_Config.m_ClAimAssist != 0 && g_Config.m_ClAimAssistHook != 0 &&
			GameClient()->m_Snap.m_pGameInfoObj != nullptr &&
			!GameClient()->m_Snap.m_SpecInfo.m_Active &&
			(m_aInputData[g_Config.m_ClDummy].m_Hook & 1) != 0 &&
			m_MoonAimTargetId >= 0 &&
			GameClient()->m_Snap.m_aCharacters[m_MoonAimTargetId].m_Active)
		{
			const int MyId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
			if(MyId >= 0)
			{
				const vec2 HookOffset = GameClient()->m_aClients[m_MoonAimTargetId].m_Predicted.m_Pos - GameClient()->m_aClients[MyId].m_Predicted.m_Pos;
				if(length(HookOffset) > 0.001f)
				{
					m_aInputData[g_Config.m_ClDummy].m_TargetX = (int)HookOffset.x;
					m_aInputData[g_Config.m_ClDummy].m_TargetY = (int)HookOffset.y;
				}
			}
		}

		if(!m_aInputData[g_Config.m_ClDummy].m_TargetX && !m_aInputData[g_Config.m_ClDummy].m_TargetY)
		{
			m_aInputData[g_Config.m_ClDummy].m_TargetX = 1;
			m_aMousePos[g_Config.m_ClDummy].x = 1;
		}

		// set direction
		m_aInputData[g_Config.m_ClDummy].m_Direction = 0;
		if(m_aInputDirectionLeft[g_Config.m_ClDummy] && !m_aInputDirectionRight[g_Config.m_ClDummy])
			m_aInputData[g_Config.m_ClDummy].m_Direction = -1;
		if(!m_aInputDirectionLeft[g_Config.m_ClDummy] && m_aInputDirectionRight[g_Config.m_ClDummy])
			m_aInputData[g_Config.m_ClDummy].m_Direction = 1;

		// dummy copy moves
		if(g_Config.m_ClDummyCopyMoves)
		{
			CNetObj_PlayerInput *pDummyInput = &GameClient()->m_DummyInput;

			// Don't copy any input to dummy when spectating others
			if(!GameClient()->m_Snap.m_SpecInfo.m_Active || GameClient()->m_Snap.m_SpecInfo.m_SpectatorId < 0)
			{
				pDummyInput->m_Direction = m_aInputData[g_Config.m_ClDummy].m_Direction;
				pDummyInput->m_Hook = m_aInputData[g_Config.m_ClDummy].m_Hook;
				pDummyInput->m_Jump = m_aInputData[g_Config.m_ClDummy].m_Jump;
				pDummyInput->m_PlayerFlags = m_aInputData[g_Config.m_ClDummy].m_PlayerFlags;
				pDummyInput->m_TargetX = m_aInputData[g_Config.m_ClDummy].m_TargetX;
				pDummyInput->m_TargetY = m_aInputData[g_Config.m_ClDummy].m_TargetY;
				pDummyInput->m_WantedWeapon = m_aInputData[g_Config.m_ClDummy].m_WantedWeapon;

				if(!g_Config.m_ClDummyControl)
					pDummyInput->m_Fire += m_aInputData[g_Config.m_ClDummy].m_Fire - m_aLastData[g_Config.m_ClDummy].m_Fire;

				pDummyInput->m_NextWeapon += m_aInputData[g_Config.m_ClDummy].m_NextWeapon - m_aLastData[g_Config.m_ClDummy].m_NextWeapon;
				pDummyInput->m_PrevWeapon += m_aInputData[g_Config.m_ClDummy].m_PrevWeapon - m_aLastData[g_Config.m_ClDummy].m_PrevWeapon;
			}

			m_aInputData[!g_Config.m_ClDummy] = *pDummyInput;
		}

		if(g_Config.m_ClDummyControl)
		{
			CNetObj_PlayerInput *pDummyInput = &GameClient()->m_DummyInput;
			pDummyInput->m_Jump = g_Config.m_ClDummyJump;

			if(g_Config.m_ClDummyFire)
				pDummyInput->m_Fire = g_Config.m_ClDummyFire;
			else if((pDummyInput->m_Fire & 1) != 0)
				pDummyInput->m_Fire++;

			pDummyInput->m_Hook = g_Config.m_ClDummyHook;
			m_aInputData[!g_Config.m_ClDummy] = *pDummyInput;
		}

		// stress testing
		if(g_Config.m_DbgStress)
		{
			float t = Client()->LocalTime();
			mem_zero(&m_aInputData[g_Config.m_ClDummy], sizeof(m_aInputData[0]));

			m_aInputData[g_Config.m_ClDummy].m_Direction = ((int)t / 2) & 1;
			m_aInputData[g_Config.m_ClDummy].m_Jump = ((int)t);
			m_aInputData[g_Config.m_ClDummy].m_Fire = ((int)(t * 10));
			m_aInputData[g_Config.m_ClDummy].m_Hook = ((int)(t * 2)) & 1;
			m_aInputData[g_Config.m_ClDummy].m_WantedWeapon = ((int)t) % NUM_WEAPONS;
			m_aInputData[g_Config.m_ClDummy].m_TargetX = (int)(std::sin(t * 3) * 100.0f);
			m_aInputData[g_Config.m_ClDummy].m_TargetY = (int)(std::cos(t * 3) * 100.0f);
		}

		// check if we need to send input
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_Direction != m_aLastData[g_Config.m_ClDummy].m_Direction;
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_Jump != m_aLastData[g_Config.m_ClDummy].m_Jump;
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_Fire != m_aLastData[g_Config.m_ClDummy].m_Fire;
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_Hook != m_aLastData[g_Config.m_ClDummy].m_Hook;
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_WantedWeapon != m_aLastData[g_Config.m_ClDummy].m_WantedWeapon;
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_NextWeapon != m_aLastData[g_Config.m_ClDummy].m_NextWeapon;
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_PrevWeapon != m_aLastData[g_Config.m_ClDummy].m_PrevWeapon;
		Send = Send || time_get() > m_LastSendTime + time_freq() / 25; // send at least 25 Hz
		Send = Send || (GameClient()->m_Snap.m_pLocalCharacter && GameClient()->m_Snap.m_pLocalCharacter->m_Weapon == WEAPON_NINJA && (m_aInputData[g_Config.m_ClDummy].m_Direction || m_aInputData[g_Config.m_ClDummy].m_Jump || m_aInputData[g_Config.m_ClDummy].m_Hook));
	}

	// copy and return size
	m_aLastData[g_Config.m_ClDummy] = m_aInputData[g_Config.m_ClDummy];

	if(!Send)
		return 0;

	m_LastSendTime = time_get();
	mem_copy(pData, &m_aInputData[g_Config.m_ClDummy], sizeof(m_aInputData[0]));
	return sizeof(m_aInputData[0]);
}

void CControls::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(g_Config.m_ClAutoswitchWeaponsOutOfAmmo && !GameClient()->m_GameInfo.m_UnlimitedAmmo && GameClient()->m_Snap.m_pLocalCharacter)
	{
		// Keep track of ammo count, we know weapon ammo only when we switch to that weapon, this is tracked on server and protocol does not track that
		m_aAmmoCount[std::max(0, GameClient()->m_Snap.m_pLocalCharacter->m_Weapon % NUM_WEAPONS)] = GameClient()->m_Snap.m_pLocalCharacter->m_AmmoCount;
		// Autoswitch weapon if we're out of ammo
		if(m_aInputData[g_Config.m_ClDummy].m_Fire % 2 != 0 &&
			GameClient()->m_Snap.m_pLocalCharacter->m_AmmoCount == 0 &&
			GameClient()->m_Snap.m_pLocalCharacter->m_Weapon != WEAPON_HAMMER &&
			GameClient()->m_Snap.m_pLocalCharacter->m_Weapon != WEAPON_NINJA)
		{
			int Weapon;
			for(Weapon = WEAPON_LASER; Weapon > WEAPON_GUN; Weapon--)
			{
				if(Weapon == GameClient()->m_Snap.m_pLocalCharacter->m_Weapon)
					continue;
				if(m_aAmmoCount[Weapon] > 0)
					break;
			}
			if(Weapon != GameClient()->m_Snap.m_pLocalCharacter->m_Weapon)
				m_aInputData[g_Config.m_ClDummy].m_WantedWeapon = Weapon + 1;
		}
	}

	// update target pos
	if(GameClient()->m_Snap.m_pGameInfoObj && !GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		// make sure to compensate for smooth dyncam to ensure the cursor stays still in world space if zoomed
		vec2 DyncamOffsetDelta = GameClient()->m_Camera.m_DyncamTargetCameraOffset - GameClient()->m_Camera.m_aDyncamCurrentCameraOffset[g_Config.m_ClDummy];
		float Zoom = GameClient()->m_Camera.m_Zoom;
		m_aTargetPos[g_Config.m_ClDummy] = GameClient()->m_LocalCharacterPos + m_aMousePos[g_Config.m_ClDummy] - DyncamOffsetDelta + DyncamOffsetDelta / Zoom;
	}
	else if(GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_UsePosition)
	{
		m_aTargetPos[g_Config.m_ClDummy] = GameClient()->m_Snap.m_SpecInfo.m_Position + m_aMousePos[g_Config.m_ClDummy];
	}
	else
	{
		m_aTargetPos[g_Config.m_ClDummy] = m_aMousePos[g_Config.m_ClDummy];
	}

	// MoonClient: drag aim assist - lock the aim on the nearest hookable player
	m_MoonDragTargetId = -1;
	if(g_Config.m_ClDragAim != 0 && Client()->State() == IClient::STATE_ONLINE &&
		GameClient()->m_Snap.m_pGameInfoObj && !GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		const int MyId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
		if(MyId >= 0)
		{
			const vec2 MyPos = GameClient()->m_aClients[MyId].m_Predicted.m_Pos;
			const float HookLength = GameClient()->m_aClients[MyId].m_Predicted.m_Tuning.m_HookLength;
			int BestId = -1;
			float BestDist = 0.0f;
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(i == MyId || !GameClient()->m_Snap.m_aCharacters[i].m_Active)
					continue;
				// teammates declared to be dragged (drag teams) first, then plain distance
				vec2 TargetPos = GameClient()->m_aClients[i].m_Predicted.m_Pos;
				float Dist = distance(MyPos, TargetPos);
				if(Dist > HookLength)
					continue;
				if(BestId == -1 || Dist < BestDist)
				{
					BestId = i;
					BestDist = Dist;
				}
			}
			if(BestId != -1)
			{
				m_MoonDragTargetId = BestId;
				m_aTargetPos[g_Config.m_ClDummy] = GameClient()->m_aClients[BestId].m_Predicted.m_Pos;
			}
		}
	}

	// MoonClient: aim bot - hook a player when they are inside the FOV zone
	m_MoonAimTargetId = -1;
	if(g_Config.m_ClAimAssist != 0 && g_Config.m_ClDragAim == 0 &&
		Client()->State() == IClient::STATE_ONLINE &&
		GameClient()->m_Snap.m_pGameInfoObj && !GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		const int MyId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
		if(MyId >= 0)
		{
			const vec2 MyPos = GameClient()->m_aClients[MyId].m_Predicted.m_Pos;
			const vec2 AimOffset = m_aTargetPos[g_Config.m_ClDummy] - MyPos;
			const float AimLen = length(AimOffset);
			if(AimLen > 1.0f)
			{
				const vec2 AimDir = AimOffset / AimLen;
				const float HookLength = GameClient()->m_aClients[MyId].m_Predicted.m_Tuning.m_HookLength;
				constexpr double PiD = 3.14159265358979323846;
				const float MinCos = std::cos(std::clamp(g_Config.m_ClAimAssistFov, 0, 180) * (float)(PiD / 180.0));
				int BestId = -1;
				float BestScore = 0.0f;
				vec2 BestDir = vec2(1.0f, 0.0f);
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(i == MyId || !GameClient()->m_Snap.m_aCharacters[i].m_Active)
						continue;
					const vec2 TargetPos = GameClient()->m_aClients[i].m_Predicted.m_Pos;
					const vec2 To = TargetPos - MyPos;
					const float Dist = length(To);
					if(Dist > HookLength || Dist < 2.0f)
						continue;
					const vec2 Dir = To / Dist;
					const float CosTarget = dot(AimDir, Dir);
					if(CosTarget < MinCos) // outside the configured FOV cone
						continue;
					// prefer the target closest to the cursor direction, tie-break by distance
					const float Score = (1.0f - CosTarget) + Dist / HookLength * 0.25f;
					if(BestId == -1 || Score < BestScore)
					{
						BestId = i;
						BestScore = Score;
						BestDir = Dir;
					}
				}
				if(BestId != -1)
				{
					m_MoonAimTargetId = BestId;
				}
			}
		}
	}
}

bool CControls::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(GameClient()->IsWorldPaused())
		return false;

	if(CursorType == IInput::CURSOR_JOYSTICK && g_Config.m_InpControllerAbsolute && GameClient()->m_Snap.m_pGameInfoObj && !GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		vec2 AbsoluteDirection;
		if(Input()->GetActiveJoystick()->Absolute(&AbsoluteDirection.x, &AbsoluteDirection.y))
		{
			m_aMousePos[g_Config.m_ClDummy] = AbsoluteDirection * GetMaxMouseDistance();
			GameClient()->m_Controls.m_aMouseInputType[g_Config.m_ClDummy] = CControls::EMouseInputType::ABSOLUTE;
		}
		return true;
	}

	float Factor = 1.0f;
	if(g_Config.m_ClDyncam && g_Config.m_ClDyncamMousesens)
	{
		Factor = g_Config.m_ClDyncamMousesens / 100.0f;
	}
	else
	{
		switch(CursorType)
		{
		case IInput::CURSOR_MOUSE:
			Factor = g_Config.m_InpMousesens / 100.0f;
			break;
		case IInput::CURSOR_JOYSTICK:
			Factor = g_Config.m_InpControllerSens / 100.0f;
			break;
		default:
			dbg_assert_failed("CControls::OnCursorMove CursorType %d", (int)CursorType);
		}
	}

	if(GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId < 0)
		Factor *= GameClient()->m_Camera.m_Zoom;

	m_aMousePos[g_Config.m_ClDummy] += vec2(x, y) * Factor;
	GameClient()->m_Controls.m_aMouseInputType[g_Config.m_ClDummy] = CControls::EMouseInputType::RELATIVE;
	ClampMousePos();
	return true;
}

void CControls::ClampMousePos()
{
	if(GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId < 0)
	{
		m_aMousePos[g_Config.m_ClDummy].x = std::clamp(m_aMousePos[g_Config.m_ClDummy].x, -201.0f * 32, (Collision()->GetWidth() + 201.0f) * 32.0f);
		m_aMousePos[g_Config.m_ClDummy].y = std::clamp(m_aMousePos[g_Config.m_ClDummy].y, -201.0f * 32, (Collision()->GetHeight() + 201.0f) * 32.0f);
	}
	else
	{
		const float MouseMin = GetMinMouseDistance();
		const float MouseMax = GetMaxMouseDistance();

		float MouseDistance = length(m_aMousePos[g_Config.m_ClDummy]);
		if(MouseDistance < 0.001f)
		{
			m_aMousePos[g_Config.m_ClDummy].x = 0.001f;
			m_aMousePos[g_Config.m_ClDummy].y = 0;
			MouseDistance = 0.001f;
		}
		if(MouseDistance < MouseMin)
			m_aMousePos[g_Config.m_ClDummy] = normalize_pre_length(m_aMousePos[g_Config.m_ClDummy], MouseDistance) * MouseMin;
		MouseDistance = length(m_aMousePos[g_Config.m_ClDummy]);
		if(MouseDistance > MouseMax)
			m_aMousePos[g_Config.m_ClDummy] = normalize_pre_length(m_aMousePos[g_Config.m_ClDummy], MouseDistance) * MouseMax;
	}
}

float CControls::GetMinMouseDistance() const
{
	return g_Config.m_ClDyncam ? g_Config.m_ClDyncamMinDistance : g_Config.m_ClMouseMinDistance;
}

float CControls::GetMaxMouseDistance() const
{
	float CameraMaxDistance = 200.0f;
	float FollowFactor = (g_Config.m_ClDyncam ? g_Config.m_ClDyncamFollowFactor : g_Config.m_ClMouseFollowfactor) / 100.0f;
	float DeadZone = g_Config.m_ClDyncam ? g_Config.m_ClDyncamDeadzone : g_Config.m_ClMouseDeadzone;
	float MaxDistance = g_Config.m_ClDyncam ? g_Config.m_ClDyncamMaxDistance : g_Config.m_ClMouseMaxDistance;
	return std::min(FollowFactor != 0.0f ? CameraMaxDistance / FollowFactor + DeadZone : MaxDistance, MaxDistance);
}
